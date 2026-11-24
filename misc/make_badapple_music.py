#!/usr/bin/env python3
"""Convert assets/bad-apple.mid into the PC-speaker score used by badapple.

The MIDI file is a dense piano roll, so the generator extracts a single lead
line: at each 10 ms sample it takes the highest sounding pitch. The original
MIDI tempo is preserved; badapple paces the video to song_ms instead.

Output format (all integers little-endian):

    magic[4]      "BAMU"
    version:u32   1
    note_count:u32
    song_ms:u32   original MIDI duration, used to time the video
    notes[note_count]:
        start_ms:u32
        freq_hz:u16   (0 means silence, though the generator omits rests)
        duration_ms:u16
"""

import argparse
import heapq
import struct
from collections import defaultdict
from pathlib import Path

HERE = Path(__file__).resolve().parent
DEFAULT_MIDI = HERE.parent / "assets" / "bad-apple.mid"
DEFAULT_OUTPUT = HERE / "music.bin"

SAMPLE_MS = 10
MIN_NOTE_MS = 60
DEFAULT_BPM = 120.0


class MidiError(ValueError):
    pass


def read_vlq(data, index):
    value = 0
    while True:
        if index >= len(data):
            raise MidiError("truncated variable-length quantity")
        byte = data[index]
        index += 1
        value = (value << 7) | (byte & 0x7F)
        if not byte & 0x80:
            return value, index


def parse_midi(path):
    data = Path(path).read_bytes()
    if data[:4] != b"MThd":
        raise MidiError("not a MIDI file")

    header_len = struct.unpack_from(">I", data, 4)[0]
    fmt, track_count, division = struct.unpack_from(">HHH", data, 8)
    if division == 0 or division & 0x8000:
        raise MidiError("only metrical (ticks-per-quarter) MIDI is supported")
    if fmt not in (0, 1, 2):
        raise MidiError(f"unsupported MIDI format {fmt}")

    offset = 8 + header_len
    tempo_events = []
    note_events = []
    max_tick = 0

    for _ in range(track_count):
        if data[offset : offset + 4] != b"MTrk":
            raise MidiError("missing MTrk chunk")
        length = struct.unpack_from(">I", data, offset + 4)[0]
        track = data[offset + 8 : offset + 8 + length]
        offset += 8 + length

        index = 0
        tick = 0
        running_status = None
        while index < len(track):
            delta, index = read_vlq(track, index)
            tick += delta
            max_tick = max(max_tick, tick)

            status = track[index]
            if status & 0x80:
                index += 1
                if status < 0xF0:
                    running_status = status
            else:
                if running_status is None:
                    raise MidiError("data byte without running status")
                status = running_status

            if status == 0xFF:
                meta_type = track[index]
                index += 1
                meta_len, index = read_vlq(track, index)
                payload = track[index : index + meta_len]
                index += meta_len
                if meta_type == 0x51 and meta_len == 3:
                    micros = struct.unpack(">I", b"\x00" + payload)[0]
                    tempo_events.append((tick, micros))
                continue

            if status in (0xF0, 0xF7):
                sysex_len, index = read_vlq(track, index)
                index += sysex_len
                continue

            event_type = status & 0xF0
            if event_type in (0x80, 0x90, 0xA0, 0xB0, 0xE0):
                data1 = track[index]
                data2 = track[index + 1]
                index += 2
                channel = status & 0x0F
                if event_type == 0x90 and data2 > 0:
                    note_events.append((tick, True, data1, channel))
                elif event_type == 0x80 or (event_type == 0x90 and data2 == 0):
                    note_events.append((tick, False, data1, channel))
            elif event_type in (0xC0, 0xD0):
                index += 1
            else:
                raise MidiError(f"unsupported status byte 0x{status:02x}")

    tempo_events.sort()
    return division, tempo_events, note_events, max_tick


def build_notes(division, tempo_events, note_events, max_tick, channel):
    # The second track is a "Xylophone" placeholder whose note-ons all have
    # velocity 0;
    # treating those as note-offs would prematurely close piano notes. Keep the
    # channel that actually contains sounding notes.
    note_events = [event for event in note_events if event[3] == channel]

    def tick_to_ms(tick):
        micros = round(60000000.0 / DEFAULT_BPM)
        previous_tick = 0
        ms = 0.0
        for event_tick, event_micros in tempo_events:
            if event_tick > tick:
                break
            ms += (event_tick - previous_tick) * micros / (1000.0 * division)
            previous_tick = event_tick
            micros = event_micros
        ms += (tick - previous_tick) * micros / (1000.0 * division)
        return ms

    # Re-striking a pitch implicitly ends the previous note; a note-off ends
    # the most recent one. This matters for the repeated piano-roll notes.
    notes = []
    active = defaultdict(list)
    for tick, is_on, pitch, _ in sorted(
        note_events, key=lambda event: (event[0], not event[1])
    ):
        if is_on:
            if active[pitch]:
                start = active[pitch].pop(0)
                notes.append((start, tick, pitch))
            active[pitch].append(tick)
        elif active[pitch]:
            start = active[pitch].pop()
            notes.append((start, tick, pitch))
    for pitch, starts in active.items():
        for start in starts:
            notes.append((start, max_tick, pitch))

    song_ms = tick_to_ms(max_tick)
    note_times = [
        (tick_to_ms(start), tick_to_ms(end), pitch) for start, end, pitch in notes
    ]

    # Sample the highest sounding pitch on a fixed grid. We only need the
    # expiry and pitch in the active heap, so this stays cheap even though the
    # MIDI has thousands of overlapping piano notes.
    note_times.sort()
    active_notes = []
    note_index = 0
    segments = []
    current_pitch = 0
    segment_start = 0.0

    for sample in range(0, int(song_ms) + SAMPLE_MS, SAMPLE_MS):
        now_ms = min(sample, song_ms)
        while note_index < len(note_times) and note_times[note_index][0] <= now_ms:
            start, end, pitch = note_times[note_index]
            heapq.heappush(active_notes, (end, pitch))
            note_index += 1
        while active_notes and active_notes[0][0] <= now_ms:
            heapq.heappop(active_notes)

        pitch = max(item[1] for item in active_notes) if active_notes else 0
        if pitch != current_pitch:
            if current_pitch:
                segments.append((segment_start, now_ms, current_pitch))
            current_pitch = pitch
            segment_start = now_ms

    if current_pitch:
        segments.append((segment_start, song_ms, current_pitch))

    return segments, song_ms


def write_score(segments, song_ms, output):
    notes = []
    for start, end, pitch in segments:
        duration = end - start
        if duration < MIN_NOTE_MS:
            continue
        freq = round(440.0 * 2.0 ** ((pitch - 69) / 12.0))
        notes.append((round(start), freq, round(duration)))

    with Path(output).open("wb") as out:
        out.write(b"BAMU")
        out.write(struct.pack("<III", 1, len(notes), round(song_ms)))
        for start, freq, duration in notes:
            out.write(struct.pack("<IHH", start, freq, duration))

    return notes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--midi", type=Path, default=DEFAULT_MIDI)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--channel", type=int, default=0)
    args = parser.parse_args()

    division, tempo_events, note_events, max_tick = parse_midi(args.midi)
    segments, song_ms = build_notes(
        division, tempo_events, note_events, max_tick, args.channel
    )
    notes = write_score(segments, song_ms, args.output)

    used_events = sum(1 for event in note_events if event[3] == args.channel)
    print(f"Parsed {used_events} MIDI note events, {song_ms / 1000:.2f}s")
    print(
        f"Wrote {args.output} with {len(notes)} notes "
        f"({args.output.stat().st_size} bytes)"
    )
    if notes:
        print(
            f"Frequency range {min(n[1] for n in notes)}-"
            f"{max(n[1] for n in notes)} Hz"
        )


if __name__ == "__main__":
    main()
