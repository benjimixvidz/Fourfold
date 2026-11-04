# Design notes

## Interface principles

1. **The value is the control.** No virtual knobs: every value is edited where it is shown.
   Numbers are dragged vertically (Ctrl for fine steps) and reset by double-click; lists and
   switches act on a click; right-click lists every value or opens a text entry. These are
   Rack's own conventions for parameters.
2. **No hidden layer.** No shift layer and no pages of settings: every setting of the selected
   track is visible at once. Editing several steps is done by selecting them, and the selection
   is the only mode. It is drawn as a frame with a corner mark and cleared with Esc.
3. **Steps are edited in place.** Notes and bars are painted on the grid; the selection
   fields edit every selected step, relative to its own value.
4. **Undo instead of confirmation.** Every edit, generator included, is one entry in Rack's
   history (Ctrl+Z / Ctrl+Shift+Z). Nothing asks for confirmation.
5. **Help in context.** A help line describes what is under the pointer; `H` shows every
   gesture and shortcut over the panel, as does the HELP button in the name band.

## Layout

- **Name band (top):** the module logo, the name, the kind of module (SEQUENCER, ROUTER) and,
  on Tetrad, the HELP button; the Fourfold lockup sits at the bottom centre.
- **Inspector:** transport (large PLAY and RESET buttons, tempo, clock source, PPQN),
  track settings,
  root and scale, generator with its actions (NEW SEED, EUCLID with HITS and ROT, CLEAR),
  selection fields, help line.
- **Grid (right):** track tabs (MONO, POLY, or MUTE in red), lane tabs, the 128-step overview
  (loop window, handles, visible page, playhead), the 16x8 grid, note names, markers for notes
  above or below the visible rows.
- **Jacks:** one row at the bottom: clock, reset, run and transpose inputs, then the on switch
  and the three outputs of each track.

## Brand

Palette: background #17181C, amber #FFB03B (brand, track 1, active states), grey #4A4C56 (pads
and neutral elements). Icons in `docs/brand/`. Each panel shows its module logo and name at the top centre and the
Fourfold lockup at the bottom left (`res/brand/`, lettering lightened for the dark panel).

## Readability

- Text contrast is at least 4.5:1 and state contrast at least 3:1 against the dark
  backgrounds (WCAG 1.4.3 and 1.4.11).
- No state relies on colour alone: active steps are filled, the selection has a frame and a
  corner mark, the playhead has a lighter column and a marker in the overview, steps outside
  the loop are darker, a muted track says MUTE.
- Targets: pads 32x27 px, tabs 54x20 px, inspector fields 72x14 px over their whole width.
  Fields are short so that every setting fits; right-click and the keyboard cover the rest.

## Keyboard handling in Rack

- Shortcuts are hover keys: they act while the pointer is over the module. Keys Rack uses
  itself (Delete, Ctrl+C, Ctrl+V...) are only taken when there is a selection to act on.
- Rack keeps the press of arrow keys for scrolling the rack and only sends `RACK_HELD` to
  modules. Tetrad treats the first `RACK_HELD` as the press, repeats after 0.4 s, and holds the
  rack view still while an arrow acts on the module.
- Held letters (H, M, Space...) act once.

## Performance

- **Audio thread.** One sample per `process()` call. The sequencer skips samples where nothing
  can start or end (no step, ratchet or note-off due). The CV frame is only rewritten when an
  event arrives or a retrigger countdown runs. Parameters and the pattern exchange with the UI
  are checked every 32 samples; the engine only takes the pattern mutex with `try_lock`.
  Measured with Rack's CPU meter: 0.2 % for Tetrad and 0.1 % for the Pipe.
- **UI.** The panel copies the patterns only when their serial number changes. Pads are grouped
  by look and each look is filled with one NanoVG path; the overview bars and the loop window
  use the same approach.

## Saved data

- Parameters are Rack parameters, indexed by their position in the core table
  (`core/params.h`). Entries never move; unused entries are kept as reserved.
- Patterns are saved in the patch as a line-based text document (`core/state.h`). Unknown
  lines are ignored, and a document that fails to parse leaves the patterns untouched.

## Roadmap

- Step editing: per-step octave, micro-timing, glide.
- Patterns per track with chaining.
- Performance view: mutes, solos, fills.
- Recording from a gate and V/oct input.
- Modulation lanes on the Pipe.
- Chord and arpeggiator modes.
