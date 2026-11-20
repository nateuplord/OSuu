import os
import glob
import numpy as np
from PIL import Image

# Configuration
FRAME_DIR = "bad-apple-frames/"  # Folder containing your extracted Bad Apple frames
WIDTH, HEIGHT = 64, 48  # Must be multiples of 8

# 16-bit tile IDs: measured 45,436 truly distinct 8x8 tiles across the whole video
MAX_PALETTE_SIZE = 65536
PROGRESS_EVERY = 250  # frames between progress prints


def process_frames_and_export():
    frame_files = sorted(glob.glob(os.path.join(FRAME_DIR, "*.png")))
    if not frame_files:
        raise ValueError("No frames found. Extract video frames to .png first.")

    palette_map = {}
    palette_bytes = bytearray()  # Stores the N x 8 bytes for the palette
    encoded_frames = bytearray()  # Stores the flat stream of tile IDs (2 bytes each)
    overflow_count = 0

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
                    if len(palette_map) < MAX_PALETTE_SIZE:
                        new_id = len(palette_map)
                        palette_map[tile_hash] = new_id
                        # Append the 8-byte chunk directly to our binary palette array
                        palette_bytes.extend(tile_hash)
                    else:
                        palette_map[tile_hash] = 0
                        overflow_count += 1

                # Append the 2-byte ID (little-endian) to the flat frame stream
                encoded_frames.extend(palette_map[tile_hash].to_bytes(2, "little"))

        if frame_num % PROGRESS_EVERY == 0 or frame_num == total_frames - 1:
            print(
                f"[{frame_num + 1}/{total_frames}] "
                f"palette={len(palette_map)} tiles, overflow={overflow_count}"
            )

    print(f"Processed {len(frame_files)} frames.")
    print(f"Final palette size: {len(palette_map)} unique 8x8 tiles.")
    if overflow_count:
        print(f"WARNING: {overflow_count} tile instances still overflowed to black.")

    # Write palette.bin (you need this alongside f0.bin..fN.bin!)
    with open("palette.bin", "wb") as f:
        f.write(palette_bytes)
    print(f"Wrote palette.bin ({len(palette_bytes)} bytes)")

    CHUNK_FRAMES = 340  # 340 * 96 bytes = 32,640 bytes (~32 KB per chunk)
    CHUNK_BYTES = CHUNK_FRAMES * 96

    for idx in range(0, len(encoded_frames), CHUNK_BYTES):
        chunk_num = idx // CHUNK_BYTES
        chunk_data = encoded_frames[idx : idx + CHUNK_BYTES]
        with open(f"f{chunk_num}.bin", "wb") as f:
            f.write(chunk_data)
        print(f"Wrote f{chunk_num}.bin ({len(chunk_data)} bytes)")


if __name__ == "__main__":
    process_frames_and_export()
