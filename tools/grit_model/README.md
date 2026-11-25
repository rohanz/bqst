# Grit capture model

BQST has one Grit model: the independent-path blend formerly called Hybrid.
It combines original Grit with a model of the full engaged path of a hardware unit, not a physically
identified output transformer or an endorsed emulation. The separate Lab audition
build and host model selector have been removed.

## Independent paths (current)

For auditions: Grit, Mix 100%, Vintage off and Autogain on.

Grit blends **75% complete Legacy + 25% complete Captured**:

- Legacy runs at the original knob value, with its original pre-drive, filter colour
  ramp, waveshaper, Vintage path and DC removal. Its Drive mapping is unchanged.
- Only Captured is remapped. New 9/12/15/18 corresponds to its own 15/16/17/18 settings.
  Drive 0–1 is unchanged, with a smooth cubic transition into the upper range.
- Captured has separate Vintage filter state. Its output is nominally aligned using
  capturedAutogain(remapped knob) / legacyAutogain(original knob), before mixing.
  This keeps the two different gain laws from making the 25% branch negligible.
- A newly measured static Autogain table compensates the blend at the original knob
  position. It is not remapped again and is not a live loudness detector.

`hybrid_independent_validation.json` records historical validation that Hybrid equals the weighted outputs
of standalone Legacy and Captured, with Vintage both off and on. Low-frequency tests
at 40/80/160 Hz, input -30/-12 dBFS and Drive 9/12/18 found at most 0.043 dB fundamental
loss from phase differences relative to a perfectly aligned magnitude sum. This is a
bounded test result, not proof for all levels/frequencies. The new blend is an intentional
sound design, not a physically identified circuit simulation.

`hybrid_calibration.json` revision 2 records the new five-material compensation sweep.
Maximum compensation is -10.65 dB; static matching still varies across source material.
Regeneration requires the current VST3, with its Drive mappings intact; Autogain is
turned off while measuring, so no bootstrap or identity Drive tables are required:

```
cmake --build /private/tmp/bqst-grit-check --target BQST_VST3 -j 8
uv run python tools/grit_model/calibrate_hybrid.py "/private/tmp/bqst-grit-check/BQST_artefacts/Release/VST3/BQST.vst3"
cmake --build /private/tmp/bqst-grit-check --target BQST_VST3 BqstGritLabTests BqstChainTests -j 8
ctest --test-dir /private/tmp/bqst-grit-check --output-on-failure
```

`--export-existing` regenerates the header from the saved revision-2 report.
The older `hybrid_validation.json` and `hybrid_spread_validation.json` describe
superseded auditions: the mixed-core model and the whole-Hybrid remapping respectively.
Neither describes the current independent-path blend.

## Captured controls and calibration

The captured branch uses a generated
perceptual taper instead of directly mapping to makeup knob positions. `calibrate.py`
uses the same nonlinear-energy metric as Cream: output-power-weighted 1-coherence with
4096-point Welch windows. The average across synthetic 808, bassline, drum-bus and dense
master signals rises linearly in dB from -40 dB at Drive 1 to the maximum measured model
at Drive 18. Below 1, the underlying reference control ramps continuously from zero.
The measured curve's monotonic envelope is inverted on a 0.25 dB knob grid.
This is a calibration across a defined material set, not a guarantee of equal perceived
saturation for all music or identical saturation to Cream at the same knob setting.

Approximate measured landmarks on the new taper:

| Drive | Capture | Internal makeup |
|---|---|---|
| 0 dB | Exact bypass | 0 dB |
| 6.33 dB | Minimum makeup | 0 dB |
| 9.33 dB | 9 o’clock | +1.65 dB |
| 16.10 dB | Noon | +9.85 dB |
| 18 dB | 3 o’clock | +17.84 dB |

The core still divides its output by makeup gain. Its branch-alignment compensation uses a static
table, measured at 0.5 dB knob intervals with integrated LUFS across
five synthetic sources: sine, 808, bassline, drums and dense master. Median loudness
change is targeted at 0 LU. Compensation is exactly 0 dB at zero drive and reaches
+10.65 dB at maximum drive. This table and the original Grit compensation formula are both required by
`GritHybrid::capturedAlignment`, even when final blend Autogain is off. Neither is dead
standalone-model data. The blend has its own compensation table; Output trim remains final.

Historically, the Captured-only VST3 was measured at 4x oversampling, 48 kHz, nine Drive positions:
musical sources are within approximately -1.36 to +0.78 LU, and the sine reaches
+4.82 LU at maximum Drive. Static compensation cannot match all sources simultaneously.
`calibration.json` and `calibrated_validation.json` include the full per-material spread.
Vintage intentionally changes tone/loudness and is off during calibration.

## Session and preset compatibility

From 1.3.1, Grit is always the 75/25 blend. The `gritRevision` host choice is removed.
State and preset writers use v3 to mark this sound change. All older files remain readable:
state loading strips obsolete revision nodes, while preset loading ignores unknown IDs.
Pre-1.2.0 projects with Grit and any projects selecting Legacy or Captured now use current
Grit and sound different. Cream, EQ and current-Grit projects are unchanged.

## Signal flow

The Captured branch replaces legacy Grit's pre-drive, guard shelves, tone filters, waveshaper and
shared DC blocker with a stateful fitted path. It uses seven parallel one-pole-filtered
odd shaping curves and four bounded low-frequency nonlinear branches. The curves are
regularised combinations of tanh bases, exported to shared immutable lookup tables.
A shared fitted output knee follows the nonlinear bank, then a 1 Hz output DC blocker
removes residual DC. The makeup gain precedes the bank and is compensated after it. Vintage, dry/wet, output trim,
oversampling, structural fades and parameter smoothing retain their existing roles.
No allocation, locks, string lookups or dynamic coefficient objects on the audio thread.

## Evidence and limits

Captures: 2026-10-08, Apollo Twin X line I/O, 48 kHz / 24 bit; unit at minimum makeup
plus 9, noon and 3 o’clock makeup settings, with compression intended to be inactive. Relay-bypass captures provide the converter/cable baseline. Left/right
recorded channels were identical. The minimum-makeup takes had no full-scale samples.
For the new tone captures, exclude only the two confirmed digitally clipped 3 o’clock
5 kHz steps (-12 and -6 dBFS input). Retain the complete noon take and all other 3 o’clock
tones. For drum validation, exclude the second and third 3 o’clock loop segments because
each contains at least one full-scale sample. Use the corrected noon drum retake.
No full-scale samples and no observed red input light do not establish which analog
stage caused limiting. This fit deliberately retains the observed limiting without
claiming it originates in the transformer.

Fit inputs: complex odd harmonics from steady sections of 40, 80, 160, 400, 1000 and
5000 Hz tones at -30, -24, -18, -12 and -6 dBFS peak. Converter response was estimated
from the bypass sweep. A fitted nuisance delay of 2.65 microseconds was removed; it is
not implemented as plugin latency. `capture_targets.npz` contains the de-embedded
numerical targets, not the user's audio. No private recording paths are shipped.

For the original minimum-makeup fit, the continuous-time fitted fundamental magnitude is within 0.08 dB of these tone
measurements. Median THD-ratio error is about 1.2 dB. Important exception: at 40 Hz,
-6 dBFS, measured THD is about 1.91% and the model is about 0.67%. These are fitting
errors, not independent validation claims. Digital filters, lookup interpolation,
1 Hz DC removal and oversampling introduce additional small differences.

The original minimum-makeup model was checked against independent sweeps, two-tone
and synthetic drum captures: after gain/
time alignment, the model improved residual versus the bypass reference by about
4.7, 2.1 and 2.1 dB respectively. The drum residual remains around -11.6 dB relative
to the recording: the prototype does **not** reproduce full hardware waveforms closely.
Do not market it as an exact match. Tone fitting alone cannot identify hysteresis,
loading, transformer versus amplifier contributions, or the hardware makeup taper.
Further captures would be needed to claim full hardware replication; production compatibility retains this older Grit separately.

### Driven revision

`driven_targets.npz` contains the four sets of de-embedded tone harmonics and explicit
usable masks. `fit_driven.py` estimates makeup from the quiet 1 kHz tones, then fits
one output threshold and knee to retained 160/400/1000 Hz harmonic magnitudes and THD,
after evaluating the original nonlinear bank. `driven_fit.json` records those constants.
The knee reaches its fitting upper bound (100), indicating essentially hard limiting;
it is an empirical description, not identification of a physical component.

Independent C++ measurements at 192 kHz: noon/-6 dBFS input gives 22.67% THD versus
23.58% recorded, with compensated fundamental gain within 0.14 dB. At 3 o’clock/-18 dBFS,
10.05% versus 10.13%, with gain within 0.05 dB. These are fit-landmark checks, not held-out
tests. The Python/C++ fixture differs by less than 5.2e-8 in sample amplitude.

Held-out drum comparison using the rebuilt universal VST3 at 4x oversampling:
all three 9 o’clock loops, all three noon loops, and the first 3 o’clock loop were
retained. After independent gain and fractional-delay alignment, model residuals were
-20.6 to -21.9 dB relative to the captures, versus -12.1 to -14.2 dB for the bypass
baseline (7.6–9.4 dB improvement). Level correction was only +0.02 to +0.10 dB.
`driven_validation.json` records the per-loop values and method. This is a useful
improvement, but remaining waveform error is about 8–9%; it is not an exact clone.
The two clipped 3 o’clock drum segments were excluded from this comparison.

Frequency- and level-dependent tone, harmonic coloration, and instantaneous peak
limiting are represented. The filter states give the model memory, but compressor
attack/release, overload recovery, transformer hysteresis and load dependence have not
been separately identified or fitted. Even harmonics and noise are not reproduced.

## Reproduce

Python dependencies: numpy, scipy, pyloudnorm (calibration), soundfile (capture analysis),
and Pedalboard (built-plugin evaluation). Calibration also requires a C++17 compiler. The existing private research virtualenv can supply them.

```
uv run python tools/grit_model/fit.py tools/grit_model/capture_targets.npz
uv run python tools/grit_model/fit_driven.py
uv run python tools/grit_model/export.py
uv run python tools/grit_model/calibrate.py
uv run python tools/grit_model/export.py
uv run python tools/grit_model/fixture.py
cmake -S . -B /private/tmp/bqst-grit-check -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build /private/tmp/bqst-grit-check --target BqstGritLabTests BqstChainTests -j 3
ctest --test-dir /private/tmp/bqst-grit-check -R 'Bqst(GritLab|Chain)Tests' --output-on-failure
```

The historical Lab names on the captured model, fixture and unit-test target remain
because that branch is still part of Grit. The obsolete model-comparison harness and
Lab installer have been removed. The Python fitting/export/fixture tools remain:
they regenerate the captured branch that current Grit requires.
