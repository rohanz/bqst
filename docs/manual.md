# BQST Manual

## Overview

BQST is a stereo mastering EQ and saturation plugin inspired by a two-module 500-series rack. The left module is a broad Baxandall-style low/high shelf EQ. The right module is a two-mode saturation stage with VU-style output meters.

The signal flow is:

```text
Input Trim -> EQ -> Saturation -> Mix -> Output Trim
```

## Top Bar

**Input** adjusts the level feeding the EQ and saturation. Hold Control while dragging Input to compensate both Output controls in the opposite direction.

**EQ L/R or EQ M/S** selects how the EQ module is routed.

**EQ Link** links the two EQ sides. When linked, moving either side moves the other side. Hold Control to temporarily edit one side only.

**Sat L/R or Sat M/S** selects how the saturation module is routed.

**Sat Link** links the two saturation sides. When linked, moving either side moves the other side. Hold Control to temporarily edit one side only.

**Realtime Oversampling** sets oversampling during normal playback.

**Render Oversampling** sets oversampling for offline export.

**Autogain** compensates saturation drive level so changes are easier to compare.

**Bypass** bypasses the whole plugin with a short fade and dimmed faceplate.

**Size** switches the fixed UI scale between 75%, 100%, 125%, and 150%.

## Presets

Use the preset strip above the top bar to load factory or user presets. The arrow buttons step through presets. The Save button writes the current sound settings as a user preset.

User presets are saved as `.bqstpreset` files in:

```text
~/Library/BQST/Presets
```

Presets save sound settings only. Utility/session controls such as plugin size, realtime/render oversampling, and global bypass are not saved into presets.

## EQ Module

The EQ has independent Side A and Side B controls. Depending on the top-bar mode, these represent either L/R or M/S.

**HF** boosts or cuts the high shelf from -6 dB to +6 dB.

**HF Freq** selects the high shelf frequency.

**LF Freq** selects the low shelf frequency.

**LF** boosts or cuts the low shelf from -6 dB to +6 dB.

The EQ is intentionally broad and smooth. It is meant for tonal shaping rather than narrow corrective work.

## Saturation Module

The saturation module has independent Side A and Side B Drive, Mix, and Output controls.

**Cream/Grit** toggles the saturation type.

**Cream** is a smooth, thick saturation character based on a combination of op-amp, transistor, and diode-style behavior.

**Grit** is a firmer transformer-style saturation character with more bite.

**Vintage** adds gentle top-end rounding after saturation.

**Drive** controls how hard the saturation stage is pushed.

**Mix** blends the saturated signal with the dry signal. At 100%, the output is fully wet.

**Output** trims the final level for each side from -12 dB to +12 dB.

When Drive is at 0 dB, the saturation stage is effectively bypassed.

## VU Meters

The VU meters show saturation-module output level. They are calibrated around:

```text
0 VU = -18 dBFS
```

They are designed to move like musical level meters rather than fast peak meters.

## Mouse And Keyboard

Drag a knob to adjust it.

Hold Shift while dragging for finer control.

Double-click a knob to reset it to its default.

Hover over a knob to see its current value.

Command-Z undoes the last plugin UI edit. Shift-Command-Z redoes it.

## Format Notes

BQST builds as VST3, AU, and Standalone on macOS. Use VST3 for Ableton/Reaper-style workflows. Use AU for Logic.
