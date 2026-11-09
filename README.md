# Fourfold

<img src="docs/brand/fourfold.svg" width="48" alt="Fourfold"> <img src="docs/brand/tetrad.svg" width="48" alt="Tetrad"> <img src="docs/brand/tetrad-pipe.svg" width="48" alt="Tetrad Pipe">

[VCV Rack 2](https://vcvrack.com) plugin: Tetrad, a four-track step sequencer, and Tetrad Pipe,
its routing expander.

![Tetrad and Tetrad Pipe in VCV Rack](docs/vcv-rack.png)

| Module | Width | Role |
|---|---|---|
| **Tetrad** | 56 HP | Four tracks of up to 128 steps, edited on a 16x8 grid, with an inspector for every setting |
| **Tetrad Pipe** | 10 HP | 16 outputs, each routed by right-click to any signal of any track |

## What's new in 2.7.1

- **RUN input in trigger mode**, the default for a new Tetrad: each trigger plays or pauses, as
  clock modules such as Impromptu Clocked send on start and stop. Gate mode stays in the
  right-click menu, and patches from 2.7.0 and earlier keep it: they play as before.
- **External clock restart**: after the clock stops (more than four periods without a pulse),
  the steps keep the last tempo, each with its own attack.
- **First beat**: a patch that opens while its clock strikes plays step 1 on that beat.
- The engine thread no longer references a std::mutex. Each of these changes is tested (see
  Tests).

Details in [CHANGELOG.md](CHANGELOG.md).

## Features

- Four tracks, Mono (one note per step), Poly (up to 7 notes per step) or Drum (eight rows, one
  fixed voice each, with their own hits).
- 128 steps per track (8 pages of 16), with a loop window (start + length) that can wrap.
- Steps hold scale degrees: changing the root or the scale re-harmonises the pattern.
  29 scales.
- Per step: notes, velocity, gate (up to a tie), probability, ratchets (x1 to x8) and trig
  conditions (A:B, not A:B, first loop, PRE, not PRE).
- Per track: division (1/1 to 1/64, triplets), direction (forward, backward, ping-pong, random,
  drunk), swing, octave, transpose, velocity / gate / probability scaling.
- Generator: seed, density, range, voices, ratchet and condition amounts; Euclidean fill.
- Loop tools: expand, condense, flip, rotate, duplicate, clear.
- Internal clock (20 to 300 BPM, tap tempo) or external clock (1 to 48 PPQN).
- Every edit goes through Rack's undo history.

## Interface

![Tetrad panel](docs/vcv-panel.png)

The left half is an **inspector**: transport, the selected track's settings, root and scale,
the generator, and the fields of the selected steps. It shows the main settings; **EXPERT**, at
the left of the name band, shows every setting (direction, swing, start, transposition,
generator voices, ratchets and conditions, the conditions of the selected steps...). A new
Tetrad starts with empty tracks: the generator writes a track once its SEED, DENS or another
of its settings moves (or NEW SEED, key `S`). The right half holds the track and lane
tabs, an overview of the 128 steps with the loop handles, the 16x8 grid and the note names of
the rows. A help line at the bottom of the inspector describes what is under the pointer, and
the **HELP** button (in the name band, or key `H`) shows every gesture and shortcut. PLAY and RESET
are the two large buttons at the top left of the inspector.

### Mouse

| Where | Gesture | Effect |
|---|---|---|
| Grid | click | NOTE lane: note on / off. Other lanes: set the value at the clicked height |
| | drag | paint notes or bars; the first pad decides between drawing and erasing |
| | Shift+drag | select steps (Shift+click extends the selection) |
| | Ctrl+click | add or remove one step from the selection |
| | right-click | step menu: select, toggle, ratchet, condition, clear, paste, loop tools |
| Note names | drag or wheel | scroll the visible notes |
| Overview | click | show that page (turns Follow off) |
| | double-click | loop on that page |
| | drag a handle / the inside | move the loop start or end / move the loop |
| Inspector | drag a number | change it (Ctrl: fine) |
| | double-click a number | default value |
| | click a list or a switch | open the list / toggle |
| | right-click | every value; numbers also get a slider and a text entry |
| Tempo | drag · double-click | change (Ctrl: 0.1 BPM) · 120 BPM |

With steps selected, the selection fields (VEL, GATE, PROB, RTCH, COND, A, B, NOTES) edit
every selected step at once. When the selected steps differ, the field shows a range
(`64-100`) and a drag moves every value by the same amount.

### Keyboard

Shortcuts work while the pointer is over the module. Letters follow the keyboard layout;
digits and arrows use their position. Without a selection, Delete and Ctrl+C keep their Rack
meaning (delete or copy the module).

| Key | Action |
|---|---|
| Space · T | play / stop · tap tempo |
| 1 2 3 4 | select a track |
| N V G P R C | lane: note, velocity, gate, probability, ratchet, condition |
| M · F · H | mute the track · follow the playhead · help |
| E · S | Euclidean fill of the loop · new generator seed |
| Left / Right | previous / next page; with a selection: move the selected steps |
| Shift+Left / Shift+Right | rotate the loop |
| Up / Down | with a selection: transpose (NOTE) or change the lane value; otherwise scroll notes |
| Shift+Up / Shift+Down | by an octave / by 10 |
| Page Up / Page Down | previous / next page |
| Ctrl+A · Esc | select the loop · clear the selection |
| Ctrl+C · Ctrl+X · Ctrl+V | copy · cut · paste at the selection |
| Ctrl+D | duplicate the loop after itself and double its length |
| Delete | clear the selected steps |
| Ctrl+Z · Ctrl+Shift+Z | undo · redo (Rack history) |

Arrow keys and Page Up / Page Down repeat when held. Over Tetrad, arrows act on the module and
the rack view stays still; elsewhere they scroll the rack as usual.

### Loop tools

In the step menu and the module menu, applied to the loop of the selected track:

| Tool | Effect |
|---|---|
| Expand | spreads the steps to every other step, doubles the length and halves the division: same music, twice the resolution |
| Condense | the reverse: keeps every other step, halves the length, doubles the division |
| Flip | reverses the step order |
| Rotate left / right | shifts the steps by one inside the loop |
| Duplicate | copies the loop after itself and doubles its length |
| Euclid | spreads HITS pulses over the loop, rotated by ROT, on the bottom visible row |
| Clear | turns every step of the loop off |

![Help overlay (HELP button or key H)](docs/vcv-help.png)

## Jacks and clock

| Jack or control | Role |
|---|---|
| PLAY | play / stop (lit green while playing, on by default). Stop goes back to step 1 |
| RESET | back to step 1 |
| CLOCK | external clock at the selected PPQN (1, 2, 4, 8, 12, 24, 48; default 1). Unpatched: internal clock at the inspector tempo |
| RESET | rising edge: back to step 1 |
| RUN | trigger (default): each trigger toggles play / pause. Gate (right-click menu, RUN input): high plays, low pauses; patches from 2.7.0 keep it. Unpatched: follows PLAY |
| TRANSP | 1 V/oct, rounded to semitones, added to the tracks with TRSP IN on |
| T1 to T4 | track on / off |
| V/OCT, GATE, VEL | per track: 0 V = C4; 10 V gates; velocity 0 to 10 V. Polyphonic in Poly mode (up to 7 channels); 8 channels in Drum mode, channel n = row n (V/OCT: 0 V plus n - 1 semitones) |

Most Rack clock modules send one pulse per quarter note on their main output, which is the
default resolution. Click the PPQN field of the inspector to change it. Over an external
clock, a step that falls on a pulse fires exactly with it, even when the clock slows down; the
transport stops after four periods without a pulse, and when the clock starts again the steps
keep its last tempo (a stop is not taken as a period). Steps between two pulses are placed from
the last measured period: when the clock speeds up, a pulse that comes early skips the step
that was still due before it. A clock of 4 PPQN or more puts every 1/16 step on a pulse.

RUN takes the short pulse that clock modules such as Impromptu Clocked send on start and on
stop: each pulse toggles play and pause. Tetrad starts playing, as these clocks do by default,
and the play / pause state is saved with the patch. Over an external clock, a start plays from
the clock's next pulse, at the clock's last tempo. If they ever get out of step (Tetrad paused
while the clock runs), set RUN input to Gate and back to Trigger in the right-click menu while the
clock runs: a new mode starts playing. For a clock that sends a run
gate instead, set RUN to Gate in the right-click menu. Patches made with 2.7.0 and earlier load
in Gate and play as before; presets from 2.7.0 leave the module's mode as it is.

## Tetrad Pipe

![Tetrad Pipe](docs/vcv-pipe.png)

Place the Pipe directly to the right of Tetrad; its light shows the link. Each of its 16
outputs is routed by right-click:

| Menu | Choices |
|---|---|
| Signal | None, Pitch (V/oct, poly), Velocity (poly), Gate (poly), Trigger (1 ms per note), Row gate, Row trigger, Row velocity, Clock, Reset |
| Track | 1 to 4 |
| Row | 1 (bottom grid row) to 8 |

Each grid row of a Drum track (and of a Poly track: scale degrees 0 to 7 at the default scroll)
has its own gate, trigger and velocity. "Route a track as drums" in the module menu sends the
velocities of rows 1-8 of a track to outputs 1-8 and their gates to outputs 9-16. The default
routing is pitch, velocity and gate of the four tracks on outputs 1-12, triggers of tracks 1
and 2, clock and reset. The Pipe adds one sample of latency (Rack expander messages).

## Drum mode

MODE set to Drum turns a track into eight drum rows, row 1 at the bottom of the grid. Each row
is a fixed voice: no notes, no scale, no transposition.

- The NOTE lane becomes **HITS**: click or paint a pad to place a hit on that row.
- Each hit has its own velocity, gate, probability, ratchet and condition. The VEL, GATE, PROB,
  RTCH and COND lanes show the values of one row, the **focused row**, named in the lane tab
  (VEL R3). Click a row name at the right of the grid, or press Up / Down, to focus another.
  The selection fields of the inspector and the step menu (right-click) also act on that row.
- Per row: a **level** (velocity scale, up to 200 %), a **probability** scale (ROW, LVL and PROB
  in the inspector, in place of the root and scale) and a **mute** (the square after the row
  name).
- The generator writes drum parts on a Drum track: kick on the beats, snare on the backbeat,
  hats on the eighths and off-beats, sparse percussion on rows 5 to 8. Euclid (key E) fills the
  focused row.
- The outputs carry 8 channels, one per row; the Pipe's row outputs give each row on its own
  jack.

## Building

Requirements: the [Rack SDK](https://vcvrack.com/manual/Building#Building-Rack-plugins) 2.x,
a C++20 compiler, `make` and `jq` (without `jq`, the Makefile reads the slug and version with
`python3`).

```bash
make RACK_DIR=/path/to/Rack-SDK          # plugin.so
make RACK_DIR=/path/to/Rack-SDK test     # core unit tests
make RACK_DIR=/path/to/Rack-SDK dist     # dist/Fourfold-<version>-<arch>.vcvplugin
make RACK_DIR=/path/to/Rack-SDK install  # copies the package into the Rack user folder
```

## Tests

Run from the repository root, with `RACK_DIR` set as for building.

| Command | What it checks |
|---|---|
| `make test` | Unit tests of the core, without Rack: timing, swing, ratchets, trig conditions, directions, Drum mode, editing, the clock input (restart after a stop, two stops in a row, gaps of exactly four periods, a clock slowing by six, a slow clock, a new sample rate), CV voices, the pattern save format. Three fuzz tests: random tempo, seeks, block sizes and settings; the clock input with random gaps, RUN starts, PPQN changes and silences; random patterns, with notes paired note by note and the saved state round-tripping |
| `make test-sanitize` | The same under AddressSanitizer and UndefinedBehaviorSanitizer, with strict warnings as errors |
| `make check` | The plugin compiled with warnings as errors, then the two above; with `CHECK_RT` naming a real-time checker (the maintainer's), also no blocking lock in the module code and no I/O in the core |
| `make test-module` | The real Tetrad module, linked against the Rack SDK and driven sample by sample: RUN in trigger and gate mode, the start-up guard, a start waiting for the clock, a start just after the clock's pulse, a patch opening over a clock held high, the first beat, saved state, presets and pastes (presets from 2.7.0 keep the mode), menu, tooltip and undo, Initialize, the panel's pattern lock never blocking the engine, output levels and pulse widths, input thresholds, NaN and infinity on every input, the 1 ms CLOCK hold-off after RESET, sample rates up to 192 kHz, removal, and no memory allocation in `process()` |
| `make compare-2.7.0` | The sound against the published 2.7.0 (tag `v2.7.0`): the same scenarios played by both versions, every output of every sample hashed. Identical: internal clock, RUN gate, thresholds, NaN, external clock with RESET, PLAY, swing, slow-downs, missing pulses, brief stops, gaps of exactly four periods, Tetrad Pipe (default and drum routes, saved routes), settings moved during playback (as MIDI mapping does), and an opening after its first beat. Different by design: the first beat of a patch opening on a clock (2.7.1 plays it) and the restart after a real stop (2.7.1 keeps the tempo). It also opens and plays patches saved by 2.6.2, 2.6.7 and 2.7.0 (`tests/fixtures/`): they play exactly as with 2.7.0. `EXTRA_PATCHES="a.vcv b.vcv"` adds your own patches |
| `make gui-test` | The RUN input menu, its undo and redo, bypass, Randomize and Duplicate, in a throwaway Rack (its own user folder; a nested X server when no monitor is on), driven by synthetic events, the saved state checked. Needs Rack 2 Free (`RACK_APP=/path/to/Rack2Free`) and X11 tools: xrandr, xdotool, xwd, ffmpeg, zstd, Xephyr |
| `make soak` | One hour of audio, computed in per-sample blocks, the clock stopping now and then, RUN starts, settings moving, resets: every note paired and ended |
| `make bench` | The real-time budget of the core: regeneration, loading a bank, render cost per sample |
| `make coverage` | The lines changed since 2.7.0 that the tests run (gcov), against a 90 % bar |

Results for 2.7.1: all green but `make coverage`, which reports 83 % of the lines changed since
2.7.0 against its 90 % bar (accepted for this release): the rest is panel code that only runs in
a window (Tetrad Pipe's menus, labels and undo, only renamed for C++20; the RUN input menu,
checked by `make gui-test`, and a help line) and one defensive check that cannot be reached. The
2.7.1 features alone (RUN input, clock restart, first beat) are 96 % covered. The core renders
in about 0.05 µs per sample (0.25 % of the budget at 48 kHz). Tetrad was also played inside
Rack's engine with clock modules (Impromptu Clocked and Clkd, VCV SEQ3, SickoCV Clocker, CVfunk
Syncro) at 48 kHz, and with Clocked also at 44.1 and 96 kHz. It builds without a compiler
warning for Linux x64, Windows x64, macOS x64 and macOS arm64 with the
[VCV plugin toolchain](https://github.com/VCVRack/rack-plugin-toolchain).

## Source layout

| Path | Content |
|---|---|
| `core/` | sequencer engine, patterns, parameters, scales, grid model, editing operations, clock follower and CV voices, pattern save format. No Rack dependency |
| `src/Tetrad.*` | the Tetrad module: transport, parameter and pattern exchange with the UI, outputs |
| `src/TetradWidget.cpp`, `Inspector.*`, `GridPanel.*`, `Actions.cpp`, `ui.hpp` | the Tetrad panel, keyboard shortcuts, menus and undo |
| `src/Pipe.cpp`, `pipe.hpp` | the Pipe expander |
| `tests/core_tests.cpp` | unit tests of the core, fuzz tests included |
| `tests/module_tests.cpp` | tests of the Tetrad module against the Rack SDK |
| `tests/render_dump.cpp`, `compare_2.7.0.sh`, `fixtures/` | sound comparison with 2.7.0, patches saved by earlier versions |
| `tests/gui/` | the GUI test in a throwaway Rack |
| `tests/bench.cpp`, `soak.cpp`, `coverage.sh` | real-time budget, one-hour endurance, coverage |
| `docs/DESIGN.md` | interface principles, performance notes, roadmap |

Threads: the engine thread owns the sequencer. The UI edits a shared copy of the patterns under
a spinlock that the engine only tries (`try_lock`, every 32 samples) and never waits for.
Patterns are saved in the patch as a small text document (`core/state.h`); parameters are
regular Rack parameters.

## License

[GPL-3.0-or-later](LICENSE).
