# BQST

BQST is a JUCE/C++ audio plugin: a dual Baxandall-style mastering EQ feeding a two-mode saturation module.

Current build:

- Bax-style low/high shelves with independent L/M and R/S controls.
- EQ and saturation can each run in L/R or M/S mode.
- Cream and Grit saturation modes with per-side Drive, Mix, and Output.
- Vintage top rounding after saturation.
- Realtime and render oversampling up to 8x.
- Auto gain for saturation level matching.
- 500-series-inspired fixed-size UI with 75%, 100%, 125%, and 150% view sizes.
- Factory and user presets saved as `.bqstpreset` files.
- VU-style output meters calibrated around 0 VU = -18 dBFS.

Useful docs:

- [docs/project-plan.md](docs/project-plan.md) for design and DSP notes.
- [docs/build-notes.md](docs/build-notes.md) for build, install, and validation commands.
- [docs/manual.md](docs/manual.md) for user-facing controls and workflow.
- [CHANGELOG.md](CHANGELOG.md) for release notes.
