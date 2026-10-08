# Experimental Grit / LA500A capture model

The captured model is integrated into BQST and can also be auditioned as **BQST Grit Lab**,
which has a separate plugin identity. Cream and EQ are unchanged. This is an empirical
model of the full engaged LA500A path, not a physically identified output-transformer
model or an endorsed JLM emulation.

## Hybrid audition: independent paths (current)

Since BQST 1.2.0, new production and **BQST Grit Lab** instances default to **Hybrid**.
Existing saved instances retain their revision; select **Hybrid** in the host's **Grit Model**
parameter when needed. Legacy and Captured remain available. For auditions: Grit, Mix 100%,
Vintage off and Autogain on.

The corrected Hybrid blends **75% complete Legacy + 25% complete Captured**:

- Legacy runs at the original knob value, with its original pre-drive, filter colour
  ramp, waveshaper, Vintage path and DC removal. Its Drive mapping is unchanged.
- Only Captured is remapped. New 9/12/15/18 corresponds to its own 15/16/17/18 settings.
  Drive 0–1 is unchanged, with a smooth cubic transition into the upper range.
- Captured has separate Vintage filter state. Its output is nominally aligned using
  capturedAutogain(remapped knob) / legacyAutogain(original knob), before mixing.
  This keeps the two different gain laws from making the 25% branch negligible.
- A newly measured static Autogain table compensates the blend at the original knob
  position. It is not remapped again and is not a live loudness detector.

`hybrid_independent_validation.json` verifies that Hybrid equals the weighted outputs
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
python tools/grit_model/calibrate_hybrid.py "/private/tmp/bqst-grit-check/BQST_artefacts/Release/VST3/BQST Grit Lab.vst3"
cmake --build /private/tmp/bqst-grit-check --target BQST_VST3 BqstGritLabTests BqstChainTests -j 8
ctest --test-dir /private/tmp/bqst-grit-check --output-on-failure
```

`--export-existing` regenerates the header from the saved revision-2 report.
The older `hybrid_validation.json` and `hybrid_spread_validation.json` describe
superseded auditions: the mixed-core model and the whole-Hybrid remapping respectively.
Neither describes the current independent-path blend.

## Captured controls and calibration

Select **Grit**, Mix 100%, Vintage off, EQ flat and Autogain on. Drive now uses a generated
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

The core still divides its output by makeup gain. Autogain now adds a separate static
wet compensation table, measured at 0.5 dB knob intervals with integrated LUFS across
five synthetic sources: sine, 808, bassline, drums and dense master. Median loudness
change is targeted at 0 LU. Compensation is exactly 0 dB at zero drive and reaches
+10.65 dB at maximum drive. The toggle uses the existing smooth wet-gain blend; there
is no live detector, loudness tracking or gain pumping. Output trim remains final.

The universal VST3 was measured at 4x oversampling, 48 kHz, nine Drive positions:
musical sources are within approximately -1.36 to +0.78 LU, and the sine reaches
+4.82 LU at maximum Drive. Static compensation cannot match all sources simultaneously.
`calibration.json` and `calibrated_validation.json` include the full per-material spread.
Vintage intentionally changes tone/loudness and is off during calibration.

## Session and preset compatibility

A new appended `gritRevision` choice (host name **Grit Model**, Legacy/Captured/Hybrid) defaults
to Hybrid in production and Lab (production defaulted to Captured before the 1.2.0 release). Host state and user preset formats are now version 2. Production sessions
and user presets predating version 2 use Legacy when they do not specify a revision;
resaving preserves that choice. Older experimental Lab sessions stay on Captured.
Factory presets use the new default. The host's parameter list exposes the choice for
an intentional upgrade; existing parameter IDs, indices and plugin identity are retained.
Revision changes use the existing structural fade and state reset.

The legacy DSP remains intact. The current production VST3 was compared with the
previously installed BQST at Drive 0, 9 and 18, 4x oversampling, Vintage and Autogain on:
both Cream and Legacy Grit were bit-identical for deterministic noise input.

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

Captures: 2026-10-08, Apollo Twin X line I/O, 48 kHz / 24 bit; LA500A minimum makeup
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
python tools/grit_model/fit.py tools/grit_model/capture_targets.npz
python tools/grit_model/fit_driven.py
python tools/grit_model/export.py
python tools/grit_model/calibrate.py
python tools/grit_model/export.py
python tools/grit_model/fixture.py
cmake -S . -B /private/tmp/bqst-grit-check -DBQST_GRIT_LAB=ON -DCMAKE_OSX_ARCHITECTURES=arm64
BUILD_DIR=/private/tmp/bqst-grit-check scripts/check.sh
cmake -S . -B build-release/grit-lab -DCMAKE_BUILD_TYPE=Release -DBQST_GRIT_LAB=ON -DBQST_BUILD_TESTS=OFF
cmake --build build-release/grit-lab --config Release -j 8
```

The fresh release directory inherits universal arm64/x86_64 and macOS 10.13 defaults.
Do not use public packaging scripts for this audition build.

## Local validation and installation

Both standard and Lab CTest configurations pass all four suites, including Python/C++
fixture agreement, measured reference bounds, 8 kHz–768 kHz stability, symmetry,
zero-drive bypass, continuity, automation and structural changes. The universal VST3 is also checked in Pedalboard against the held-out drum captures
at Drive 6/12/18 dB and 4x oversampling.

For the 1.2.0 release, the full `scripts/check.sh` gate (four CTests plus pluginval
strictness 10) passed for both the production and Lab configurations in the desktop
session. The universal 1.2.0 production VST3, as a fresh instance (default Hybrid), was
bit-identical to the approved universal Lab build with Hybrid selected, at Drive 1/6/9/12/18,
Vintage off/on and Autogain off/on (4x oversampling, 48 kHz, settled smoothing).
`scripts/install-grit-lab-local.sh` runs pluginval strictness 10 BEFORE installing either bundle.
Set BQST_VST3_DIR to the custom DAW scan folder when invoking it. The script requires
both universal bundles and valid local signatures. AU goes to the standard user folder.
The local build is ad-hoc signed, not a notarized public release.
