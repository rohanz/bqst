# Build Notes

## Configure

```sh
cmake -S . -B build
```

## Build Debug VST3

```sh
cmake --build build --config Debug
```

The development artifact is written to:

```text
build/BQST_artefacts/VST3/BQST.vst3
```

To install a local VST3 test build manually:

```sh
mkdir -p ~/Library/Audio/Plug-Ins/VST3
cp -R build/BQST_artefacts/VST3/BQST.vst3 ~/Library/Audio/Plug-Ins/VST3/
```

If your DAW scans a custom VST3 folder, point `BQST_VST3_DIR` at it when running the install script:

```sh
BQST_VST3_DIR="/path/to/custom/vst3" scripts/install-local.sh
```

## Build Release

```sh
scripts/build-macos-release.sh
```

By default, this creates a universal macOS release containing both Apple Silicon
and Intel slices. To build an architecture-specific test artifact instead, pass
`arm64` or `x86_64`.

The default deployment target is macOS 10.13 so the plugin can load on older
Intel systems instead of inheriting the build machine's current macOS version.
Override with `MACOS_DEPLOYMENT_TARGET=...` only when intentionally raising the
minimum supported macOS version.

The release artifacts are written to:

```text
build-release/BQST_artefacts/Release/VST3/BQST.vst3
build-release/BQST_artefacts/Release/AU/BQST.component
```

Install the release build with:

```sh
scripts/install-local.sh
```

The local install script copies:

```text
VST3 -> ${BQST_VST3_DIR:-~/Library/Audio/Plug-Ins/VST3}/BQST.vst3
AU   -> ~/Library/Audio/Plug-Ins/Components/BQST.component
```

## Validate

```sh
/Applications/pluginval.app/Contents/MacOS/pluginval --validate build-release/BQST_artefacts/Release/VST3/BQST.vst3 --strictness-level 10
/Applications/pluginval.app/Contents/MacOS/pluginval --validate ~/Library/Audio/Plug-Ins/Components/BQST.component --strictness-level 10
```

This may need to run outside the sandbox because pluginval opens plugin bundles and editors.

## Package and Notarize

Build a local installer package:

```sh
scripts/package-macos.sh
```

This produces `dist/BQST-$BQST_VERSION-macOS-universal.pkg` with ad-hoc signed universal plugin bundles. It is useful for install testing, but it is not suitable for public distribution.

The package script verifies that both VST3 and AU binaries include `arm64` and
`x86_64` before staging the installer. For an architecture-specific internal
package, override the build and expected architecture explicitly:

```sh
BUILD_DIR="$PWD/build-release-arm64" \
PKG_FLAVOR="arm64" \
EXPECTED_MACOS_ARCHS="arm64" \
scripts/package-macos.sh
```

For a public macOS package, install the Developer ID Application and Developer ID Installer certificates, then create a notary keychain profile:

```sh
xcrun notarytool store-credentials bqst-notary --apple-id "you@example.com" --team-id "TEAMID" --password "app-specific-password"
```

If Xcode was just installed and `xcode-select -p` still prints `/Library/Developer/CommandLineTools`, switch the active developer directory once:

```sh
sudo xcode-select -s /Applications/Xcode.app/Contents/Developer
```

If you cannot switch it globally, prefix the package command with:

```sh
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
```

Then run:

```sh
PLUGIN_SIGN_IDENTITY="Developer ID Application: Your Name (TEAMID)" \
INSTALLER_SIGN_IDENTITY="Developer ID Installer: Your Name (TEAMID)" \
NOTARY_PROFILE="bqst-notary" \
NOTARIZE=1 \
scripts/package-macos.sh
```

The script signs the VST3/AU bundles, signs the installer package, submits it with `notarytool`, staples the notarization ticket, and verifies the final package with `spctl`.

The macOS installer uses `packaging/macos/Distribution.xml` with Welcome and Read Me pages. The installer artwork is generated from the BQST project banner in the separate website-redesign repo (`assets/images/projects/bqst/banner.png` within it).

## Windows Installer

The Windows installer project uses Inno Setup 6 and the cropped artwork in `packaging/windows`.

The Windows installer is built locally on Windows (there is no CI workflow in this repo).
Build the VST3, then run the packaging script:

```powershell
cmake -S . -B build-windows -DCMAKE_BUILD_TYPE=Release
cmake --build build-windows --config Release
powershell -ExecutionPolicy Bypass -File scripts/package-windows.ps1
```

The script expects:

```text
build-windows/BQST_artefacts/Release/VST3/BQST.vst3
```

It writes:

```text
dist/BQST-$BQST_VERSION-Windows.exe
```

## Current Build Status

The project configures and builds Debug and Release VST3, AU, and Standalone artifacts.

The Release VST3 has passed pluginval strictness level 10.

The macOS package script can build, sign, submit, staple, and verify a notarized installer
when run with Developer ID identities and `NOTARIZE=1`.

Implemented in the current build:

- JUCE/CMake plugin target.
- Stereo VST3 processor.
- Independent L/R and M/S routing for the EQ and saturation modules.
- Input Trim from -18 dB to +18 dB.
- EQ Link and Saturation Link controls.
- Per-side low/high Bax-style shelves in 0.1 dB gain steps.
- Bax shelves use a broad low-Q curve and clamp very high shelf frequencies safely below Nyquist.
- Per-side Cream and Grit saturation choices.
- Cream uses a smooth asymmetric nonlinear curve with drive-scaled harmonic weighting.
- Cream includes body focus, treble shaping, and low-end guarding around saturation.
- Grit uses transformer-inspired low/low-mid weighting, partial low restore, and post-saturation top rounding.
- Grit is one model: 75% original Grit + 25% captured hardware whole-path model.
  Older Grit model choices in states and presets are ignored.
- Saturation Drive from 0 dB to +18 dB, with reduced internal drive scaling for gentler onset.
- Vintage adds optional top rounding after saturation.
- Per-side saturation Mix controls.
- Shared Vintage saturation tone control on the saturation module plate.
- Auto Gain level-matches the wet saturation path before per-side Mix blending.
- Continuous gain/color controls use short smoothing ramps to reduce zipper noise during moves and automation.
- Oversampling wraps the full active EQ and saturation chain, so Dry/wet Mix is aligned inside the same oversampled path.
- Sliders support Shift fine-drag and explicit double-click reset values.
- Drive at 0 skips saturation and saturation autogain.
- Per-side output trim.
- Per-side VU-style saturation output meters in the editor.
- VU meter calibration: 0 VU = -18 dBFS with sample-time moving-coil ballistics reaching 99% in 300 ms.
- Realtime/render oversampling up to 8x around the full active processing chain.
- Oversampling latency reporting with global-bypass delay matching.
- Global bypass uses a short dry/wet crossfade to avoid clicky hard switching.
- Top-bar controls for input, mode, link, oversampling, auto gain and bypass; the editor resizes freely from a corner grip.
- Preset strip with factory presets, previous/next navigation, and user preset saving.
- 500-series-inspired UI using local PNG assets and procedural faceplate rendering.

## Single-model Grit regression (1.3.1)

Compared native arm64 Release renders with Apple clang 21.0.0 before and after
removing the Grit Model choice. Each of the nine scenarios contains 382,976 bytes
of interleaved-by-block float32 output.

| Scenario | Before | Result |
|---|---|---|
| 1–3 | Cream | Byte-identical |
| 4 | Current Grit (formerly Hybrid) | Byte-identical |
| 5–7 | Cream/EQ | Byte-identical |
| 8 | Legacy Grit, Vintage on | Changed intentionally to current Grit |
| 9 | Current Grit, M/S, 8x | Byte-identical |

Full render SHA-256 before:
`d051d231c304aa1af95c3dda89e3cd1fcbb1a753ea9fc33e90ba4b272c626517`

Full render SHA-256 after:
`bbd7d81b8d461e764326d9cbbe1d79493ddad0554054a3d64a5741f779a3157c`

The chain tests also load state versions 0–3 with each old revision value, no value,
out-of-range values and NaN; rendered output must match fresh current Grit byte for
byte, with Vintage off/on. Old preset revision values are covered separately.
State and preset writers now emit v3; old revisions are discarded/ignored.

## Frame log

Quit the host, then start its executable from Terminal with a writable log path:

```sh
BQST_FRAME_LOG="$HOME/Desktop/bqst-frames.log" "/Applications/Your Host.app/Contents/MacOS/Your Host"
```

The variable is read when each BQST editor opens. An unset/empty value disables
logging; an invalid path leaves it disabled. Output appends, so use a fresh path
for each capture and open one BQST editor for an unambiguous trace. No parameters,
presets or saved state are changed. Close the editor to stop logging.

A background writer flushes one line per second even when the host message thread
is stalled. `interval_ms` is the actual reporting interval. `frames` counts completed
display-refresh callbacks, `gap_max_ms` and `gap_p95_ms` describe callback entry gaps
(nearest-rank 95th percentile), and `gaps_gt25`/`gaps_gt50` count gaps strictly above
25/50 ms. Gaps cross reporting boundaries; a stall appears in the gap statistics
when the next callback completes. A zero-frame interval itself indicates no
completed display callback, including when the editor is hidden.

`callback_ms`/`callback_max_ms` are total/maximum time inside display callbacks;
`timers`, `timer_ms` and `timer_max_ms` are the count and total/maximum time inside
the 60 Hz editor timer. Times are wall-clock milliseconds, including any OS
preemption. `audio_blocks_per_s` is the processor's block count divided by the
actual interval; `samples_per_block` is its mean host block size (zero if idle).
Audio counters are sampled atomically and are approximate at interval boundaries.
`native_frames`, `painted_frames` and `mixed_frames` count callbacks ending with
both needles native, both painted, or one of each. `dropped` counts diagnostic
events discarded if the bounded queue fills while disk I/O is slow; such intervals
are incomplete. Disk writing never runs in either editor callback or audio processing.

On macOS a native needle is not moved by those display callbacks: the editor's needle
clock (a display link bound to the screen the editor's window is on, running on its
own thread) moves it at every refresh of that screen, however often the host lets the
message thread run (`src/BqtVuNeedleLayer.h`). Anything BQST paints over a needle's
sweep either reports its opaque shape (`bqst::vu::OpaqueCover`: the About card, the value
readout), and the needle layer is masked by that shape so the needle stays native and
still passes behind it, or it does not, and that needle is painted instead. A shape is a
rounded rectangle, which the mask cuts out exactly; the cover paints its fill from the same
shape. The editor re-presents the needles in the same turn as a cover is shown, hidden or
moved, so no frame pairs a cover with an old mask. One limit: on the one-pixel antialiased
fringe of a cover the result is a layer above the paint, not the needle painted under the
cover. Where the cover has coverage `a` over needle N, cover colour C and face B, the
screen shows (1-a)N + a(aC + (1-a)B) instead of (1-a)N + aC, a difference of a(1-a)(B-C),
at most a quarter of B-C. These fields
show its real rate next to the callback rate above:

- `refresh_hz`: the clock's display link refresh rate (the screen's current rate);
  0 until the clock has ticked once.
- `screen_max_fps`: the maximum refresh rate macOS reports for that screen (macOS 12+;
  0 if unknown). A 60 here means the screen itself is limited to 60 Hz (for example a
  ProMotion panel set to a fixed 60 Hz, or an external display).
- `needle_presents_per_s`: changed needle positions the clock committed to the system
  compositor per second of the actual interval (one commit moves both needles). The
  clock commits only when a reading changed, so it should equal `refresh_hz` whenever a
  native needle is moving and the audio side publishes at least once a refresh, even in
  an interval with `frames=0`. It is lower when host blocks are longer than a refresh
  (the reading then changes once a block), and 0 while the needles stand still (bypassed
  and at rest, or fully released) or both are painted (a cover over them that does not
  report its shape; the clock stops then).
- `needle_gap_max_ms`: the longest time between two consecutive commits in the
  interval, while the clock was running (a refresh skipped because the message thread
  was changing a needle at that instant shows up here). A needle that stood still for
  more than 250 ms starts a fresh measurement when it moves again.
- `needle_cpu_ms`: CPU time the clock thread spent inside its ticks during the
  interval (path, Core Animation commit). At 60 Hz it measured about 3 ms per second
  on an M-series Mac, so about 6 ms per second (0.6% of one core) at 120 Hz.

A commit is what this process controls. That the window server then shows each one on
screen is proven by `BqstVuNeedleTests`, which captures its own window with
ScreenCaptureKit while the message thread is stalled or throttled to 30 events per
second (it needs Screen Recording permission for the terminal running it, and says
SKIP without it).
