# The replay panel

While a solve is running the screen is mostly black: candidate replays run in the fast loop and only the
overlay is drawn. The replay panel uses that dead time. It plays **GD's own record of the deepest verified
attempt** in the free area from the middle of the screen to the top-right, looping, so you can watch how far the
run has really got.

## When it is there, and the keys

It is on by default, **only while a solve runs and the screen is dark** (render off, which is how a solve runs).
The showing of the solution at the end is a replay and the panel is not drawn there; with the screen turned on (F5)
it is hidden -- not emptied: its mode, place, stop and pace are kept, and it comes back as it was when the screen
goes off again. Both ends of a solve empty it, so the next level never starts with the last one's picture.

`Tab` (Geode setting `key-replaypanel`) goes round **the whole verified path** (every stretch the run has got through,
end to end; the default) -> **its last stretch** (`replaypanelspan` ticks, 20 s) -> **off** -> the whole path.
`replaypanel` in `autorun.cfg` picks where it starts: 0 off, 1 the whole path, 2 the last stretch. `F1` does not hide
it: the panel only shows during a solve, and `F1` does not hide the overlay then either.

While the panel is on screen with a path to play (the screen dark), the transport keys are **the panel's**, not the
game's -- the level behind a solve is the loop's own verification replay and has nothing to move:

| key | the panel |
|---|---|
| `F2` | stop / start the replay |
| `F3` / `F4` | step it one tick / ten (a step stops it; held down they repeat) |
| `←` `→` | seek back / forward: a tap is five seconds, holding accelerates |
| `↑` `↓` | its pace, one notch (0.25x ... 16x; the game's spectating notches) |

With the screen on these keys are the game's again (F2 pauses it, the arrows change its speed), as before. The keys
legend on the overlay names whose they are at the moment. The panel keeps its left edge clear of the overlay's own
text (the elapsed time at the end of the first line, the render state at the end of the keys line), moving right when a
line is long, and is held where it is -- marked "[held: the game is busy]" -- while the game takes 40 ms or more over a
frame (a recording pass, a section search, the fast loop on a heavy level), since an animation drawn on frames like
those only jerks.

## What you are looking at

* **The path** is the rows the game wrote as each tick of the deepest attempt ended (`anchors::g_deepest`), not
  the model's guess. Wherever the model was wrong and GD's state was taken instead (a *fixup*), the path is
  already GD's: nothing has to be corrected on the way out. A bright trail runs behind the player, a faint one
  behind that, and a dim green one shows the road ahead up to the frontier.
* **The level** is the table the session opened (`objrects.txt`'s content, not the sliced copy the scene may
  hold), drawn as the boxes the physics reads: solids, hazards (turned ones as the oriented box of `obb.txt`),
  saws, portals, pads, rings, slopes as the surface line with the solid side filled. Decorations and triggers
  are not drawn.
* **The moving parts** come from the run's own recording of the moving geometry (`dp_groups.txt`) and hold each
  object's last recorded row until the next, the rule the model reads that file by. The status line says whether
  that recording belongs to the attempt on screen (`recorded`), to another one (`another attempt's`: the
  objects are then only close), or whether there is none (`as loaded`: they stand where the level loads them).
* **The corrections** are the run's fixups that lie on this path: a small orange tick on the bar under the
  picture for each, and a ring around the player's position as the path passes it (red for a fixup that records
  *GD ends the run here*).
* **The player** is a plain silhouette per mode (cube, ship, ball, ufo, wave, robot, spider, swing) on its
  hitbox, turned with the gameplay rotation and flipped with gravity. They are not the game's art, only enough
  to tell the modes apart.
* **The bar** along the bottom is the stretch being played, with the playhead on it.

Until the loop has had a death there is nothing verified to show, and the panel says so (a level its first plan
clears never has one: the solver keeps a deepest attempt when an attempt dies).

## Settings

| `autorun.cfg` | |
|---|---|
| `replaypanel=<n>` | where the panel starts: 0 off, **1 the whole verified path (default)**, 2 its last stretch. The key goes round all three. Workers launched through `gdtas.worker` get 0 |
| `replaypanelspan=<ticks>` | the length of the last stretch: 4800 (20 s) by default |
| `replaypanelspeed=<x>` | the pace it starts at, to the nearest of the game's spectating notches: 1 is real time (240 ticks a second) |

## What it costs, and what it never does

It reads what the solver has already kept and writes nothing back; it is not part of any recorded run. Off, or
outside a solve, a frame costs a few tests. On, the level and the moving-parts recording are read and parsed once each on a background
thread, through a handle that lets the solver replace or delete the file meanwhile, and a frame is a few hundred
primitives into one `CCDrawNode`, clipped to the panel's box on the CPU (a draw node has no scissor). Measured in
the game: about 0.1 ms a frame on an official level, about 0.3 ms on a custom level with 88,000 boxes and 42,000
moving objects, whose level table takes 0.2 s to read and whose recording (190,000 rows) 0.4 s.

## Looking at it without the game

`replay_panel_geom.hpp` has no game in it, so `tools/panel_geom_test.cpp` builds and checks it alone
(`cmake --build build-dp --config Release --target panelgeomtest`): the readers, the clipping, the rotation
against GD's own oriented-box corners, every mode in every gameplay frame, the clock. With `--render` it draws
the frame the game would draw for one tick of a real run as an SVG:

```bash
build-dp/Release/panelgeomtest.exe --render objrects.txt obb.txt groups.txt dump.csv 10804 panel.svg
```

(`-` for a file you have not got.) The same picture, outside the game and with the model's trace beside GD's, is
the [divergence view](DIVERGENCE_VIEW.md).

## Limits

* Only the verified path is played; the model's tail beyond the frontier is not drawn.
* The partner of a dual player is placed for an unrotated run only.
* The picture is of the boxes, not the artwork, and the camera is its own (the player a third of the way in,
  ten blocks across), not the game's.
* A level whose recording is another attempt's shows its moving parts only approximately; the status line says so.
