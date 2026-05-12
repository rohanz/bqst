# Changelog

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
