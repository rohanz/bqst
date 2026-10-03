# Changelog

## 1.1.2

- Brighter VU meter faces: the unlit part of the face is less dark.

## 1.1.1

- Fixed the VU meters occasionally dimming a frame before the rest of the panel when bypass
  was toggled while audio was playing.

## 1.1.0

Sound and behaviour changes:

- New Cream algorithm. Cream has been rebuilt: thicker low end, more harmonic density through the
  mids and top, gentler peak rounding, and a Drive knob that moves continuously and evenly from
  transparent to heavily saturated. Existing sessions and presets that use Cream will sound
  different. The Cream factory presets have been retuned to keep their saturation amount.
- New Vintage curve for both Cream and Grit: a broader, gentler top-end shelf.
- Linking now happens inside the plugin. With EQ Link or Sat Link on, the R/S side follows the
  L/M side and any R/S automation for that group is ignored. A linked knob move is now a single
  undo step in hosts (it used to write both sides, which cost two). Turning a link off from its
  button copies L/M into R/S first; Control no longer temporarily unlinks, but still moves both
  sides while unlinked.
- Sessions saved with a link on but differing sides now play R/S at the L/M value.

Look and feel:

- Backlit VU meters: an off-white face lit from below, with the scale numbers centred on their
  ticks and one minus and one plus sign at the ends of the scale.
- Bypass now dims the whole panel at once; the meters no longer dim separately when bypass is
  toggled quickly.
- Drop-down menus use the panel's font and colours, with a lamp marking the current choice.
- Top-bar buttons that are switched on (links, autogain, bypass) now light up pink.

Internal:

- Saturation state is reset through one shared path; mid/side conversion, parameter-ID prefixes
  and preset defaults each have a single source.
- Removed an unused legacy saturation curve.

## 1.0.3

Sound changes:

- Zero drive is now genuinely transparent. The saturation stage's tone shaping (and the Vintage
  shelf) used to snap on at full strength the instant Drive left 0.0, applying about 1.9 dB of
  tilt with no ramp. It now fades in with drive, reaching full strength at 6 dB. Settings above
  6 dB of drive are unchanged.
- The EQ curve no longer changes with the oversampling setting. The shelves were designed at
  whatever the oversampled rate happened to be, so the same nominal curve measured differently at
  2x than at 8x — and since realtime defaults to 2x and render to 4x, a bounce did not match
  playback. They now use a decramped design that tracks the analog prototype at the host rate to
  within 0.24 dB (previously up to 1.21 dB off at the 18 kHz position). The high shelf, especially
  the 18 kHz setting at 44.1/48 kHz, is the most affected.
- Parameter ramps now take the same time at every oversampling factor. Smoothing was set from the
  base rate but consumed per oversampled sample, so a 20 ms ramp finished in 2.5 ms at 8x.
- Added a DC blocker to the saturation path. Both curves are asymmetric and left a measurable DC
  offset (about -24.6 dBFS at full Cream drive) that nothing downstream removed.
- Switching oversampling, and entering an offline bounce, no longer clicks from stale filter state.
- The bypass crossfade no longer dips toward silence the first time bypass is engaged.
- The host's own bypass button now uses the plugin's latency-compensated crossfade.

Fixes:

- Fixed a potential crash: a host block larger than the prepared size could write past the end of
  the oversampler's buffer. Oversized blocks are now split, which also removed the last two
  allocations from the audio thread.
- Fixed linked EQ/saturation sides collapsing onto one value when a session or preset was loaded.
- Fixed the meters latching to NaN after a zero-length process block.
- Fixed the VU needle looking intermittently low-framerate: it is now synced to the display
  refresh with time-based rather than per-frame smoothing.
- Fixed hover readouts being able to latch onto a different instance of the plugin.
- Fixed a full-UI repaint running 60 times a second while the interface was idle.
- The selected preset name and the view size are now remembered when the editor is reopened.
- Preset files are validated before being applied; a malformed file no longer silently resets
  every parameter.
- Parameters missing from an older saved state now reset to their defaults instead of inheriting
  whatever was previously loaded.

Internal:

- Added chain-level tests covering the whole processing chain, not just the DSP helpers.
- Added CI, and `scripts/check.sh` now builds all formats and fails if validation did not run.
- The version and bundle identifier now live in one place instead of nine.

## 1.0.2

- Fixed the About panel being hidden behind the bypass dimming when bypass is on.
- Released under the EBR Audio Tech brand: manufacturer name and bundle id updated. Plugin identity codes are unchanged, so existing sessions still recall the plugin.
- Removed all heap allocation from the audio thread (in-place IIR coefficients via ArrayCoefficients, cached atomic parameter pointers) to prevent dropouts under load.
- Smoothed Drive, Mix, and autogain per sample so automating them no longer steps/zippers; steady-state sound is unchanged.
- Reported plugin latency to the host off the audio thread.
- Validated untrusted state and presets: non-finite (NaN/Inf) and out-of-range values are rejected before reaching the DSP.
- User presets now load self-contained (reset to defaults first); presets save atomically with a failure message; state and presets carry a format version.
- Cached the static faceplate to an offscreen image for smoother repaints (undo/redo, bypass).
- Guarded processBlock against non-stereo buffers; stop the editor timer on close; removed dead code.
- Build now defaults to a universal arm64+x86_64 / macOS 10.13 binary; signing no longer uses `codesign --deep`; added DSP unit tests and `scripts/check.sh`.

## 1.0.0

- Bumped the plugin version to 1.0.0.
- Added AU build output alongside VST3 and Standalone.
- Added a simple user manual in `docs/manual.md`.
- Split the editor implementation into focused files for setup, interaction/undo, layout/painting, and presets.

## 0.1.2

- Added a preset strip above the main utility bar.
- Added factory presets: Default, Clean Bax Lift, Cream Glue, Grit Console Push, Wide Air MS, and Subtle Master Polish.
- Added user preset saving as `.bqstpreset` XML files.
- User presets default to `~/Library/BQST/Presets`.
- Added previous/next preset buttons and preset hover help.
- Expanded fixed UI size options to 75%, 100%, 125%, and 150%.
- Updated factory preset values from user-tuned preset files and added Cream Sheen.
- Root-level user presets now appear directly under User instead of User > User.
- Presets now ignore utility/session controls: realtime/render oversampling, EQ/Sat in state, and global bypass.
- Split editor drawing support into dedicated style/widget files for easier maintenance.
- Added APVTS undo grouping for knob drags and reduced input trim to +/-12 dB.
- Refined VU meter markings and global bypass dimming behavior.

## 0.1.1

- Renamed the plugin to BQST.
- Added the current 500-series-inspired faceplate UI.
- Added fixed UI size selection: 100% and 125%, defaulting to 100%.
- Cleaned up host automation names to match the faceplate labels.
- Hid obsolete per-module bypass parameters from host automation.
- Added realtime/render oversampling controls up to 8x.
- Added global bypass crossfade and latency-matched bypass path.
- Added VU-style saturation output meters.
- Added Control-drag input/output trim compensation.
- Set Auto Gain on by default.

Validation status:

- Release VST3 builds successfully.
- pluginval strictness 10 passes on the built VST3.
- The installed VST3 has been copied to Rohan's custom Ableton VST3 folder and ad-hoc signed.

Known release tasks:

- Confirm the latest UI and automation names inside Ableton after a full plugin rescan.
- Run final listening tests on kick-heavy, bright, full-mix, and mono/stereo material.
