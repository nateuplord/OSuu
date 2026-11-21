import os
import glob
import struct
import numpy as np
from PIL import Image

# Configuration
FRAME_DIR = "bad-apple-frames/"  # Folder containing your extracted Bad Apple frames
WIDTH, HEIGHT = 160, 120  # Must be multiples of 8

# 32-bit tile IDs: true (non-upscaled) 160x120 content needs ~88,151 distinct
# 8x8 tiles, which overflows a 16-bit id space (max 65536) - that was fine at
# 64x48 (45,436 tiles) but would silently corrupt ~3.8% of tiles here.
PROGRESS_EVERY = 250  # frames between progress prints

GRID_COLS = WIDTH // 8
GRID_ROWS = HEIGHT // 8
TILES_PER_FRAME = GRID_COLS * GRID_ROWS  # 300


def rle_encode_ids(ids):
    """(run_len: 1 byte, tile_id: 4 bytes LE) pairs, run capped at 255."""
    out = bytearray()
    n = len(ids)
    i = 0
    while i < n:
        val = ids[i]
        j = i + 1
        while j < n and ids[j] == val and (j - i) < 255:
            j += 1
        out.append(j - i)
        out += struct.pack("<I", val)
        i = j
    return bytes(out)


def process_frames_and_export():
    frame_files = sorted(glob.glob(os.path.join(FRAME_DIR, "*.png")))
    if not frame_files:
        raise ValueError("No frames found. Extract video frames to .png first.")

    palette_map = {}
    palette_bytes = bytearray()  # Stores the N x 8 bytes for the palette
    encoded_frames = bytearray()  # Stores the flat stream of tile IDs (4 bytes each)

    total_frames = len(frame_files)
    for frame_num, filepath in enumerate(frame_files):
        img = Image.open(filepath).convert("L").resize((WIDTH, HEIGHT))
        binary_frame = np.array(img) > 128

        for y in range(0, HEIGHT, 8):
            for x in range(0, WIDTH, 8):
                tile = binary_frame[y : y + 8, x : x + 8]

                # Fixed bit order: LSB-first (bit 0 is leftmost) to match framebuffer.c
                tile_hash = np.packbits(tile, bitorder="little").tobytes()

                if tile_hash not in palette_map:
                    new_id = len(palette_map)
                    palette_map[tile_hash] = new_id
                    # Append the 8-byte chunk directly to our binary palette array
                    palette_bytes.extend(tile_hash)

                # Append the 4-byte ID (little-endian) to the flat frame stream
                encoded_frames.extend(palette_map[tile_hash].to_bytes(4, "little"))

        if frame_num % PROGRESS_EVERY == 0 or frame_num == total_frames - 1:
            print(f"[{frame_num + 1}/{total_frames}] palette={len(palette_map)} tiles")

    print(f"Processed {len(frame_files)} frames.")
    print(f"Final palette size: {len(palette_map)} unique 8x8 tiles.")

    # Write palette.bin (you need this alongside frames.bin!)
    with open("palette.bin", "wb") as f:
        f.write(palette_bytes)
    print(f"Wrote palette.bin ({len(palette_bytes)} bytes)")

    # Temporal RLE: transpose to TILES_PER_FRAME per-grid-cell sequences (each
    # covering every frame) and RLE each one independently, since a mostly-
    # static camera means a screen cell tends to hold the same tile for many
    # consecutive frames - far more redundancy than RLE-ing a single frame's
    # tiles left-to-right (measured 2.2x vs 1.28x on the 64x48 version).
    num_frames = len(encoded_frames) // 4 // TILES_PER_FRAME
    tile_ids = struct.unpack(f"<{num_frames * TILES_PER_FRAME}I", bytes(encoded_frames))

    streams = []
    for pos in range(TILES_PER_FRAME):
        cell_seq = tile_ids[pos::TILES_PER_FRAME]
        streams.append(rle_encode_ids(cell_seq))

    with open("frames.bin", "wb") as f:
        f.write(struct.pack("<I", num_frames))
        for s in streams:
            f.write(struct.pack("<I", len(s)))
        for s in streams:
            f.write(s)

    total_size = 4 + TILES_PER_FRAME * 4 + sum(len(s) for s in streams)
    print(f"Wrote frames.bin ({total_size} bytes, {num_frames} frames)")


if __name__ == "__main__":
    process_frames_and_export()
