# Changelog

## 2.7.1

New:
- RUN input in trigger mode, the default for a new Tetrad: each trigger toggles play and pause,
  as clock modules such as Impromptu Clocked send on start and on stop. The play / pause state
  is saved with the patch. Gate (high plays, low pauses) stays in the right-click menu (RUN
  input), and patches from 2.7.0 and earlier load in Gate: they play as before. Presets from
  2.7.0 (no RUN setting) leave the module's mode as it is. Initialize puts RUN back in trigger
  mode, as on a new Tetrad. Over an external clock, a start plays from the clock's next pulse. A
  new RUN mode is one undo step (Ctrl+Z brings back the mode and play / pause), and the help (H)
  lists the module menu.

Fixes:
- When an external clock stops (no pulse for more than four periods: the transport stops) and
  starts again, the steps after the start keep the last tempo, after two stops in a row too,
  unless the second lasts about as long as the first: that reads as a clock slowed down more
  than four times. Before, the stop was taken as one clock period: the first step after the
  start held its gate for as long as the stop, and the next note came without a new attack. The
  same holds when a patch opens over a clock held high (Impromptu Clocked, stopped) for more
  than two seconds, and at any length when a RUN trigger starts it, even a trigger arriving a
  sample after the clock's pulse. A clock that swings, misses a pulse, pauses briefly, slows
  down up to four times or runs slower than one pulse every two seconds (started after the patch
  opened) plays exactly as in 2.7.0; one that slows down more is followed from its second long
  gap. A change of sample rate while an external clock runs keeps its tempo.
- A patch that opens while its clock strikes on the very first samples plays step 1 on that
  beat. Before, its patterns were loaded 32 samples later and the first beat was silent.
- The engine thread no longer references a std::mutex: the pattern exchange with the panel uses
  a spinlock that the engine only tries, never waits for.

Tests (see README, Tests):
- Module tests of the RUN input against the Rack SDK; a sound comparison with 2.7.0, sample by
  sample, Tetrad Pipe included; patches saved by 2.6.2, 2.6.7 and 2.7.0 opened and played; a GUI
  test of the RUN menu; fuzz tests of the clock input and of patterns; a soak of one hour of
  audio; a bench; coverage of the changed lines.

## 2.7.0

New:
- Drum mode (MODE: Drum): the eight grid rows are fixed voices with their own hits (velocity,
  gate, probability, ratchet and condition per hit), shown row by row in the value lanes (the
  focused row: click its name, or Up / Down), with a level, a probability and a mute per row.
  The generator writes drum parts and Euclid fills the focused row. The track outputs carry 8
  channels, one per row. Patches save the drum part in state format 4; format 3 still loads.
- EXPERT, at the left of the name band as on the Fourfold voices: off, the inspector shows the
  main settings (mode, division, length, octave, gate, scale, seed, density, range, and the
  velocity, gate and probability of the selected steps); on, every setting. Presets and
  Initialize leave the view as it is.
- Tetrad starts with empty tracks (Initialize empties them too): the generator writes a track
  once one of its settings moves, or with NEW SEED. Patches keep their patterns.

Panels:
- Both panels: an amber band across the top with the module logo, the name and the kind of
  module (SEQUENCER, ROUTER), replacing the strip on the left edge; the Fourfold lockup at the
  bottom centre. Panel widths follow 1 HP = 5.08 mm exactly.
- Tetrad: back to 56 HP, so patches made with 2.6.2 keep the Pipe linked. In patches saved with
  2.6.7 (59 HP), slide the Pipe against Tetrad once. HELP sits in the name band. The outputs of
  each track share an amber tab (VCV: inverted background for outputs).
- Tetrad: muted track tabs are dimmed instead of red (tracks 2-4 start muted); the chosen lane
  is underlined in amber instead of filled, so it does not compete with the selected track;
  the follow button is outlined; the root notes are bright in the row labels; the help line
  shows only once there is something to say.
- Pipe: 10 HP (was 12). Its two output columns are amber, with the routes in dark and the
  track number in the track's colour. The output tooltip reads "Routed 1 output" with the
  right-click hint as its description; the Utility tag is dropped (VCV reserves it for basic
  functions).

Fixes:
- Pipe: a bypassed Tetrad now silences the Pipe (its gates used to hang on the last frame).
  Route changes from the menus can be undone.
- Changing the PPQN while an external clock runs keeps the position (the playhead used to jump).
- CLOCK edges are ignored for 1 ms after RESET (VCV voltage standards).
- RESET brings a Drunk track back to the first step of its loop, and trig conditions count
  loops from there; a Drunk track also starts on that step.
- Drum rows (Pipe row outputs): an older note's end no longer closes the row of a newer note.
- The sliders in the value menus and the tempo slider of the module menu can be undone.

## 2.6.7

- Tetrad: the name strip on the left edge, as on the Pipe and the Fourfold voices. The panel
  grows to 59 HP; the header goes, the inspector and the grid reach the top, and the grid rows,
  overview and tabs are taller. HELP sits under the inspector. Patches place the Pipe 3 HP
  further right.
- Pipe: each output shows its route under the jack, as a tag with the track number on the
  track's colour (the colours of Tetrad's track tabs) and the signal (PITCH, VEL, GATE, TRIG,
  R3 GATE...). Nothing is cut any more; the output number sits small at the top left.

## 2.6.6

- Pipe: the two jack columns are placed inside the width left of the name band, with room for
  the numbers and labels.

## 2.6.5

- Matte aluminium relief on both panels (faint gradient, recessed zones). The Pipe gets the
  vertical name strip on its left edge with the Fourfold mark; Tetrad keeps its compact header,
  as its 56 HP are fully used.

## 2.6.4

- Each panel shows its module logo and name at the top centre and the Fourfold lockup at the
  bottom left.

## 2.6.3

- Brand colours (amber #FFB03B on #17181C, grey #4A4C56) and icons on the Tetrad and Pipe panels;
  track 1 and active states use the amber.

## 2.6.2

- The plugin slug becomes `Fourfold` (a brand rather than a module name). Patches saved with
  the `TetradSeq` plugin need their modules placed again.
- The inspector field FOLW is renamed TRSP IN: it sets whether the TRANSP jack transposes the
  track, and was easy to mistake for the page follow (F button).

## 2.6.1

- Large PLAY and RESET buttons with icons; PLAY is lit green while playing.
- HELP button at the top right of the panel (replaces the small "?" of the inspector).

## 2.6.0

First public release.

- Tetrad: four-track, 128-step sequencer with a 16x8 grid, an inspector, keyboard shortcuts,
  loop tools, generator, internal or external clock.
- Tetrad Pipe: 16 outputs routed by right-click, drum rows for Poly tracks.
