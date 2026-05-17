# BQST

BQST is a JUCE/C++ audio plugin: a two-module mastering tone tool with a broad Baxandall-style EQ feeding a two-mode saturation stage.

It builds as:

- VST3
- AU
- Standalone

Portfolio writeup:

https://rohanjk.xyz/projects/bqst

## Features

- Bax-style low/high shelves with broad, low-Q curves.
- Independent side controls in L/R or M/S mode.
- Separate L/R or M/S routing for the EQ and saturation modules.
- Cream and Grit saturation modes.
- Per-side Drive, Mix, and Output controls.
- Vintage top rounding after saturation.
- Static drive-based autogain calibrated from offline test material.
- Realtime and render oversampling up to 8x.
- VU-style output meters calibrated around `0 VU = -18 dBFS`.
- Factory and user presets saved as `.bqstpreset` files.
- 500-series-inspired fixed-size UI with 75%, 100%, 125%, and 150% view sizes.

## Build

Requirements:

- macOS
- CMake 3.22+
- A C++17 compiler
- JUCE checked out as the `external/JUCE` submodule

Clone with submodules:

```sh
git clone --recurse-submodules https://github.com/rohanz/bqst.git
cd bqst
```

Configure and build:

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --config Release
```

Release artifacts are written to:

```text
build-release/BQST_artefacts/Release/VST3/BQST.vst3
build-release/BQST_artefacts/Release/AU/BQST.component
build-release/BQST_artefacts/Release/Standalone/BQST.app
```

## Local Install

Install the release build into the standard user plugin folders:

```sh
scripts/install-local.sh
```

Use a custom VST3 folder if needed:

```sh
BQST_VST3_DIR="/path/to/custom/vst3" scripts/install-local.sh
```

The script clears quarantine attributes and ad-hoc signs the copied bundles for local testing.

## Validation

The release VST3 has passed `pluginval` strictness level 10 during development.

Example validation commands:

```sh
/Applications/pluginval.app/Contents/MacOS/pluginval --validate build-release/BQST_artefacts/Release/VST3/BQST.vst3 --strictness-level 10
/Applications/pluginval.app/Contents/MacOS/pluginval --validate build-release/BQST_artefacts/Release/AU/BQST.component --strictness-level 10
```

The plugin has also been tested in Ableton for loading, UI interaction, parameter automation, preset recall, bypass behavior, and offline render paths.

## Packaging

The repo includes an initial macOS package builder:

```sh
scripts/package-macos.sh
```

For public distribution, install Apple Developer ID certificates and run it with:

```sh
PLUGIN_SIGN_IDENTITY="Developer ID Application: Your Name (TEAMID)" \
INSTALLER_SIGN_IDENTITY="Developer ID Installer: Your Name (TEAMID)" \
scripts/package-macos.sh
```

The resulting package still needs notarization before public release.

## Docs

- [docs/manual.md](docs/manual.md): user-facing controls and workflow.
- [docs/design.md](docs/design.md): product/design direction.
- [docs/build-notes.md](docs/build-notes.md): detailed build, install, and validation notes.
- [AGENTS.md](AGENTS.md): implementation notes for future AI/coding-agent sessions.
- [CHANGELOG.md](CHANGELOG.md): release notes.

## Status

BQST is a portfolio/release-candidate project. Source is public for review and learning, while signed installers and formal binary releases are still being prepared.
