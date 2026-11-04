# Fourfold design system

Decided on 2026-11-04 from the proposals canvas (https://claude.ai/artifact/ReXv3JxFxMtxM276sVV9ym).
Applies to every Fourfold module (Monad, Dyad, Triad, Tetrad, Tetrad Pipe) and to the website.

## Feel

Raw and playful. Pads, blocks and frank colours; a groovebox rather than a laboratory.

## Palette

| Role | Colour |
|---|---|
| Panel background | #17181C (top of a light vertical gradient #1A1B20 to #15161A) |
| Zones (recessed) | #1D1E24 |
| Displays | #0C0D10 |
| Knobs, neutral elements | #4A4C56 |
| Brand, pointer marks, active states, outputs | #FFB03B |
| Text | #DEDEDA; on amber #17181C |
| Secondary text | #8C8E98 |

One accent only: every module is amber. Voices are told apart by their logo and name.

## Typography

IBM Plex Mono (the lockup's face) everywhere, three sizes:

| Use | Size, weight |
|---|---|
| Module name | 34 px, 700, letter-spacing 0.06 em |
| Labels | 11 px, 500, uppercase |
| Values | 13 px, 500 |

## Name treatment (A2)

The module name runs vertically along the left edge, bottom to top, amber, very large; the
module logo sits above it, the Fourfold mark below it. The strip is 46 px wide on 18 HP. The
name is the most visible element of the panel.

## Material and light (B2)

Matte anodised aluminium with light relief: a faint vertical gradient on the panel, zones
slightly recessed (inset shadow), knobs with a soft drop shadow and a highlight at the top,
jacks recessed. One soft light from above for every element. No bevels, no knurling.

## Controls (C)

- Knobs in three sizes by importance: principal 72 px (one per voice), current 46 px, fine
  30 px. Thick amber pointer, no printed scale (Rack shows the value on hover).
- Buttons are rounded squares, the pads of the logos (radius about a quarter of the side).
  The button is its own light: amber when on, dark with an amber dot for momentary triggers.
- Selectors: a display (label left, value right, click lists the values) or a 3-4 position
  segment when the positions fit.
- Jacks: inputs plain; outputs on an amber tab.

## Voice layout (D3)

Eight controls on the panel: the principal knob, a wave selector, six current knobs, and the
trigger / option buttons. A display at the top shows the pitch sweep and the envelope live and
the current preset's name. Every other setting lives in an "Expert" submenu of the module's
context menu (sliders and typed values).

The eight controls are chosen after the Microtonic A/B, from what changes the sound most.
Starting list for Monad: FREQ, SWEEP, DECAY, TONE, NOISE, DIST, LEVEL, WAVE.
