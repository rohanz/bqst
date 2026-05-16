#!/usr/bin/env python3
"""Measure the downloaded Bereich03 Density reference files.

This uses only the Python standard library so it can run on a clean macOS
machine. It is intentionally simple: the goal is to track level, crest factor,
and residual energy for the OFF/ON examples while tuning BQST's Cream mode.
"""

from __future__ import annotations

import math
import struct
import sys
import wave
from pathlib import Path


def read_wav_mono(path: Path) -> tuple[int, list[float]]:
    with wave.open(str(path), "rb") as wav:
        channels = wav.getnchannels()
        sample_width = wav.getsampwidth()
        sample_rate = wav.getframerate()
        frames = wav.readframes(wav.getnframes())

    values: list[float] = []
    if sample_width == 3:
        for index in range(0, len(frames), 3):
            chunk = frames[index : index + 3]
            sign = b"\xff" if chunk[2] & 0x80 else b"\x00"
            values.append(int.from_bytes(chunk + sign, "little", signed=True) / 8388608.0)
    elif sample_width == 2:
        values = [value / 32768.0 for (value,) in struct.iter_unpack("<h", frames)]
    else:
        raise ValueError(f"unsupported sample width: {sample_width}")

    mono = [sum(values[index : index + channels]) / channels for index in range(0, len(values), channels)]
    return sample_rate, mono


def db(value: float) -> float:
    return 20.0 * math.log10(max(value, 1.0e-20))


def metrics(samples: list[float]) -> dict[str, float]:
    peak = max(abs(sample) for sample in samples)
    rms = math.sqrt(sum(sample * sample for sample in samples) / len(samples))
    return {
        "peak_dbfs": db(peak),
        "rms_dbfs": db(rms),
        "crest_db": db(peak / rms),
        "dc": sum(samples) / len(samples),
    }


def residual_metrics(dry: list[float], wet: list[float]) -> dict[str, float]:
    length = min(len(dry), len(wet))
    residual = [wet[index] - dry[index] for index in range(length)]
    residual_rms = math.sqrt(sum(sample * sample for sample in residual) / length)
    dry_rms = math.sqrt(sum(sample * sample for sample in dry[:length]) / length)
    return {
        "residual_dbfs": db(residual_rms),
        "residual_vs_dry_db": db(residual_rms / dry_rms),
    }


def main() -> int:
    reference_dir = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("references/density")
    files = [
        ("off", reference_dir / "density-off.wav"),
        ("density 3", reference_dir / "density-3.wav"),
        ("density 3 boom+vint", reference_dir / "density-3-boom-vint.wav"),
    ]

    loaded: dict[str, list[float]] = {}
    print("file,sample_rate,peak_dbfs,rms_dbfs,crest_db,dc")
    for label, path in files:
        sample_rate, samples = read_wav_mono(path)
        loaded[label] = samples
        values = metrics(samples)
        print(
            f"{label},{sample_rate},{values['peak_dbfs']:.2f},{values['rms_dbfs']:.2f},"
            f"{values['crest_db']:.2f},{values['dc']:+.6f}"
        )

    dry = loaded["off"]
    print("\ncomparison,residual_dbfs,residual_vs_dry_db")
    for label in ("density 3", "density 3 boom+vint"):
        values = residual_metrics(dry, loaded[label])
        print(f"{label},{values['residual_dbfs']:.2f},{values['residual_vs_dry_db']:.2f}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
