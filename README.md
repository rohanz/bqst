# BQST

BQST is a mastering tone plugin built around a broad Baxandall-style EQ feeding a two-mode saturation stage. It is designed for subtle mix-bus and master-bus tone shaping: lift, weight, density, and controlled harmonic color without turning into a distortion box by default.

Portfolio writeup: https://rohanjk.xyz/projects/bqst

## Download

The recommended macOS release is the signed and notarized installer:

[Download BQST 1.0.0 for macOS](https://www.rohanjk.xyz/downloads/bqst/BQST-1.0.0-macOS.pkg)

The installer includes:

- VST3: `/Library/Audio/Plug-Ins/VST3/BQST.vst3`
- Audio Unit: `/Library/Audio/Plug-Ins/Components/BQST.component`

Factory presets are built into the plugin. User presets can be saved from the plugin UI as `.bqstpreset` files.

## Features

- Bax-style low and high shelves with broad, low-Q curves.
- Independent side controls in L/R or M/S mode.
- Separate routing modes for the EQ and saturation sections.
- Cream and Grit saturation modes.
- Per-side Drive, Mix, and Output controls.
- Vintage top rounding after saturation.
- Static drive-based autogain calibrated from offline test material.
- Realtime and render oversampling up to 8x.
- VU-style output meters calibrated around `0 VU = -18 dBFS`.
- Factory presets, user preset saving, and previous/next preset navigation.
- Fixed hardware-inspired UI with 75%, 100%, 125%, and 150% view sizes.

## Formats

- macOS VST3
- macOS Audio Unit
- macOS Standalone app when building from source

## Build From Source

Requirements:

- macOS
- CMake 3.22+
- Xcode or Apple command line tools
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

Build artifacts are written to:

```text
build-release/BQST_artefacts/Release/VST3/BQST.vst3
build-release/BQST_artefacts/Release/AU/BQST.component
build-release/BQST_artefacts/Release/Standalone/BQST.app
```

Additional developer build, validation, local install, and packaging notes are in
[docs/build-notes.md](docs/build-notes.md).

## Validation

The release VST3 and Audio Unit builds have passed `pluginval` strictness level 10 during development. BQST has also been tested in Ableton for loading, UI interaction, parameter automation, preset recall, bypass behavior, sample-rate changes, fixed UI sizes, and offline render paths.

## Documentation

- [docs/manual.md](docs/manual.md): controls and workflow.
- [docs/design.md](docs/design.md): design and product direction.
- [docs/build-notes.md](docs/build-notes.md): detailed build, packaging, signing, and validation notes.
- [packaging/README.md](packaging/README.md): installer artwork and packaging notes.
- [CHANGELOG.md](CHANGELOG.md): release notes.

## License

BQST source code is licensed under GPLv3. See [LICENSE.md](LICENSE.md) and
[LICENSE-GPLv3.txt](LICENSE-GPLv3.txt).

The BQST name, logo, UI artwork, installer artwork, website images, factory presets,
screenshots, and related branding assets are reserved. See [TRADEMARKS.md](TRADEMARKS.md).
