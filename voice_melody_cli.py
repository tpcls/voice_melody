#!/usr/bin/env python3
"""Convert a monophonic vocal WAV recording into a MIDI melody file.

This tool intentionally uses only the Python standard library so it can run in
places where the JUCE plugin dependencies are unavailable.
"""

from __future__ import annotations

import argparse
import importlib
import importlib.util
import math
import queue
import struct
import sys
import wave
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Optional, Sequence

MIN_FREQUENCY_HZ = 70.0
MAX_FREQUENCY_HZ = 1000.0
YIN_THRESHOLD = 0.16
DEFAULT_TICKS_PER_QUARTER = 480
DEFAULT_TEMPO_BPM = 120
NOTE_NAMES = ("C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B")


@dataclass(frozen=True)
class PitchResult:
    frequency_hz: float
    confidence: float
    rms: float


@dataclass(frozen=True)
class NoteEvent:
    start_frame: int
    end_frame: int
    note: int
    velocity: int


def read_wav_mono(path: Path) -> tuple[int, list[float]]:
    with wave.open(str(path), "rb") as wav:
        channels = wav.getnchannels()
        sample_width = wav.getsampwidth()
        sample_rate = wav.getframerate()
        frame_count = wav.getnframes()
        raw = wav.readframes(frame_count)

    if channels < 1:
        raise ValueError("WAV file must have at least one channel")
    if sample_width not in (1, 2, 3, 4):
        raise ValueError(f"Unsupported WAV sample width: {sample_width} bytes")

    samples: list[float] = []
    stride = channels * sample_width
    max_int = float(1 << (sample_width * 8 - 1))

    for frame_offset in range(0, len(raw), stride):
        mono = 0.0
        for channel in range(channels):
            offset = frame_offset + channel * sample_width
            chunk = raw[offset : offset + sample_width]
            if sample_width == 1:
                value = chunk[0] - 128
                scale = 128.0
            elif sample_width == 2:
                value = struct.unpack_from("<h", chunk)[0]
                scale = max_int
            elif sample_width == 3:
                sign = b"\xff" if chunk[2] & 0x80 else b"\x00"
                value = int.from_bytes(chunk + sign, "little", signed=True)
                scale = max_int
            else:
                value = struct.unpack_from("<i", chunk)[0]
                scale = max_int
            mono += value / scale
        samples.append(mono / channels)

    return sample_rate, samples


def detect_pitch_yin(window: list[float], sample_rate: int) -> Optional[PitchResult]:
    if not window:
        return None

    rms = math.sqrt(sum(sample * sample for sample in window) / len(window))
    if rms <= 0.0:
        return None

    max_tau = min(len(window) // 2 - 1, int(sample_rate / MIN_FREQUENCY_HZ))
    min_tau = max(2, int(sample_rate / MAX_FREQUENCY_HZ))
    if max_tau <= min_tau:
        return None

    compare_length = len(window) - max_tau
    difference = [0.0] * (max_tau + 1)
    for tau in range(1, max_tau + 1):
        total = 0.0
        for index in range(compare_length):
            delta = window[index] - window[index + tau]
            total += delta * delta
        difference[tau] = total

    cumulative = [1.0] * (max_tau + 1)
    running_sum = 0.0
    for tau in range(1, max_tau + 1):
        running_sum += difference[tau]
        cumulative[tau] = difference[tau] * tau / running_sum if running_sum > 0.0 else 1.0

    tau_estimate = -1
    for tau in range(min_tau, max_tau + 1):
        if cumulative[tau] < YIN_THRESHOLD:
            while tau + 1 <= max_tau and cumulative[tau + 1] < cumulative[tau]:
                tau += 1
            tau_estimate = tau
            break

    if tau_estimate < 0:
        return None

    better_tau = float(tau_estimate)
    if 0 < tau_estimate < max_tau:
        left = cumulative[tau_estimate - 1]
        centre = cumulative[tau_estimate]
        right = cumulative[tau_estimate + 1]
        divisor = 2.0 * (2.0 * centre - left - right)
        if abs(divisor) > 1.0e-9:
            better_tau += (right - left) / divisor

    frequency = sample_rate / better_tau
    if frequency < MIN_FREQUENCY_HZ or frequency > MAX_FREQUENCY_HZ:
        return None

    return PitchResult(frequency_hz=frequency, confidence=max(0.0, min(1.0, 1.0 - cumulative[tau_estimate])), rms=rms)


def frequency_to_midi_note(frequency_hz: float) -> int:
    return round(69 + 12 * math.log2(frequency_hz / 440.0))


def midi_note_name(note: int) -> str:
    octave = note // 12 - 1
    return f"{NOTE_NAMES[note % 12]}{octave}"


def render_note_ladder(note: int, low_note: int = 48, high_note: int = 84, width: int = 37) -> str:
    low_note = max(0, min(127, low_note))
    high_note = max(low_note + 1, min(127, high_note))
    note = max(low_note, min(high_note, note))
    position = round((note - low_note) / (high_note - low_note) * (width - 1))
    cells = ["─"] * width
    cells[position] = "●"
    return f"{midi_note_name(low_note):>3} |{''.join(cells)}| {midi_note_name(high_note):<3}"


def render_note_timeline(notes: list[NoteEvent], sample_rate: int, width: int = 64) -> str:
    if not notes:
        return "No notes detected."

    end_frame = max(note.end_frame for note in notes)
    if end_frame <= 0:
        return "No notes detected."

    lines = ["Detected melody UI:"]
    for note in notes:
        start_column = round(note.start_frame / end_frame * (width - 1))
        end_column = max(start_column + 1, round(note.end_frame / end_frame * width))
        bar = [" "] * width
        for index in range(start_column, min(width, end_column)):
            bar[index] = "█"
        start_seconds = note.start_frame / sample_rate
        end_seconds = note.end_frame / sample_rate
        lines.append(f"{midi_note_name(note.note):>4} │{''.join(bar)}│ {start_seconds:6.2f}s–{end_seconds:6.2f}s")
    return "\n".join(lines)


def choose_stable_note(next_note: int, stable_note: int, stable_count: int, accepted_note: int, smoothing: int) -> tuple[int, int, int]:
    if next_note == stable_note:
        stable_count += 1
    else:
        stable_note = next_note
        stable_count = 1

    if stable_count >= smoothing:
        accepted_note = stable_note

    return stable_note, stable_count, accepted_note


def pitch_result_to_note(result: Optional[PitchResult], gate_threshold: float, confidence_threshold: float, transpose: int) -> int:
    if result and result.rms >= gate_threshold and result.confidence >= confidence_threshold:
        return max(0, min(127, frequency_to_midi_note(result.frequency_hz) + transpose))
    return -1


def samples_to_notes(
    samples: list[float],
    sample_rate: int,
    *,
    frame_size: int,
    hop_size: int,
    gate_threshold: float,
    confidence_threshold: float,
    transpose: int,
    velocity: int,
    smoothing: int,
    min_note_ms: float,
) -> list[NoteEvent]:
    notes: list[NoteEvent] = []
    active_note = -1
    active_start = 0
    stable_note = -1
    stable_count = 0
    accepted_note = -1

    def close_active(end_frame: int) -> None:
        nonlocal active_note, active_start
        min_frames = int(sample_rate * min_note_ms / 1000.0)
        if active_note >= 0 and end_frame - active_start >= min_frames:
            notes.append(NoteEvent(active_start, end_frame, active_note, velocity))
        active_note = -1

    for start in range(0, max(0, len(samples) - frame_size + 1), hop_size):
        window = samples[start : start + frame_size]
        result = detect_pitch_yin(window, sample_rate)
        next_note = pitch_result_to_note(result, gate_threshold, confidence_threshold, transpose)
        stable_note, stable_count, accepted_note = choose_stable_note(
            next_note, stable_note, stable_count, accepted_note, smoothing
        )

        if accepted_note != active_note:
            close_active(start)
            if accepted_note >= 0:
                active_note = accepted_note
                active_start = start

    close_active(len(samples))
    return notes


def variable_length_quantity(value: int) -> bytes:
    if value < 0:
        raise ValueError("Delta time cannot be negative")
    buffer = value & 0x7F
    value >>= 7
    while value:
        buffer <<= 8
        buffer |= (value & 0x7F) | 0x80
        value >>= 7

    data = bytearray()
    while True:
        data.append(buffer & 0xFF)
        if buffer & 0x80:
            buffer >>= 8
        else:
            break
    return bytes(data)


def midi_event(delta_ticks: int, payload: bytes) -> bytes:
    return variable_length_quantity(delta_ticks) + payload


def write_midi(path: Path, notes: Iterable[NoteEvent], sample_rate: int, tempo_bpm: int) -> None:
    microseconds_per_quarter = round(60_000_000 / tempo_bpm)
    events: list[tuple[int, bytes]] = [(0, b"\xff\x51\x03" + microseconds_per_quarter.to_bytes(3, "big"))]

    for note in notes:
        start_tick = round(note.start_frame / sample_rate * tempo_bpm / 60.0 * DEFAULT_TICKS_PER_QUARTER)
        end_tick = round(note.end_frame / sample_rate * tempo_bpm / 60.0 * DEFAULT_TICKS_PER_QUARTER)
        events.append((start_tick, bytes([0x90, note.note, note.velocity])))
        events.append((max(start_tick + 1, end_tick), bytes([0x80, note.note, 0])))

    events.sort(key=lambda item: (item[0], item[1][0] == 0x90))

    track = bytearray()
    previous_tick = 0
    for tick, payload in events:
        track += midi_event(tick - previous_tick, payload)
        previous_tick = tick
    track += midi_event(0, b"\xff\x2f\x00")

    header = b"MThd" + struct.pack(">IHHH", 6, 0, 1, DEFAULT_TICKS_PER_QUARTER)
    chunk = b"MTrk" + struct.pack(">I", len(track)) + bytes(track)
    path.write_bytes(header + chunk)



def require_sounddevice():
    if importlib.util.find_spec("sounddevice") is None:
        raise RuntimeError(
            "real-time mode requires the optional 'sounddevice' package. "
            "Install it with: python3 -m pip install sounddevice"
        )
    return importlib.import_module("sounddevice")


def audio_block_to_mono(block: Sequence[Sequence[float]]) -> list[float]:
    mono: list[float] = []
    for frame in block:
        if isinstance(frame, (int, float)):
            mono.append(float(frame))
        else:
            values = [float(value) for value in frame]
            mono.append(sum(values) / len(values) if values else 0.0)
    return mono


def run_realtime(args: argparse.Namespace) -> int:
    sounddevice = require_sounddevice()
    sample_rate = int(args.sample_rate or sounddevice.query_devices(kind="input")["default_samplerate"])
    audio_queue: queue.Queue[list[float]] = queue.Queue()
    notes: list[NoteEvent] = []
    rolling_window: list[float] = []
    processed_since_analysis = 0
    processed_frames = 0
    active_note = -1
    active_start = 0
    stable_note = -1
    stable_count = 0
    accepted_note = -1

    def close_active(end_frame: int) -> None:
        nonlocal active_note, active_start
        min_frames = int(sample_rate * args.min_note_ms / 1000.0)
        if active_note >= 0 and end_frame - active_start >= min_frames:
            notes.append(NoteEvent(active_start, end_frame, active_note, args.velocity))
        active_note = -1

    def callback(indata, frames, time_info, status) -> None:  # noqa: ANN001 - sounddevice callback types are runtime-provided.
        if status and args.print_events:
            print(f"audio status: {status}", file=sys.stderr)
        audio_queue.put(audio_block_to_mono(indata.tolist()))

    print("Listening... press Ctrl+C to stop and write MIDI.")
    with sounddevice.InputStream(
        samplerate=sample_rate,
        channels=args.channels,
        blocksize=args.block_size,
        device=args.device,
        callback=callback,
    ):
        try:
            while True:
                block = audio_queue.get()
                rolling_window.extend(block)
                processed_frames += len(block)
                processed_since_analysis += len(block)

                if len(rolling_window) > args.frame_size:
                    del rolling_window[: len(rolling_window) - args.frame_size]

                while len(rolling_window) >= args.frame_size and processed_since_analysis >= args.hop_size:
                    processed_since_analysis -= args.hop_size
                    result = detect_pitch_yin(rolling_window[-args.frame_size :], sample_rate)
                    next_note = pitch_result_to_note(result, args.gate_threshold, args.confidence, args.transpose)
                    stable_note, stable_count, accepted_note = choose_stable_note(
                        next_note, stable_note, stable_count, accepted_note, args.smoothing
                    )

                    event_frame = max(0, processed_frames - len(rolling_window))
                    if accepted_note != active_note:
                        close_active(event_frame)
                        if accepted_note >= 0:
                            active_note = accepted_note
                            active_start = event_frame
                            if args.print_events or args.show_ui:
                                freq_text = f" {result.frequency_hz:.1f} Hz" if result else ""
                                if args.show_ui:
                                    print(f"\n{midi_note_name(active_note):>4}{freq_text}  {render_note_ladder(active_note)}")
                                else:
                                    print(f"note_on  {active_note:3d}{freq_text}")
                        elif args.print_events or args.show_ui:
                            print("note_off")
        except KeyboardInterrupt:
            pass

    close_active(processed_frames)
    write_midi(args.output_midi, notes, sample_rate, args.tempo)
    if args.show_ui:
        print(render_note_timeline(notes, sample_rate))
    print(f"Wrote {len(notes)} realtime note(s) to {args.output_midi}")
    return 0

def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Convert a monophonic voice WAV file to a MIDI melody.")
    parser.add_argument("input_wav", type=Path, nargs="?", help="Input mono/stereo PCM WAV file")
    parser.add_argument("output_midi", type=Path, help="Output .mid file")
    parser.add_argument("--realtime", action="store_true", help="Listen to a microphone in real time and write MIDI when stopped")
    parser.add_argument("--frame-size", type=int, default=2048, help="Analysis window size in samples")
    parser.add_argument("--hop-size", type=int, default=256, help="Analysis hop size in samples")
    parser.add_argument("--gate-threshold", type=float, default=0.012, help="Minimum RMS required to emit a note")
    parser.add_argument("--confidence", type=float, default=0.72, help="Minimum YIN confidence required to emit a note")
    parser.add_argument("--transpose", type=int, default=0, help="Transpose emitted MIDI notes by semitones")
    parser.add_argument("--velocity", type=int, default=96, help="MIDI note velocity, 1-127")
    parser.add_argument("--smoothing", type=int, default=3, help="Consecutive frames required before note changes")
    parser.add_argument("--min-note-ms", type=float, default=35.0, help="Drop detected notes shorter than this length")
    parser.add_argument("--tempo", type=int, default=DEFAULT_TEMPO_BPM, help="Tempo written to the MIDI file")
    parser.add_argument("--sample-rate", type=int, default=None, help="Realtime input sample rate; defaults to the input device rate")
    parser.add_argument("--block-size", type=int, default=256, help="Realtime audio callback block size")
    parser.add_argument("--channels", type=int, default=1, help="Realtime input channel count")
    parser.add_argument("--device", default=None, help="Realtime input device name or index passed to sounddevice")
    parser.add_argument("--print-events", action="store_true", help="Print realtime note on/off events while listening")
    parser.add_argument("--show-ui", action="store_true", help="Show note names and an ASCII pitch/timeline UI instead of only numeric output")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    if args.frame_size <= 0 or args.hop_size <= 0:
        raise ValueError("frame-size and hop-size must be positive")
    if not 1 <= args.velocity <= 127:
        raise ValueError("velocity must be between 1 and 127")
    if args.smoothing <= 0:
        raise ValueError("smoothing must be positive")
    if args.tempo <= 0:
        raise ValueError("tempo must be positive")

    if args.realtime:
        return run_realtime(args)
    if args.input_wav is None:
        raise ValueError("input_wav is required unless --realtime is used")

    sample_rate, samples = read_wav_mono(args.input_wav)
    notes = samples_to_notes(
        samples,
        sample_rate,
        frame_size=args.frame_size,
        hop_size=args.hop_size,
        gate_threshold=args.gate_threshold,
        confidence_threshold=args.confidence,
        transpose=args.transpose,
        velocity=args.velocity,
        smoothing=args.smoothing,
        min_note_ms=args.min_note_ms,
    )
    write_midi(args.output_midi, notes, sample_rate, args.tempo)
    if args.show_ui:
        print(render_note_timeline(notes, sample_rate))
    print(f"Wrote {len(notes)} note(s) to {args.output_midi}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except Exception as exc:  # CLI boundary: print friendly errors instead of tracebacks.
        print(f"voice_melody_cli: {exc}", file=sys.stderr)
        raise SystemExit(1)
