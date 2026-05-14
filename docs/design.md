# BQST Design Brief

BQST is a mastering-friendly tone plugin: a broad Baxandall-style EQ feeding a two-mode saturation stage. It should feel like a carefully chosen piece of analog hardware, but it should behave like good software.

This document describes the product taste. For implementation details, see `AGENTS.md`.

## Product Intent

BQST should be useful on a mix bus, master bus, drum bus, bass, vocal, or instrument group. It is not meant to be a science-project distortion box first. It should start subtle, stay controllable, and become stronger only when the user deliberately pushes it.

The core promise:

```text
broad tone shaping -> tasteful density/edge -> predictable level control
```

The plugin should make it easy to do a few common jobs:

- add weight without making the low end messy
- add air without harshness
- add density without obvious clipping
- add transformer-style edge when wanted
- compare saturation changes without being fooled by loudness
- work in L/R or M/S independently per module

## Sound

The sound should prioritize broad usefulness over extreme character. The top end should not get brittle quickly, especially on bass-heavy material. The low end should remain solid, but it should not be allowed to dominate the nonlinear stage.

Good BQST saturation should feel like:

- controlled
- smooth enough for mastering
- still capable of obvious color at higher Drive
- strong at maximum settings, but not broken
- cleanly bypassed when Drive is `0 dB`

Avoid:

- immediate buzz at tiny Drive values
- hidden compression-like gain behavior
- huge tonal shifts when Autogain is toggled
- harsh upper-mid crunch unless the user pushes Grit hard
- making Cream secretly dark by default

## EQ Module

The EQ is a broad Baxandall-style tone control, not a surgical EQ.

The left module should feel like a mastering Bax EQ:

- high shelf and low shelf
- +/-6 dB gain range
- stepped frequency selectors
- broad, low-Q curves
- fine gain movement for plugin use
- independent side controls, with link available

The EQ should be able to make a whole mix brighter, heavier, lighter, or darker without sounding like a narrow EQ move.

It should not grow into a full parametric EQ unless that is a deliberate product direction change.

## Saturation Module

The saturation module is the right-hand 500-series unit. It has two characters:

### Cream

Cream should be the smoother, thicker, more polished algorithm. It is inspired by analog density: op-amps, transistors, diodes, and subtle tape-like emphasis/de-emphasis behavior. It should be the safer mastering choice.

Cream should:

- round peaks rather than chop them
- add density without obvious fizz
- stay usable on full mixes
- have a smooth top end
- keep Drive low settings genuinely subtle

### Grit

Grit should be transformer-inspired: firmer, more forward, and a little more aggressive. It should add edge and bite, but still remain useful on real material.

Grit should:

- feel more forward than Cream
- add low-mid authority and upper harmonic bite
- avoid becoming a high-mid buzz machine
- be capable of heavier pushed settings

## Autogain Philosophy

Autogain is there to help judgement, not to become another effect.

It should behave like calibrated drive compensation:

```text
saturation -> wet autogain -> mix -> output trim
```

Autogain should not be a live RMS or LUFS follower inside the plugin. LUFS/RMS can be used offline to tune the static compensation curve, but the plugin itself should avoid signal-dependent gain chasing. Live matching can make bass-heavy material feel like it is being compressed or tone-shifted, which is not the goal.

The Mix knob should blend between dry and level-compensated wet. Output trim is the final manual correction after the blend.

## Low-End Handling

BQST should be friendly to bass and 808s, but not at the cost of sounding neutered.

The current low-end safeguard uses a small pre/post low-shelf pair around saturation. This is a good product decision: reduce sub energy before the waveshaper, then restore the broad bass balance afterward.

The intent is not to remove bass. The intent is to stop bass from producing harsh broadband buzz.

If this needs refinement later, prefer a drive-dependent low guard over a heavy static cut.

## Oversampling

Oversampling should be practical, not performative.

Realtime and render oversampling are separate because that is a useful digital workflow advantage:

- lower latency during tracking or writing
- higher quality when exporting

The default should remain practical for normal use. Do not make high oversampling mandatory for the plugin to sound acceptable. Oversampling should improve aliasing behavior; it should not hide bad saturation tuning.

## Visual Design

BQST should look like a modernized piece of classic rack gear:

- two pink 500-series-style faceplates
- black top utility bar
- cream knobs and buttons
- white/cream panel text
- textured anodized faceplate
- screws, markings, VU meters, and hardware cues
- minimal layout, not skeuomorphic clutter

The current visual identity is:

```text
vintage hardware language + modern minimalism + playful pink faceplate
```

The faceplate should stay distinctive. It should not drift toward generic dark plugin UI or generic analog clone UI.

## UX Principles

BQST should feel like an extension of the user's hand/mouse.

Important behaviors:

- linked controls move both sides
- holding Control temporarily inverts link behavior
- double-click resets controls
- hover value readouts appear for knobs
- explanatory help appears with a consistent delay
- Command-Z/Control-Z should undo the whole gesture, not tiny increments
- Drive `0 dB` should be clean/effectively bypassed
- Mix should not create obvious loudness jumps when Autogain is on
- Global bypass should dim the full rack as one visual layer. The overlay belongs above the rack as a single editor-level component, not inside the rack or the VU meters, so rapid bypass toggles never show separate dimming regions.

The plugin should borrow the best parts of analog gear without copying the clunky parts. Examples:

- analog-inspired VU meters, but with digital routing flexibility
- hardware-style controls, but with autogain
- analog-style L/R controls, but with independent M/S per module
- realtime/render oversampling split, which analog hardware cannot offer

## Presets

Presets should be musically named and practical. They should show the plugin's real use cases:

- clean broad EQ lift
- subtle master polish
- cream glue/density
- grit console push
- M/S air or width

Avoid factory presets that only demonstrate extreme distortion. BQST can do strong saturation, but the first impression should be tasteful and usable.

## What BQST Is Not

BQST is not:

- a full channel strip
- a surgical parametric EQ
- a metering suite
- a wild distortion multi-effect
- a clone of one exact hardware unit
- a purely nostalgic analog cosplay plugin

It is a focused EQ plus saturation processor with a strong visual identity and sensible digital conveniences.

## Future Changes Should Preserve

Before adding a feature, ask:

1. Does it make broad tone shaping or saturation more useful?
2. Does it preserve mastering usefulness?
3. Does it keep the UI clean?
4. Does it fit the two-module 500-series concept?
5. Does it improve workflow without copying analog limitations?

Good future additions might include:

- optional drive-dependent low guard
- better offline calibration tests for autogain
- more factory presets
- a manual/installer/signing workflow
- measured article visuals exported from the real plugin

Risky additions:

- too many saturation modes
- parametric mid bands
- compressor/limiter features
- complex metering pages
- adding controls that crowd the faceplate

BQST should stay focused.
