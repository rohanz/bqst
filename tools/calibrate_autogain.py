#!/usr/bin/env python3

import math
from dataclasses import dataclass


sample_rate = 48000.0
duration_seconds = 3.0
num_samples = int(sample_rate * duration_seconds)
drive_scale = 0.40


@dataclass
class Biquad:
    b0: float
    b1: float
    b2: float
    a1: float
    a2: float
    z1: float = 0.0
    z2: float = 0.0

    def process(self, x):
        y = self.b0 * x + self.z1
        self.z1 = self.b1 * x - self.a1 * y + self.z2
        self.z2 = self.b2 * x - self.a2 * y
        return y


def db_to_gain(db):
    return 10.0 ** (db / 20.0)


def make_low_shelf(freq, q, gain):
    a = math.sqrt(gain)
    w0 = 2.0 * math.pi * freq / sample_rate
    cos_w0 = math.cos(w0)
    sin_w0 = math.sin(w0)
    alpha = sin_w0 / (2.0 * q)
    beta = 2.0 * math.sqrt(a) * alpha

    b0 = a * ((a + 1.0) - (a - 1.0) * cos_w0 + beta)
    b1 = 2.0 * a * ((a - 1.0) - (a + 1.0) * cos_w0)
    b2 = a * ((a + 1.0) - (a - 1.0) * cos_w0 - beta)
    a0 = (a + 1.0) + (a - 1.0) * cos_w0 + beta
    a1 = -2.0 * ((a - 1.0) + (a + 1.0) * cos_w0)
    a2 = (a + 1.0) + (a - 1.0) * cos_w0 - beta
    return Biquad(b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0)


def make_high_shelf(freq, q, gain):
    a = math.sqrt(gain)
    w0 = 2.0 * math.pi * freq / sample_rate
    cos_w0 = math.cos(w0)
    sin_w0 = math.sin(w0)
    alpha = sin_w0 / (2.0 * q)
    beta = 2.0 * math.sqrt(a) * alpha

    b0 = a * ((a + 1.0) + (a - 1.0) * cos_w0 + beta)
    b1 = -2.0 * a * ((a - 1.0) + (a + 1.0) * cos_w0)
    b2 = a * ((a + 1.0) + (a - 1.0) * cos_w0 - beta)
    a0 = (a + 1.0) - (a - 1.0) * cos_w0 + beta
    a1 = 2.0 * ((a - 1.0) - (a + 1.0) * cos_w0)
    a2 = (a + 1.0) - (a - 1.0) * cos_w0 - beta
    return Biquad(b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0)


def make_peak(freq, q, gain):
    a = math.sqrt(gain)
    w0 = 2.0 * math.pi * freq / sample_rate
    cos_w0 = math.cos(w0)
    sin_w0 = math.sin(w0)
    alpha = sin_w0 / (2.0 * q)

    b0 = 1.0 + alpha * a
    b1 = -2.0 * cos_w0
    b2 = 1.0 - alpha * a
    a0 = 1.0 + alpha / a
    a1 = -2.0 * cos_w0
    a2 = 1.0 - alpha / a
    return Biquad(b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0)


def density_saturate(sample, drive01):
    if drive01 <= 0.0:
        return sample

    push = drive01 * drive01
    max_push = push * drive01
    asymmetry = drive01 * (0.016 + drive01 * 0.045 + push * 0.040)
    odd_weight = drive01 * (0.032 + drive01 * 0.095 + push * 0.115 + max_push * 0.135)
    soft_knee = 0.80 + drive01 * 0.42 + push * 0.36 + max_push * 0.60
    driven = sample * soft_knee + odd_weight * sample * sample * sample + asymmetry
    shaped = (math.tanh(driven) - math.tanh(asymmetry)) * (1.0 + 0.07 * drive01 + 0.13 * max_push)
    blend = drive01 * 0.39 + push * 0.16 + max_push * 0.15
    return sample * (1.0 - blend) + shaped * blend


def transformer_saturate(sample, drive01):
    if drive01 <= 0.0:
        return sample

    push = drive01 * drive01
    max_push = push * drive01
    drive = 0.92 + drive01 * 1.55 + push * 0.82 + max_push * 1.15
    bias = 0.018 * drive01 + push * 0.010 + max_push * 0.018
    tanh_norm = math.tanh(0.86)
    biased = sample * drive + bias
    shaped = math.tanh(biased * 0.86) / tanh_norm - math.tanh(bias * 0.86) / tanh_norm
    rounded = shaped - (0.025 * drive01 + 0.014 * push + 0.020 * max_push) * shaped * shaped * shaped
    blend = drive01 * 0.43 + push * 0.12 + max_push * 0.14
    return sample * (1.0 - blend) + rounded * blend


def process_saturation(samples, drive01, sat_type):
    pre_low = make_low_shelf(95.0, 0.55, db_to_gain(-2.2))
    post_low = make_low_shelf(95.0, 0.55, db_to_gain(2.2))

    if sat_type == "cream":
        body_tone = make_peak(720.0, 0.52, db_to_gain(0.85))
        pre_tone = make_high_shelf(6200.0, 0.55, db_to_gain(-0.65))
        post_tone = make_high_shelf(7000.0, 0.50, db_to_gain(-0.55))
        shape = density_saturate
    else:
        low_drive = make_low_shelf(165.0, 0.62, db_to_gain(1.10))
        pre_tone = make_peak(245.0, 0.72, db_to_gain(0.55))
        low_restore = make_low_shelf(165.0, 0.62, db_to_gain(-0.70))
        post_tone = make_high_shelf(7800.0, 0.50, db_to_gain(-0.75))
        shape = transformer_saturate

    gain = db_to_gain(drive01 * 18.0 * drive_scale)
    out = []
    for sample in samples:
        value = pre_low.process(sample)
        if sat_type == "cream":
            value = body_tone.process(value)
        else:
            value = low_drive.process(value)
        value = pre_tone.process(value)
        value = shape(value * gain, drive01)
        if sat_type == "grit":
            value = low_restore.process(value)
        value = post_tone.process(value)
        value = post_low.process(value)
        out.append(value)
    return out


def rms_db(samples):
    start = int(0.25 * sample_rate)
    usable = samples[start:]
    mean_square = sum(x * x for x in usable) / max(1, len(usable))
    return 10.0 * math.log10(max(mean_square, 1.0e-16))


def crest_db(samples):
    rms = db_to_gain(rms_db(samples))
    peak = max(abs(x) for x in samples)
    return 20.0 * math.log10(max(peak, 1.0e-12) / max(rms, 1.0e-12))


def normalize_peak(samples, peak_db):
    peak = max(abs(x) for x in samples)
    if peak <= 0.0:
        return samples
    gain = db_to_gain(peak_db) / peak
    return [x * gain for x in samples]


def sine(freq):
    return [math.sin(2.0 * math.pi * freq * i / sample_rate) for i in range(num_samples)]


def bass_808():
    out = []
    phase = 0.0
    for i in range(num_samples):
        t = i / sample_rate
        local = t % 0.75
        freq = 42.0 + 34.0 * math.exp(-local * 11.0)
        phase += 2.0 * math.pi * freq / sample_rate
        env = math.exp(-local * 4.0)
        out.append(math.sin(phase) * env)
    return out


def bassline():
    notes = [55.0, 73.42, 49.0, 65.41, 41.2, 55.0, 82.41, 61.74]
    out = []
    for i in range(num_samples):
        t = i / sample_rate
        step = int((t * 4.0) % len(notes))
        local = (t * 4.0) % 1.0
        env = min(1.0, local * 20.0) * math.exp(-local * 1.2)
        f = notes[step]
        saw = 2.0 * ((t * f) % 1.0) - 1.0
        sub = math.sin(2.0 * math.pi * f * 0.5 * t)
        out.append((0.58 * saw + 0.55 * sub) * env)
    return out


def drums():
    out = []
    for i in range(num_samples):
        t = i / sample_rate
        beat = t % 0.5
        kick = math.sin(2.0 * math.pi * (52.0 + 80.0 * math.exp(-beat * 55.0)) * t) * math.exp(-beat * 28.0)
        snare_phase = (t - 0.25) % 0.5
        snare_noise = math.sin(2.0 * math.pi * 1737.0 * t) * math.sin(2.0 * math.pi * 3181.0 * t)
        snare = snare_noise * math.exp(-snare_phase * 34.0) if snare_phase < 0.18 else 0.0
        hat_phase = t % 0.125
        hat = math.sin(2.0 * math.pi * 7600.0 * t) * math.exp(-hat_phase * 95.0)
        out.append(0.95 * kick + 0.25 * snare + 0.12 * hat)
    return out


def dense_master():
    out = []
    for i in range(num_samples):
        t = i / sample_rate
        low = 0.45 * math.sin(2.0 * math.pi * 58.0 * t)
        low += 0.26 * math.sin(2.0 * math.pi * 116.0 * t + 0.4)
        mid = 0.18 * math.sin(2.0 * math.pi * 440.0 * t)
        mid += 0.13 * math.sin(2.0 * math.pi * 880.0 * t + 1.1)
        air = 0.06 * math.sin(2.0 * math.pi * 6200.0 * t)
        groove = 0.82 + 0.18 * math.sin(2.0 * math.pi * 2.0 * t)
        out.append((low + mid + air) * groove)
    return out


materials = {
    "1k sine": normalize_peak(sine(1000.0), -12.0),
    "bass 808": normalize_peak(bass_808(), -1.0),
    "bassline": normalize_peak(bassline(), -3.0),
    "drum bus": normalize_peak(drums(), -1.0),
    "dense master": normalize_peak(dense_master(), -1.0),
}


drives_db = [0.5, 1.0, 2.0, 3.0, 6.0, 9.0, 12.0, 15.0, 18.0]


def target_table(sat_type):
    rows = []
    for drive_db in drives_db:
        drive01 = drive_db / 18.0
        targets = []
        for name, samples in materials.items():
            dry = rms_db(samples)
            wet = rms_db(process_saturation(samples, drive01, sat_type))
            target = dry - wet
            targets.append((name, target))
        rows.append((drive_db, targets))
    return rows


def model_gain_db(drive_db, amount, exponent):
    drive01 = drive_db / 18.0
    gain = 1.0 / (1.0 + (drive01 ** exponent) * amount)
    return 20.0 * math.log10(gain)


def fit_curve(rows):
    data = []
    for drive_db, targets in rows:
        for _, target in targets:
            data.append((drive_db, target))

    best = None
    for amount_i in range(40, 401):
        amount = amount_i / 100.0
        for exp_i in range(60, 251):
            exponent = exp_i / 100.0
            error = 0.0
            for drive_db, target in data:
                predicted = model_gain_db(drive_db, amount, exponent)
                error += (predicted - target) ** 2
            score = error / len(data)
            if best is None or score < best[0]:
                best = (score, amount, exponent)
    return best


def print_report(sat_type):
    rows = target_table(sat_type)
    score, amount, exponent = fit_curve(rows)
    print(f"\n{sat_type.upper()} autogain fit: amount={amount:.2f}, exponent={exponent:.2f}, rmse={math.sqrt(score):.2f} dB")
    print("drive | model | avg target | spread | material targets")
    for drive_db, targets in rows:
        values = [target for _, target in targets]
        avg = sum(values) / len(values)
        spread = max(values) - min(values)
        details = ", ".join(f"{name} {target:+.1f}" for name, target in targets)
        print(f"{drive_db:>4.1f} | {model_gain_db(drive_db, amount, exponent):>+5.1f} | {avg:>+6.1f} | {spread:>4.1f} | {details}")

    print("\nPrevious-model comparison:")
    previous_amount = 2.05 if sat_type == "cream" else 2.20
    previous_exponent = 1.80 if sat_type == "cream" else 1.21
    for drive_db, targets in rows:
        avg = sum(target for _, target in targets) / len(targets)
        previous = model_gain_db(drive_db, previous_amount, previous_exponent)
        fitted = model_gain_db(drive_db, amount, exponent)
        print(f"{drive_db:>4.1f} dB: previous {previous:+5.1f} dB, fitted {fitted:+5.1f} dB, avg target {avg:+5.1f} dB")


if __name__ == "__main__":
    print("Material levels:")
    for name, samples in materials.items():
        print(f"{name:>12}: rms {rms_db(samples):>5.1f} dBFS, crest {crest_db(samples):>4.1f} dB")

    print_report("cream")
    print_report("grit")
