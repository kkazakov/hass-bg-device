#!/usr/bin/env python3
"""Generate a 16 kHz mono PCM16 WAV for gateway/firmware tests."""

from __future__ import annotations

import argparse
import math
import struct
import wave


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-o", "--output", default="/tmp/bg-voice-test.wav")
    parser.add_argument("--sample-rate", type=int, default=16000)
    parser.add_argument("--duration", type=float, default=1.0)
    parser.add_argument("--frequency", type=float, default=440.0)
    args = parser.parse_args()

    frames = max(1, int(args.sample_rate * args.duration))
    fade = max(1, int(args.sample_rate * 0.02))
    samples = bytearray()
    for i in range(frames):
        envelope = min(1.0, i / fade, (frames - i) / fade)
        value = int(0.25 * 32767 * envelope * math.sin(2 * math.pi * args.frequency * i / args.sample_rate))
        samples += struct.pack("<h", value)

    with wave.open(args.output, "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(args.sample_rate)
        wav.writeframes(bytes(samples))

    print(f"wrote {args.output}: {frames} frames, {args.sample_rate} Hz, PCM16 mono")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
