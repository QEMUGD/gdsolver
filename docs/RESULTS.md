# Results

The tables are cold regression runs of the v0.6.0 build (2026-10-18) on GD 2.209,
the Steam update of 2026-10-17 —
`python py/cold_regress.py --one-session`, the whole suite inside a single game
session, no plan and no solution file to start from — one with coins
(`--cfg coinroute=1 coins=1`) and one for the end of the level alone. The coin
runs ended every level with every coin, as the game itself counted them. The
repairs and times are those of the runs the regression baseline was taken from.
The same build made for the first 2.209 executable took the same repairs on every
level, and the same path in every round.

Every cell reads **with every coin**, then in brackets **the end of the level
alone**. A repair cell splits into **divergence + search** (see below).

| # | Level | Repairs | Time | | # | Level | Repairs | Time |
|--:|---|--:|--:|---|--:|---|--:|--:|
| 1 | Stereo Madness | 0 (0) | 9 s (6 s) | | 12 | Theory of Everything | 0 (0) | 6 s (6 s) |
| 2 | Back On Track | 0 (0) | 5 s (6 s) | | 13 | Electroman Adventures | 0 (0) | 7 s (7 s) |
| 3 | Polargeist | 0 (0) | 5 s (6 s) | | 14 | Clubstep | 0 (0) | 7 s (7 s) |
| 4 | Dry Out | 0 (0) | 5 s (5 s) | | 15 | Electrodynamix | 0 (0) | 7 s (7 s) |
| 5 | Base After Base | 0 (0) | 5 s (6 s) | | 16 | Hexagon Force | 0+2 (0+2) | 20 s (18 s) |
| 6 | Can't Let Go | 0 (0) | 5 s (5 s) | | 17 | Blast Processing | 0+1 (0+2) | 10 s (11 s) |
| 7 | Jumper | 0 (0) | 6 s (6 s) | | 18 | Theory of Everything 2 | 0+1 (0+1) | 11 s (11 s) |
| 8 | Time Machine | 0 (0) | 6 s (6 s) | | 19 | Geometrical Dominator | 0+2 (0+1) | 34 s (25 s) |
| 9 | Cycles | 0 (0) | 6 s (6 s) | | 20 | Deadlocked | 0+2 (0+3) | 1 m 20 s (51 s) |
| 10 | xStep | 0+1 (0) | 11 s (8 s) | | 21 | Fingerdash | 1+6 (0+2) | 1 m 16 s (1 m 26 s) |
| 11 | Clutterfunk | 0 (0) | 42 s (43 s) | | 22 | Dash | 1+16 (1+15) | 4 m 8 s (3 m 7 s) |

The two numbers in a cell are two separate searches, not a total and a part:
with coins the goal is a different one, so the search takes a different route
from the first round on, and it is not always the longer one.

Against v0.5.0's one-session runs (GD 2.2081), the 22 levels went from 35 to 27
repairs and 580 to 519 s without coins, and from 60 to 33 repairs and 881 to
611 s with them. The large moves: Dash 36 → 17 and Deadlocked 10 → 2 with coins;
Hexagon Force 5 → 2 without them. Against the run of the trend, with coins
Fingerdash went 6 → 7, Geometrical Dominator 1 → 2 and xStep 0 → 1, and without
them Blast Processing 1 → 2. The [changelog](../changelog.md) has what changed.

### The spin-off levels

The official levels of Meltdown, World and SubZero, and The Challenge, run the
same way, one game session per set. World's levels and The Challenge have no
coins, so they have one number. These levels are not in the main game's level
list; the suite reads them from level files (cfg `leveldir=<dir>`, one
`<id>.lvl` per level), which are not distributed here.

| Game | # | Level | Repairs | Time |
|---|--:|---|--:|--:|
| Meltdown | 1001 | The Seven Seas | 0+2 (0+2) | 15 s (13 s) |
| Meltdown | 1002 | Viking Arena | 0+1 (0+1) | 13 s (14 s) |
| Meltdown | 1003 | Airborne Robots | 0+2 (0+1) | 36 s (10 s) |
| World | 2001 | Payload | 1+0 | 6 s |
| World | 2002 | Beast Mode | 0 | 5 s |
| World | 2003 | Machina | 0 | 3 s |
| World | 2004 | Years | 0 | 7 s |
| World | 2005 | Frontlines | 0 | 6 s |
| World | 2006 | Space Pirates | 0 | 7 s |
| World | 2007 | Striker | 0 | 5 s |
| World | 2008 | Embers | 0+3 | 4 s |
| World | 2009 | Round 1 | 0 | 5 s |
| World | 2010 | Monster Dance Off | 0 | 5 s |
| — | 3001 | The Challenge | 1+1 | 8 s |
| SubZero | 4001 | Press Start | 0+2 (0+2) | 50 s (25 s) |
| SubZero | 4002 | Nock Em | 0+5 (1+2) | 7 m 3 s (45 s) |
| SubZero | 4003 | Power Trip | 0+6 (0+6) | 1 m 32 s (1 m 31 s) |

Against v0.5.0, the 17 levels without coins went from 45 to 21 repairs and 378 to
259 s, and the six with coins from 64 to 18 repairs and 706 to 629 s: Nock Em
26 → 5 with coins and 11 → 3 without, Power Trip 27 → 6 with coins and 24 → 6
without, and Press Start 7 → 2 with them. Each of these is one run; the spin-off
levels have no baseline on 2.209 yet, so a level's count is not yet known to be
reproducible here. They ran on the build one fix before the release's, a fix that
changes no plan: the regression's replay suite is byte for byte the same on both.

**Repairs** is how many times the loop had to go back: solve, replay, die,
re-anchor on the game's real state, solve the tail. `0` means the very first
plan the DP produced cleared the level. Each one is split by what the loop's
recorder found when it replayed the dead plan in the model against what the game
did:

* **divergence** — the model and the game had come apart before the death: the
  first difference the recorder found in position or velocity is over 0.01. The
  model is wrong there, and the repair is the game correcting it.
* **search** — the two agreed up to the death. The model, given what the game
  showed, dies there too; the plan was wrong, not the model — typically a route
  that only looked open because the moving geometry past the last replay was not
  yet recorded, or a plan that dies in the model's own walk of it.

On the official levels 26 of the 27 repairs without coins and 31 of the 33 with
them are search; on the spin-off levels 18 of 21, and all 18 with coins. For a
given build the count is normally reproducible: `py/cold_regress.py` prints
each level's count against `data/cold_baseline.json`
(`data/cold_baseline_coins.json` for a coin run), and a change is reported, not
failed. The `[fp]` line each round prints is compared with the baseline run's
separately when a release is accepted.

It is not a fidelity score. It counts what the loop had to do, and that depends
on which corridor the search happens to walk as much as on where the model is
wrong — the search is deepest-first, so touching one rule reorders the frontier
and the run takes a different route. Making the model *more* correct can raise
it: closing a route the model only believed in sends the search off to find the
real one.

**Time** counts everything from the level being built to the solution being
written (the game's own start-up is not in it), on a 16-core desktop; the 22 add
up to 10 minutes with coins and 8.5 without. Each run shared the machine with
one other game: the first 2.209 executable's run, or a custom level. Read the
clock as a guide and not as a contract: it is not what the regression compares,
and a level's time is read from outside the game about once a second.

Almost all of it is the search. Broken down on a run before the section solver
joined the loop, the DP calls were 86 % of the total and 88–93 % on the four
expensive levels; on the ones that finish inside half a minute, most of what is
left is starting the game and loading the level. On a level where section solves
run (Dash with coins, SubZero) a good part of the time is spent searching inside
the game instead.
