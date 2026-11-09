# The RUN input menu (2.7.1), on a Tetrad saved by 2.7.0. Module coordinates (from Tetrad's top-left
# corner): a right click on the name band opens the module menu there; "RUN input", its last
# submenu, is 335 px lower; its items open to the right of the menu, which widens with the current
# choice (x 992 is inside the submenu either way).
snap s0_opened
runmenu() { xm click 560 17 3; sleep 0.4; xm move 612 352; sleep 0.4; xm move 992 352; sleep 0.4; }
runmenu
shot run_menu
xm click 992 373          # Trigger (default): each trigger plays / pauses
snap s1_trigger
runmenu
xm click 992 352          # Gate: high plays, low pauses
snap s2_gate
xm move 96 346; sleep 0.3; xm move 97 347; sleep 2   # the RUN jack: its tooltip
shot run_tooltip
# Undo and redo of the RUN mode (the last change was Trigger -> Gate), with the pointer on the
# name band: Ctrl+Z, then Ctrl+Shift+Z.
xm key 560 17 z 4
snap s3_undo
xm key 560 17 z 5
snap s4_redo
# Bypass (Ctrl+E) and back, Randomize (Ctrl+R), each on the module; Duplicate from the module
# menu (over Tetrad, Ctrl+D duplicates the loop, as in 2.7.0), 178 px under the right click.
xm key 560 17 e 4
snap s5_bypass
xm key 560 17 e 4
xm key 560 17 r 4
snap s6_randomize
xm click 560 17 3; sleep 0.4; xm click 608 195; sleep 0.4
xm move 950 200; sleep 0.4; xm click 950 200   # where the copy follows the pointer, drop it
snap s7_duplicate
shot after_duplicate
# The help (key H), for a look: its RUN lines.
xm key 300 100 h; sleep 0.8
shot help
xm click 300 100
