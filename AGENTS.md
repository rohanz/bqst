# BQST Agent Guide

This file is for AI coding agents working on BQST. Read it before changing code.

Machine-specific values — the user's custom DAW VST3 folder, the website repo
location, and the Developer ID signing cert hash — live in `AGENTS.local.md`, a
gitignored, local-only companion to this file (kept out of this public repo). Read it
for the concrete local paths/identifiers; if it is missing, ask the user.

## Project

BQST is a JUCE/C++ audio plugin with VST3, AU, and Standalone targets. It is a 500-series-inspired mastering tone tool: a Baxandall-style EQ module feeding a saturation module.

## Build And Install

Configure release builds when needed:

```sh
scripts/build-macos-release.sh
```

The default release build is universal macOS and must include both Apple Silicon
and Intel slices, pinned to `CMAKE_OSX_DEPLOYMENT_TARGET` macOS 10.13. These are
now defaulted in `CMakeLists.txt` (each overridable with `-D`), so even a plain
`cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release` produces a universal
10.13 build; do not let a release build inherit the build machine's macOS
version. Verify with:

```sh
lipo -info build-release/BQST_artefacts/Release/VST3/BQST.vst3/Contents/MacOS/BQST
lipo -info build-release/BQST_artefacts/Release/AU/BQST.component/Contents/MacOS/BQST
otool -l build-release/BQST_artefacts/Release/AU/BQST.component/Contents/MacOS/BQST | grep -A3 LC_BUILD_VERSION
```

Standalone only:

```sh
cmake --build build-release --config Release --target BQST_Standalone -j 8
```

After building, install local test bundles, clear xattrs, ad-hoc sign, and verify:

```sh
scripts/install-local.sh
codesign --verify --deep --strict "${BQST_VST3_DIR:-$HOME/Library/Audio/Plug-Ins/VST3}/BQST.vst3"
codesign --verify --deep --strict "$HOME/Library/Audio/Plug-Ins/Components/BQST.component"
```

Use `BQST_VST3_DIR=/path/to/custom/vst3 scripts/install-local.sh` if a DAW scans a nonstandard VST3 folder.

The user's DAW scans a custom VST3 folder (absolute path in `AGENTS.local.md`); install
the VST3 there via `BQST_VST3_DIR`. The AU still must go to
`~/Library/Audio/Plug-Ins/Components`; there is no VST2 build.

## Fast Local Checks

For day-to-day work, prefer a native (non-universal) build and the check script,
which is the local equivalent of CI (there is no cloud CI):

```sh
scripts/check.sh   # configure native arch, build VST3 + unit tests, run ctest, pluginval s10
```

Env overrides: `BUILD_DIR`, `STRICTNESS`, `PLUGINVAL`. Unit tests live in
`tests/dsp_tests.cpp` (gated by the `BQST_BUILD_TESTS` CMake option, registered
with CTest) and cover the `BqtDsp.h` invariants: zero-drive bypass, continuity
from zero drive, autogain monotonicity, and finite/bounded output. Run
`scripts/check.sh` before handing off or releasing.

## Public macOS Release Signing

Do not embed Apple account passwords, app-specific passwords, private keys, or
certificate exports in this repo.

For public distribution, use Developer ID signing and notarization. Required
local setup:

- Valid `Developer ID Application: Rohan Kulshrestha (3KJDR8Q6RL)` identity in
  the login keychain.
- Valid `Developer ID Installer: Rohan Kulshrestha (3KJDR8Q6RL)` certificate in
  the login keychain.
- Stored notarytool profile named `bqst-notary`.

Because Codex sandboxing can hide keychain identities, keychain/signing commands
may need elevated execution. Check setup with:

```sh
security find-identity -v -p codesigning
xcrun notarytool history --keychain-profile bqst-notary
```

Build, sign, notarize, staple, and verify the universal package:

```sh
scripts/build-macos-release.sh

PLUGIN_SIGN_IDENTITY="<Developer ID Application SHA-1 — see AGENTS.local.md>" \
INSTALLER_SIGN_IDENTITY="Developer ID Installer: Rohan Kulshrestha (3KJDR8Q6RL)" \
NOTARY_PROFILE="bqst-notary" \
NOTARIZE=1 \
scripts/package-macos.sh
```

There are two `Developer ID Application` certs in the keychain with the same name, so
`codesign` cannot disambiguate by name — pass `PLUGIN_SIGN_IDENTITY` as the SHA-1 hash
from `security find-identity -v -p codesigning`. The exact hash used for releases is in
`AGENTS.local.md`; if it ever fails, use the other listed hash. The Installer cert is
not duplicated, so its full name works as `INSTALLER_SIGN_IDENTITY`.

Final verification:

```sh
pkgutil --check-signature dist/BQST-1.0.2-macOS-universal.pkg
spctl -a -vv -t install dist/BQST-1.0.2-macOS-universal.pkg
```

Expected `spctl` result includes `accepted`, `source=Notarized Developer ID`,
and the Developer ID Installer origin. Do not publish a package that reports
`no signature` or `invalid signature`.

`scripts/package-macos.sh` signs bundles **without** `codesign --deep` (Apple
discourages `--deep` for applying signatures; the VST3/AU bundles have a single
Mach-O and no nested code, so a direct sign is equivalent — `--deep` is kept only
for verification). It also hard-fails if `pkgutil --check-signature` reports a bad
installer signature when an installer identity was provided. The bundle ID is
`tech.ebraudio.bqst` (`BUNDLE_ID` in `CMakeLists.txt`); the AU/VST3 plugin identity
comes from the `RohK`/`Bqs1` codes, not the bundle ID.

Run `git diff --check` before committing.

## Publishing a Release

End to end, to ship a new version:

1. **Bump the version everywhere** and add a `CHANGELOG.md` entry: CMake
   `project(... VERSION ...)`, `scripts/package-macos.sh` (`VERSION` + `PKG_FINAL`),
   `packaging/macos/Distribution.xml`, the installer `ReadMe.html`/`Welcome.html`,
   `packaging/windows/BQST.iss`, `scripts/package-windows.ps1`, and the `dist/BQST-...pkg`
   filename references in the docs. (Manufacturer is `EBR Audio Tech`; do not change the
   `RohK`/`Bqs1` codes — that breaks DAW recall.)
2. **Clean universal build** (`rm -rf build-release` first — incremental Unix Makefiles
   builds do NOT regenerate the bundle Info.plist/moduleinfo, see Validation Expectations).
   Confirm both slices + version + identity with `lipo -archs` and
   `PlistBuddy -c 'Print CFBundleShortVersionString'` / the moduleinfo `Vendor`.
3. **Sign + notarize**: `NOTARIZE=1 scripts/package-macos.sh` (see Public macOS Release
   Signing). Result is `dist/BQST-X.Y.Z-macOS-universal.pkg` — Developer-ID-signed bundles
   (hardened runtime + secure timestamp) inside a signed, notarized, stapled installer.
   The notary status must be `Accepted` and `spctl -a -vv -t install` must report
   `accepted` / `source=Notarized Developer ID`. Spot-check a bundle with `codesign -dvv`:
   it should show `Authority=Developer ID Application`, `flags=...(runtime)`, a `Timestamp`.
4. **Publish the download (separate website repo).** The website repo path is in
   `AGENTS.local.md`; within it, `projects/bqst.md` is the project page and
   `downloads/bqst/` is the served download folder:
   - Copy the notarized pkg in:
     `cp dist/BQST-X.Y.Z-macOS-universal.pkg <website>/downloads/bqst/`
   - Point both download buttons in `projects/bqst.md` at the new filename (two
     `<a href="/downloads/bqst/BQST-...-macOS-universal.pkg" ...>` links).
   - Re-verify the served file with `spctl -a -vv -t install`, then commit + deploy the
     website repo. Old `BQST-*.pkg` in `downloads/bqst/` become orphaned and can be deleted.

## Source Layout

Important files:

- `src/BqtDsp.h`: small DSP constants and inline saturation/autogain functions.
- `src/BqtProcessorDsp.cpp`: audio processing, oversampling, EQ, saturation chain, metering, bypass crossfade.
- `src/BqtParameterLayout.cpp`: APVTS parameter definitions, default values, parameter names visible to DAWs.
- `src/PluginProcessor.h`: processor members and DSP object declarations.
- `src/PluginEditor.cpp`: editor construction, attachments, button setup, top bar help strings.
- `src/BqtEditorLayout.cpp`: faceplate painting and component layout.
- `src/BqtEditorInteraction.cpp`: undo behavior, linked control mirroring, tooltips/readouts, hover behavior.
- `src/BqtEditorWidgets.cpp/.h`: custom look-and-feel, knobs, buttons, VU meters, readout bubble.
- `src/BqtPresetManager.cpp/.h`: factory/user preset handling.
- `tests/dsp_tests.cpp`: dependency-free unit tests for `BqtDsp.h`.
- `scripts/check.sh`: local build + unit test + pluginval check.
- `assets/`: embedded UI image assets.

## Real-Time Safety

`processBlock` and everything it calls run on the audio thread and must stay
allocation-free and lock-free. These rules are enforced in the current code —
keep them:

- **No heap allocation on the audio thread.** Update IIR coefficients in place
  with `juce::dsp::IIR::ArrayCoefficients::makeXxx` (returns a `std::array` by
  value), never `Coefficients::makeXxx` (which `new`s a ref-counted object every
  call).
- **No string parameter lookups.** Read parameters through the cached
  `std::atomic<float>*` pointers in `paramPtrs` (populated once by
  `cacheParameterPointers()`), not `getRawParameterValue(prefix + "...")`. Never
  build a `juce::String` on the audio thread.
- The static saturation tone filters are rebuilt only when the sample rate or the
  vintage flag changes (`updateSaturationToneFilters` early-returns otherwise).
- Drive, pre-drive gain, mix and the drive-derived autogain are smoothed per
  sample inside `processSide` (do not go back to `SmoothedValue::skip`, which
  steps once per block and zippers on automation).
- Latency is reported off the audio thread: `processBlock` only stores the new
  value and calls `triggerAsyncUpdate()`; `setLatencySamples()` runs in
  `handleAsyncUpdate()` on the message thread.
- `processBlock` clears unused output channels and returns early if handed fewer
  than two channels.

The DSP unit tests and `pluginval --strictness-level 10` (both run by
`scripts/check.sh`) guard these — keep them green.

## DSP Signal Flow

Overall:

```text
input
-> input trim
-> EQ module, if enabled
-> saturation module, if enabled
-> output
```

EQ and saturation can independently run in L/R or M/S.

Saturation module:

```text
dry copy saved
-> low guard pre
-> algorithm-specific pre tone
-> pre-drive gain
-> waveshaper
-> algorithm-specific post tone
-> low guard post
-> vintage top rolloff, if enabled
-> wet autogain
-> mix with dry copy
-> output trim
```

Keep this order unless there is a strong reason to change it. In particular, autogain should not be inside the nonlinear waveshaper path. It should compensate the wet signal before dry/wet mix. Output trim is the final manual trim for the blended saturation side.

## EQ Design

The EQ is Baxandall-style broad tone shaping, not a parametric EQ.

Frequency positions in `BqtDsp.h`:

```cpp
lowShelfFrequenciesHz  = { 74, 84, 98, 116, 131, 166, 230, 361 };
highShelfFrequenciesHz = { 1600, 1800, 2100, 2500, 3400, 4800, 7100, 18000 };
```

Shelf gain range is `+/-6 dB`. Shelf Q is `0.38`. Filters are JUCE IIR shelves. Frequencies are clamped to `0.42 * currentSampleRate` as a safety measure.

## Oversampling

Oversampling wraps the full processing chain in `processBlock` when enabled:

```text
host block
-> processSamplesUp
-> currentSampleRate = hostSampleRate * oversamplingFactor
-> processChain
-> processSamplesDown
```

Available realtime/render choices are Off, 2x, 4x, 8x. Realtime and render oversampling are separate parameters. Latency is taken from the active oversampler and reported to the host off the audio thread via `AsyncUpdater` (see Real-Time Safety).

Do not add separate oversampling per module unless there is a very strong reason. The current single-wrapper approach keeps latency/reporting simpler and covers both EQ cramping and saturation aliasing.

## Saturation Algorithms

Saturation type values:

```cpp
SaturationType::density     // UI label: Cream
SaturationType::transformer // UI label: Grit
```

Drive parameter is `0..18 dB`. Internally:

```cpp
drive01 = driveDb / 18
pre-drive gain = driveDb * 0.40
```

At `0 dB`, saturation is effectively bypassed. This is intentional. The curves must ramp continuously from zero drive; do not reintroduce fixed minimum saturation that turns on abruptly at `0.1 dB`.

### Cream

Cream is smoother, dense, and polished. It uses:

- soft asymmetric `tanh` curve
- drive-scaled asymmetry
- drive-scaled odd harmonic weighting
- subtle high-frequency emphasis/de-emphasis
- low-end guard

Cream tone filters:

```cpp
densityBodyFocus   = peak 720 Hz, Q 0.52, +0.85 dB
densityPreEmphasis = high shelf 6.2 kHz, Q 0.55, -0.65 dB
densityDeEmphasis  = high shelf 7.0 kHz, Q 0.50, -0.55 dB
```

The body focus and treble shaping make Cream smoother and less clipper-like when pushed. Vintage is the intentional additional top-softening control.

### Grit

Grit is transformer-inspired: firmer, more forward, more edge/bite than Cream.

Grit tone filters:

```cpp
transformerLowDrive   = low shelf 165 Hz, Q 0.62, +1.10 dB
transformerLowRestore = low shelf 165 Hz, Q 0.62, -0.70 dB
transformerWeight     = peak 245 Hz, Q 0.72, +0.55 dB
transformerTop        = high shelf 7.8 kHz, Q 0.50, -0.75 dB
```

The low-drive/partial-restore pair makes low and low-mid content hit the nonlinear stage harder without simply adding a large final bass boost. The top filter is intentionally mild. It rounds Grit without making it dark.

### Low-End Guard

The saturation low-end safeguard is a pre/post shelf pair:

```cpp
saturationLowGuardPre  = low shelf 95 Hz, Q 0.55, -2.2 dB
saturationLowGuardPost = low shelf 95 Hz, Q 0.55, +2.2 dB
```

This reduces sub energy before the waveshaper, then restores bass after. It is meant to prevent 808s/kicks from dominating the saturation and creating high-frequency buzz. Because saturation is nonlinear, the shelves do not perfectly cancel when Drive is active; this is intentional.

If bass still gets too buzzy, consider drive-dependent low guarding rather than heavy static filtering.

### Vintage

Vintage is an optional top rolloff after saturation:

```cpp
vintage = high shelf 12 kHz, Q 0.42, -3.2 dB when enabled
```

Keep it obvious enough to be a mode, but not so dark that it is only special-effect material.

## Autogain

Autogain is static, drive-based, and saturation-type-specific. It is not live RMS matching. This is intentional.

Why not live RMS/LUFS matching inside the plugin:

- it reacts to source material and can change saturation feel
- 808s/bass dominate RMS and can cause strange level changes
- it can behave like hidden compression/pumping
- LUFS is better for offline calibration than real-time per-block gain correction

Current curve in `BqtDsp.h`:

```cpp
amount   = Cream ? 2.04 : 2.71
exponent = Cream ? 1.67 : 1.48
gain = 1 / (1 + pow(drive01, exponent) * amount)
```

Use `tools/calibrate_autogain.py` to rerun the offline material sweep before changing these values. It tests sine, 808, bassline, drum-bus, and dense-master style signals against the plugin's saturation tone path.

Approximate compensation:

```text
Drive    Cream      Grit
1 dB     -0.1 dB    -0.2 dB
3 dB     -0.8 dB    -1.5 dB
6 dB     -2.4 dB    -3.7 dB
12 dB    -6.2 dB    -7.9 dB
18 dB    -9.7 dB    -11.4 dB
```

These values were calibrated from offline sweeps using sine/two-tone/808-ish tests at several levels. If retuning, use multiple sources and keep the behavior predictable.

## GUI And UX

The GUI is a fixed-size hardware-inspired 500-series faceplate with selectable scale sizes. Avoid freely resizable layout unless deliberately redesigned.

Design goals:

- cream knobs and VU meters on pink faceplates
- strict rack/panel layout
- minimal, analog-inspired, but not clunky
- use digital conveniences where useful: M/S routing per module, autogain, link controls, separate realtime/render oversampling

Top bar controls include presets, size, input trim, EQ mode/link, saturation mode/link, realtime/render oversampling, autogain, bypass.

The saturation panel now has one centered `drive` label between the two drive knobs. Keep it aligned with the visual language of the EQ center labels (`hf`, `freq`, `lf`).

Global bypass dimming is deliberately implemented as one editor-level `BypassOverlay` sibling above `rackComponent`, with bounds matched to the rack. Do not move this back into `RackComponent::paintOverChildren()` or into a rack child component: the VU meters repaint independently at 60 Hz, and child-level dimming can flash as a separate meter-sized rectangle during rapid bypass toggles.

The faceplate is expensive to draw (procedural grain), so `RackComponent::paint`
renders `paintRack` once into an offscreen image (at display scale, like the VU
meter's `rebuildStaticLayer`) and blits it, rebuilding only on a size/scale
change. Keep `paintRack` purely size-dependent — no per-parameter or live content
— or the cache will show stale pixels.

## Tooltips And Readouts

There are two timing classes:

```cpp
hoverHelpDelayMs  = 700 // explanatory help for buttons/menus/etc.
hoverValueDelayMs = 300 // value readouts for knobs/sliders
```

This is intentional. Help text should feel consistent with the JUCE tooltip delay; value previews can appear faster.

Cream/Grit help is dynamic on the sat type button:

- Cream: "Combination of op-amps, transistors and diodes for a smooth, thick saturation."
- Grit: "Transformer-style saturation with firmer edge and bite."

When the sat type changes while help is visible, update the visible readout immediately.

All hover help goes through this custom top-bar system (`setTopBarHelp`), including
combo boxes. There is no JUCE `TooltipWindow`; do not call `setTooltip` on controls
(it would show a second, redundant hint on hover).

## Linking And Undo

EQ and saturation link buttons mirror L/R or M/S-side controls.

Behavior:

- If linked, moving either side moves the other side.
- Holding Control temporarily inverts the link behavior.
- Continuous knobs and stepped frequency selectors should undo as one gesture, not tiny increments.

Undo support is implemented with editor-side snapshots and mirrored parameter gestures. This is intentional: APVTS is constructed with a null `UndoManager` and there is no `juce::UndoManager` member, so the editor-side snapshot stack is the source of truth. Be careful when changing linked control behavior; test Command-Z in Ableton, not only Standalone.

## Presets

Factory/user presets live in `BqtPresetManager`.

Do not save non-musical workflow state into presets:

- size
- render oversampling
- bypass-like workflow state, unless deliberately changed

User presets are `.bqstpreset` files under the app data location returned by `BqtPresetManager::getUserPresetDirectory()`.

Other preset/state behavior to preserve:

- Loading a user preset resets musical params to their defaults first (like the
  factory path) so presets are self-contained; `defaultValues` excludes workflow
  state, so this does not touch oversampling/bypass.
- `saveUserPreset` writes atomically (temp file + swap) and the editor reports a
  failure box; do not go back to writing the target file directly.
- Saved state and presets carry a version attribute (`bqstStateVersion` /
  `bqstPresetVersion`); branch on it to migrate if the format ever changes.
- The editor tracks the selected preset by a stable key (`factory:category/name`
  or `user:<path>`), re-resolved after each refresh — not a raw array index.
- Untrusted input is validated: `setStateInformation` checks the root tag and
  strips non-finite values; `setValueNotifyingHost` drops non-finite values and
  clamps to range. A crafted/corrupt preset must never reach the DSP as NaN
  (e.g. `roundToInt(NaN)` as a choice index).

## Validation Expectations

Before handing off plugin changes:

1. `git diff --check`
2. `scripts/check.sh` passes (build + unit tests + `pluginval --strictness-level 10`).
3. If DSP changed, briefly describe the signal-flow impact and what to test.

### Installing a build the user can actually test in a DAW

This bit us once — follow it exactly:

- **Build universal, not native.** `scripts/check.sh` builds native arm64 for
  speed, which is fine for validation but **must not** be installed for DAW
  testing: an arm64-only bundle will not load in a Rosetta/x86_64 host, and a
  native build can leave an **empty AU binary** (`.component/Contents/MacOS` with
  no executable), which is the "audio unit could not be updated" error. For
  install, build universal: `scripts/build-macos-release.sh` (or a plain release
  `cmake` build — the CMake defaults are universal) into `build-release/`, then
  confirm both slices exist with `lipo -archs`.
- **Install from `build-release/`** with `scripts/install-local.sh`. The user's
  DAW scans a custom VST3 folder, so pass it through:
  `BQST_VST3_DIR="<custom VST3 folder — see AGENTS.local.md>" scripts/install-local.sh`.
  The AU always installs to `~/Library/Audio/Plug-Ins/Components`.
- **Remove conflicting older installs first.** A previously released build under
  `/Library/Audio/Plug-Ins/{VST3,Components}/BQST.*` (needs `sudo`) shares the
  VST3 UID and AU codes, so the DAW dedups to it and the new build "doesn't show
  up" / the AU registry conflicts. Also remove any stray copy in the standard
  `~/Library/Audio/Plug-Ins/VST3` if installing to a custom folder.
- After installing, rescan plugins in the DAW (and for AU,
  `killall -9 AudioComponentRegistrar` or relaunch the host).
- **Identity/version changes need a clean build.** Changing `COMPANY_NAME`,
  `BUNDLE_ID`, or the version does **not** regenerate the bundle's
  `Info.plist`/`moduleinfo.json` on an incremental Unix Makefiles build (it's
  timestamp-based and doesn't see the changed generator command). `rm -rf` the
  build dir (or build into a fresh one) so the new metadata is baked in — verify
  with `PlistBuddy -c 'Print CFBundleIdentifier'` and the moduleinfo `Vendor`.

For major release readiness, run the full notarized package build.

## Git

Remote:

```text
origin https://github.com/rohanz/bqst.git
```

Branch:

```text
master
```

Commit and push plugin changes often when the user asks. Do not include unrelated generated files.
