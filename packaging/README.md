# Installer Packaging

Installer artwork is stored in `packaging/assets`, with macOS-specific resources in
`packaging/macos/resources` and Windows-specific resources in `packaging/windows`.

Asset sizes:

- `assets/wizard-image.png`: 164 x 314
- `assets/wizard-small-image.png`: 55 x 55
- `assets/installer-background.png`: 800 x 450
- `macos/resources/background.png`: 620 x 418 for the macOS Installer UI
- `windows/wizard-image.bmp`: 164 x 314 for Inno Setup
- `windows/wizard-small-image.bmp`: 55 x 55 for Inno Setup

## macOS

The macOS package uses `packaging/macos/Distribution.xml` and resources in
`packaging/macos/resources`.

Build a local unsigned/ad-hoc package:

```sh
scripts/package-macos.sh
```

Build a signed and notarized package:

```sh
PLUGIN_SIGN_IDENTITY="Developer ID Application: Your Name (TEAMID)" \
INSTALLER_SIGN_IDENTITY="Developer ID Installer: Your Name (TEAMID)" \
NOTARY_PROFILE="bqst-notary" \
NOTARIZE=1 \
scripts/package-macos.sh
```

The output is:

```text
dist/BQST-1.0.2-macOS-universal.pkg
```

## Windows

The Windows installer is built locally on Windows with `scripts/package-windows.ps1`
(Inno Setup 6). See [../docs/build-notes.md](../docs/build-notes.md) for the full steps.

The script expects:

- the Windows VST3 bundle to exist and contain a Windows `.vst3` binary
- `dist/BQST-1.0.2-Windows.exe` to be produced and non-empty

Build BQST on Windows first:

```powershell
cmake -S . -B build-windows -DCMAKE_BUILD_TYPE=Release
cmake --build build-windows --config Release
```

Then install Inno Setup 6 and run:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/package-windows.ps1
```

The installer output is:

```text
dist/BQST-1.0.2-Windows.exe
```

The installer places the VST3 bundle in:

```text
C:\Program Files\Common Files\VST3\BQST.vst3
```
