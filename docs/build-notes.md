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

This produces `dist/BQST-1.0.0-macOS-universal.pkg` with ad-hoc signed universal plugin bundles. It is useful for install testing, but it is not suitable for public distribution.

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

The macOS installer uses `packaging/macos/Distribution.xml` with Welcome and Read Me pages. The installer artwork is generated from the BQST project banner in the website redesign:

```text
/Users/rohan/Documents/progwork/www/rohan-website-redesign/assets/images/projects/bqst/banner.png
```

## Windows Installer

The Windows installer project uses Inno Setup 6 and the cropped artwork in `packaging/windows`.

The GitHub Actions workflow `.github/workflows/windows-installer.yml` builds the Windows VST3 on `windows-latest`, runs Inno Setup, verifies the expected output files, and uploads the VST3 and installer as artifacts.

On Windows:

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
dist/BQST-1.0.0-Windows.exe
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
- VU meter calibration: 0 VU = -18 dBFS with 300 ms smoothing.
- Realtime/render oversampling up to 8x around the full active processing chain.
- Oversampling latency reporting with global-bypass delay matching.
- Global bypass uses a short dry/wet crossfade to avoid clicky hard switching.
- Top-bar controls for input, mode, link, oversampling, auto gain, bypass, and fixed UI size.
- Preset strip with factory presets, previous/next navigation, and user preset saving.
- 500-series-inspired UI using local PNG assets and procedural faceplate rendering.
