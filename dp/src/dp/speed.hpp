#pragma once
#include "dp/prelude.hpp"

namespace dp {

// Tolerance for the "gap between the foot and the surface" within which the player
// still counts as standing (the static-surface part; a moving surface adds + |dcy|
// -- see the note at the support test).
//
// **0.6 is far too wide.** GD's grounding IS the collision test (do the boxes
// overlap?), so if the surface is below the foot the player falls unless touching.
// Measured (lv16 t=15,686, where it moves onto the thin floor uid7859 whose top is
// 419.95 at x=24,015):
//   GD     y 435.000 -> 434.952 (one tick of gravity) -> lands at 434.950
//   model  y 435.000 unchanged (the 0.05px gap fell inside 0.6)
// and it runs 0.05px too high from then on. --supporttol A/Bs it.
inline double kSupportTol = 0.01;
constexpr double kDx = 1.29825;
// ...but GD does NOT step by the float32 nearest to 1.29825. Read straight out
// of the dump's first increment (x(1) = 0, x(2) = 1.29825044) on lv1/3/7/10/11
// and lv12 alike, GD's per-tick advance is the float32 1.2982504367828369 --
// four ULPs above (float)1.29825 = 1.2982499599456787.
// Four ULPs sounds like nothing; it is not. The error accumulates over the
// level and lands on contact boundaries: lv12 t=3340 has the cube leaving a
// ledge whose edge + half is exactly 4335.0000, GD sits at 4335.0005 (gone) and
// the model at 4335.0000 (still supported, the test being inclusive), so the
// model fell one tick late and stayed a tick behind for the rest of the level.
// Padding the contact tests to absorb this was tried and broke five levels at
// once (see kContactEps); reproducing GD's constant is the actual fix.
constexpr float kDxF = 1.29825044f;
// Speed portals (GameObjectType 20, object ids 200/201/202/203/1334). lv1-14
// contain none, lv15 is the first: id 202 at x=2,379 and id 203 at x=19,939.
// The per-tick advance for each is GD's own x speed divided by the 240 Hz
// physics step. 0.9 and 1.1 are measured here (the dump carries a `speed`
// column): lv15 t=129 -> 130 steps 1.29825 and t=130 -> 131 steps 1.61425
// exactly, and the switch happens on the tick the player's box first touches
// the portal's (x = 2,338.95, portal box left edge 2,353.5, half 15). The
// other three rows are GD's published speeds and are NOT yet verified.
// GD's own speed multiplier at a re-anchor (the dump's `speed` column), or 0
// when the caller did not supply one. See the --start notes.
inline double g_startSpeedMul = 0.0;
// ...and the same table keyed by that multiplier instead of by object id.
// 1.3 was 1.9492188f, "measured on lv15" -- but that measurement was of the
// FLOAT ACCUMULATION, not of the constant. GD advances x with `x += dx` in a
// float32 (advanceX does the same), so the observable per-tick step is the
// constant rounded to the ulp of wherever x happens to be:
//   lv15's 1.3 section sits at x = 19,900..21,650, ulp 0.001953125
//        1.95 / 0.001953125 = 998.4  -> 998 ulps = 1.94921875   (what was read)
//   lv20's 1.3 section sits at x = 4,994..6,700, ulp 0.00048828125
//        1.95 / 0.00048828125 = 3993.6 -> 3994 ulps = 1.9501953125
// and GD's own dumps give exactly those two numbers, averaged over 899 and 855
// ticks respectively. One constant, two readings -- so the constant is
// 468/240 = 1.95, which is also GD's published x speed for 1.3, and the model
// reproduces BOTH readings for free because it accumulates the same way.
// The old value only matched at x ~ 20,000 and drifted 1 px per ~1,020 ticks
// anywhere else; that drift is lv20's wall after the teleport fix (its first
// divergence is a wave section whose dx is exactly this).
inline float dxForSpeedMul(double mul) {
    if (mul < 0.8) return 251.16f / 240.0f;   // 0.7
    if (mul < 1.0) return 1.29825044f;        // 0.9  normal
    if (mul < 1.2) return 1.6142578f;         // 1.1
    if (mul < 1.45) return 468.0f / 240.0f;   // 1.3
    return 576.0f / 240.0f;                   // 1.6
}
// ...and the inverse. Some rules need GD's `speed` multiplier itself (the rotation
// easing while grounded; it is not proportional to dx -- the dx for 1.3 is 1.95,
// not 1.29825 x 1.3/0.9 = 1.875). The thresholds are cut the same way as cubePhysFor.
inline double speedMulForDx(double dxF) {
    if (dxF > 2.2) return 1.6;
    if (dxF > 1.78) return 1.3;
    if (dxF > 1.45) return 1.1;
    if (dxF > 1.15) return 0.9;
    return 0.7;
}
// --seatspeed: a speed as one byte (State::seatSpd), 1..5 for 0.7 .. 1.6, 0 for none.
inline uint8_t speedCodeForDx(double dxF) {
    const double mul = speedMulForDx(std::fabs(dxF));
    return (uint8_t)(mul < 0.8 ? 1 : mul < 1.0 ? 2 : mul < 1.2 ? 3 : mul < 1.45 ? 4 : 5);
}
inline float dxForSpeedCode(uint8_t code) {
    static const double kMul[6] = {0.9, 0.7, 0.9, 1.1, 1.3, 1.6};
    return dxForSpeedMul(kMul[code < 6 ? code : 0]);
}
inline float dxForSpeedId(int id) {
    switch (id) {
        case 200: return 251.16f / 240.0f;   // 0.7  slow      UNVERIFIED
        case 201: return kDxF;               // 0.9  normal
        case 202: return 1.6142578f;         // 1.1  measured on lv15
        case 203: return 468.0f / 240.0f;    // 1.3  = 1.95, see the note above
        case 1334: return 576.0f / 240.0f;   // 1.6            UNVERIFIED
        default: return kDxF;
    }
}
// The CUBE's jump impulse and gravity are speed-dependent too, and not
// monotonically. Measured on lv15 (each twice, at two different places in the
// level, by injecting the cube onto a floor upstream of the speed portal so it
// really passes through it, then pressing):
//     0.9   jump 11.180   gravity 0.216
//     1.1   jump 11.420   gravity 0.215
//     1.3   jump 11.230   gravity 0.216
// A model that keeps 11.18/0.216 everywhere is ~0.24 out on the very first tick
// of every jump in a 1.1 section and drifts from there.
// 0.7 and 1.6 are NOT measured -- they fall back to the 0.9 row, and the first
// level that uses one has to be re-checked. Ball, ship and UFO constants are
// also still 0.9-only; nothing has needed them at another speed yet.
// RINGS scale with the same factor as the jump; PADS do not. Measured at 1.1:
//   yellow ring  11.180 -> 11.420   (= jump, exactly as at 0.9)
//   gravity ring  4.472 ->  4.568   (ratio 1.0214669, the jump's to 7 digits)
//   yellow pad   16.000 -> 16.000   (unchanged)
// So the scale is a property of the "jump impulse" family. The pink ring is
// assumed to be in that family too (same object class) but is UNVERIFIED, and
// so are the ball-mode ring values at speeds other than 0.9.
// THE BALL'S SPIN RATE, in degrees per tick, ready to be staked into
// State::rotRate. From PlayerObject::runBallRotation (@0x38d350, grounded) and
// runBallRotation2 (@0x38d480, airborne):
//
//     m_rotationSpeed = +-120 / (S * 0.20 * V)      grounded
//     m_rotationSpeed = +-340 / (S * 0.80 * V)      airborne
//     applied as (dt/60) * speed with dt = 0.25, i.e. speed / 240
//
// S is `(scale == 1.0) ? 1.0 : 0.8` -- the scale field is read but used only as
// a boolean, and the 0.8 is a literal, so the mini sprite's 0.6 never reaches
// the arithmetic. That is why mini/full is 5/4 and not 1/0.6.
//
// V is the speed divisor, and it works out to kDxF/dx: at 0.9 it is 1 and the
// rates are the round 2.5 / 3.125 (grounded) and 1.7708333 / 2.2135417 (air).
// Writing it against dx is what makes the whole family look "proportional to
// distance travelled" -- it is not. GD stakes a RATE and spends it per tick, so
// it does NOT change on a slope, where the distance covered per tick does.
//
// Checked against the corpus: grounded full 2.5000 (7,658 ticks), grounded mini
// 3.1250 (2,366), and the 1.1 / 1.3 tiers land on 3.1085 / 3.7551 to four
// decimals. air/ground = 17/24 = 0.7083333 reproduces the measured mode over
// 24,551 airborne ticks.
inline double ballRotRate(bool mini, bool airborne, double dx) {
    const double S = mini ? 0.8 : 1.0;
    const double V = (dx > 0.0) ? ((double)kDxF / dx) : 1.0;
    const double rate = airborne ? (340.0 / (S * 0.80 * V))
                                 : (120.0 / (S * 0.20 * V));
    return rate / 240.0;
}

struct CubePhys { double jump, g; };
inline CubePhys cubePhysFor(float dxF) {
    if (dxF > 1.78f) return {11.230, -0.216};   // 1.3
    if (dxF > 1.45f) return {11.420, -0.215};   // 1.1
    if (dxF > 1.15f) return {11.180, -0.216};   // 0.9
    // 0.7. Measured on lv18 t=15,293 (full size, on flat ground at y=225,
    // speed 0.7): GD leaves at vy = 10.620 and the next ticks step down by
    // 0.212 (10.408 / 10.196 / 9.984 ...). This row used to fall back to 0.9's
    // 11.180 / 0.216, which is 0.56 vy of extra jump on every press in a 0.7
    // section -- lv18 goes 0.7 the moment it takes the size portal at
    // x=20,386, so the whole route after it was planned too high.
    // [2026-08-30] "1.6 is still unmeasured and still falls back here" USED TO
    // STAND ON THIS LINE AND WAS FALSE IN BOTH HALVES. 1.6 is GD's 4x, its dx is
    // 2.400 (measured), and 2.400 > 1.78 -- so it takes the FIRST branch, never
    // this one. And it is not unmeasured any more: calib_speedcal_cube_s4, one
    // press on flat ground, gives jump 11.230 and -0.216/tick for the whole arc,
    // which is exactly what that branch already returned. The 1x control on the
    // same rig gives 11.180 / -0.216, matching the 0.9 row to the digit.
    // So the 1.3 row covers 1.3 AND 1.6, now on measurements of both.
    // What actually falls back here is 0.5x, and that IS the measured row below.
    return {10.620, -0.212};
}
inline double ringScaleFor(float dxF) { return cubePhysFor(dxF).jump / 11.180; }
// OPEN QUESTION (2026-08-01) -- the ball's tap is NOT one number at 1.1:
//   flat ground, upward tap : 3.4260  (lv15 t=10665) = 3.354 * ring scale
//   on a slope, upward tap  : 3.7056  (lv16 t=4306 with m=1.0, t=4314 m=0.5)
//   on a slope, downward tap: 3.4260  (lv16 t=4501)
// The +0.2796 does not scale with the slope's gradient, so it is not the
// surface's own velocity, and it is not a landing-speed effect either (the two
// slope samples came in at -12.714 and -1.935 and gave the same value).
// Until that is understood the FLAT value is used everywhere: it is the one a
// cleared level (lv15) depends on, and re-anchoring can absorb the 0.28 the
// slope case is out by.
inline double ballFlipFor(float dxF) { return 3.354 * ringScaleFor(dxF); }
// ...and the slope case gets a flat +0.2796 on top, which is what the three
// measurements above actually say. lv1-15 contain no slope at all, so this
// cannot move any level that is already cleared.
constexpr double kBallSlopeTapBonus = 0.2796;
// [2026-08-21 r92] The 0.2796 above was **the sp1.1 value**. On the ceilhold
// calibration rig, the bonus in the 6 ball cells (1x, |m| 0.5/1/2 x normal/mini) is
//   0.225 / 0.417 / 0.591  (the same for normal and mini)
// = **0.075 times the ball's slope-exit launch slopeExitVy(|m|,2) at that gradient
//   and that speed** (2.999 / 5.554 / 7.880 x 0.075 = 0.2249 / 0.4166 / 0.5910).
// At sp1.1, exit(0.5)=3.729, so 0.075x3.729 = 0.2797 = the old constant itself
// (the corpus's lv16 t=4,701 reproduces as before). The old form scaled only with
// the gradient via the exit ratio and **did not scale with speed**, so it was 24%
// too high at 1x.
// 0.075 = 0.25 x 0.30 (1/4 of the launch = the cube's bonus rule x the ball's tap
// ratio 3.354/11.180), but the origin of that product is unconfirmed, so it is kept
// as the one measured constant.
constexpr double kBallSlopeExitBonus = 0.075;
inline bool isSpeedId(int id) {
    return id == 200 || id == 201 || id == 202 || id == 203 || id == 1334;
}
constexpr double kYScale = 0.225;
// GD's m_yVelocity lives on a 0.001 grid. Measured over 440,000 ticks of the 19
// verified replays (build/fidelity/fid_lv*.dump.csv, dumped at precision 9):
// 99.8% of every yvel GD reports is an exact multiple of 0.001, and of the 107
// half-grid values that do occur -- all of them the direct output of a HALVING
// (a ball tap, or the ship's leave-the-ground halving) -- not one survives to
// the next tick. So the rounding is not in the impulse and not at end of tick:
// it is inside the gravity/thrust integration, which is also the value GD's own
// position update uses (lv9 t=10571: vy 2.9555 + 0.069 = 3.0245, GD stores
// 3.024 and moves y by 0.225*3.024 = 0.6804, exactly the dy in the dump).
//
// The model integrated in full precision instead, so every halving left a
// ~0.0005 residue that then rode along untouched for thousands of ticks and
// pulled y off by 0.02..0.10 px. That is small enough to hide under the
// fidelity diff's 0.3 tolerance and large enough to decide a contact: on lv9
// the ship's head reached the block underside 0.076 px short and clamped one
// tick late, and on lv10 the same 0.023 px meant it never clamped at all (the
// block's x had gone by), which is the death at t=4026.
inline double qVy(double v) { return std::round(v * 1000.0) / 1000.0; }
constexpr double kCubeG = -0.216, kCubeJump = 11.18, kCubeTerm = -15.0;
// Ball, measured on lv10's ball zone (data/ballpress.dump, portal at x=2295):
//   gravity     -0.1290 / tick   (69 consecutive airborne ticks, no variation)
//   tap         does NOT jump -- it FLIPS gravity and starts the ball moving
//               toward the new floor at 3.354 (t=1820 grounded y=105 vy=0 ->
//               t=1821 upsideDown=1 vy=+3.354, then +0.129 per tick)
//   rest height surface + 15, same as the cube (measured y=105 on the ground)
constexpr double kBallG = -0.129;
constexpr double kBallFlip = 3.354;
// UNVERIFIED: the ball was still accelerating at |vy| = 11.997 when it left the
// zone, so no plateau was observed. The cube's -15 is used as a stand-in; a
// taller ball section is needed to measure the real cap.
constexpr double kBallTerm = -15.0;
// SWING (mode 7, portal type 41; lv22 x=4,641 is the first anywhere).
// Measured on lv22's reference replay (dx=1.6143, update 76):
//   - the tap TOGGLES gravity; holding does nothing extra (push t=3700,
//     release t=3706, accel stayed positive for 100+ ticks after the release)
//   - in the player frame the whole mode is: gravity -0.086/tick, terminal 8,
//     and each tap flips the frame with vp := -0.8 * vp. Both measured flips
//     match to 0.001: -6.424 -> -0.8*(-6.424)-0.086 = -5.053 (world -5.053)
//     and 8.000 (world, flip=1, vp=-8) -> -0.8*(-8)-0.086 = 6.314 (world 6.314)
//   - terminal: vy sat at exactly 8.000 for many ticks
// OPEN: measured at this section's speed only; the grounded
// tap (launch off a surface) untested -- treated as the same toggle.
// [2026-08-20] What used to say "mini untested" was measured on the flyramps
// calibration rig: **the mini swing is 0.129/tick** (= 0.086 x 1.5). In the free
// flight right after a ramp launch GD steps 5.554 -> 5.425 -> 5.296 -> 5.167, i.e.
// by 0.129, while the same rig at normal size steps by 0.086. The same in 3 units
// (m=1 mini n=3/n=1, m=2 mini). No counter-example in the corpus (swing exists only
// in lv22, where everything is vsize=1.0 and 0.086).
constexpr double kSwingG = 0.086;
constexpr double kSwingGMini = 0.129;
inline double swingG(bool mini) { return mini ? kSwingGMini : kSwingG; }
// [2026-08-20 r39] The ship's ladder **while riding a ramp**. Read directly off
// the fixups of hp32's lv16 cold run (6 consecutive ticks at x=23,148..23,156:
// 2.000 2.086 2.172 2.258 2.344 2.430). Same value as the normal-size swing, but
// it has a different origin so it gets its own name -- the mini ship is unmeasured
// (the mini swing is 1.5x, but there is no basis yet for applying that to the ship).
constexpr double kShipRampG = 0.086;
// [2026-08-30] MEASURED TO BE WRONG IN TWO DIRECTIONS, and left alone anyway --
// read this before "fixing" it. calib_forcedrop_swing_f10 (a swing walked into a
// force box, m_force=10, no input):
//   FULL SIZE ignores this cap entirely. vy climbs +0.8140/tick straight through
//     8.140, 8.954, ... to 17.094 at t=281 and is still climbing. The model
//     clamps at 8 unless State::boost is set, and a force box is not one of the
//     setters (those are the ramp launch and the red orb/pad, 889549e).
//   MINI clamps -- but at 9.385, not 8, and it holds there exactly.
// So the cap is not one number and not simply conditional. It is NOT changed
// here because nothing in lv1-22 reaches it this way: lv22's carpet lifts its
// swing at ~0.216/tick over a few ticks and never approaches 8. Loosening a
// clamp on a rig that no corpus level exercises is how a change passes every
// regression and breaks a level later ([[gd-loosening-rule]]). What is needed
// first is a rig where the corpus's own mechanisms (orb, pad, ramp) push a swing
// past 8, so the two readings can be told apart.
constexpr double kSwingTerm = 8.0;
constexpr double kSwingFlipDamp = 0.8;
// --dynhazpad <px>: inflation of the moving hazards' boxes (default 0 = behaviour
// unchanged). The note is where L.dyn.samples gets filled. A provisional
// prescription for lv22's 0.19px miss.
inline double g_dynHazPad = 0.0;

// THE LEVEL'S OWN COMPATIBILITY FLAGS (LevelSettingsObject). The mod writes
// them beside objrects as `levelsettings.txt`; the loader picks the file up by
// name, so nothing has to be threaded through a caller.
//
// Of the 22 official levels only lv22 has any of them set, and it has five
// (lab: flags-levelsettings-branches-2026-09-01.md, collected for all 22 --
// every other level is all-zero). All five are carried here because reading
// four of them costs nothing and the next question about lv22 will want them;
// only fixRadiusCollision has a reader today.
//
// fixRadiusCollision is kA39 at LevelSettingsObject+0x1cf, and
// GJBaseGameLayer::playerCircleCollision (0x211df0) branches on it in its first
// instruction: zero picks the player's RECT against the circle, non-zero picks
// centre distance. See hazardHit.
inline int g_fixRadiusCollision = 0;
inline int g_fixGravityBug = 0;
inline int g_fixNegativeScale = 0;
inline int g_fixRobotJump = 0;
inline int g_dynamicLevelHeight = 0;
// platformerMode is LevelSettingsObject+0x155. GJBaseGameLayer::loadLevelSettings (0x23ad00) copies
// it to the layer (+0x309e) and to both players (+0xb70). Its one reader here is --movelockx.
inline int g_platformerMode = 0;
// --maxplayy <y>: GD's MAX GAMEPLAY Y, the world-y bound above which
// checkCollisions declares the player out of bounds and (after two consecutive
// ticks, latch at player+0xc38) destroys it with a NULL object
// (GJBaseGameLayer::updateMaxGameplayY writes layer+0x36a8: dynamic-height
// levels use max(1200, max object y) + 90 + 300, every other level a fixed
// 2790; the kill gate is checkCollisions+0x999). The mod reads the layer's
// live value and passes it; without the flag the model keeps its old
// "the sky is open" behaviour. Measured on lv22 (bound 3,675 = 3,285 + 390,
// injection-bisected to 0.4px at two x) and lv9 (default 2,790: y=1,010
// lives).
inline double g_maxPlayY = 1e18;
// --goalspare (EXPERIMENT): the two search prunes that stop a body ABOVE the level -- out-of-play
// over g_yBound and escapee-prune over every surface still ahead -- keep one that reaches the goal
// first. Both assume a body up there has nothing left to do, but one that stays alive to the goal
// has cleared the level: a custom level crosses its last wall with a gravity orb and rises untouched to the
// end (GD complete at y=1,635; the model pruned the same plan at y=1,308, and g_yBound 1,410 would
// have pruned it next). A body is kept only when even the fastest rise cannot carry it over MAX
// GAMEPLAY Y before the goal, so GD's own death up there is still modelled (maxplayy) and the
// frontier of escapees the prunes were built against (lv9) does not come back mid-level. Needs
// --maxplayy: without a known ceiling nothing is kept.
// ON BY DEFAULT with the SubZero coin set (the release reports what the defaults solve);
// --no-goalspare turns it off.
inline constexpr int kDefGoalSpare = 1;
inline int g_goalSpare = kDefGoalSpare;
inline double g_goalX = 1e18;   // the search's goal, L.maxX + 60 (cli.hpp)
// --goalgd (default off): the goal is where GD ends the level. PlayLayer::checkForEnd (0x3b4cf0) finishes it on the
// tick the player's x reaches EndPortalObject::getSpawnPos (0x1381a0): the end portal's x - 300 / the camera zoom
// (the layer's +0x1a8), plus instead of minus for a portal flipped in x (m_flippedX). L.maxX + 60 is past the last
// object, and objects can lie well past the portal: one custom level completes at x 23,466.17 against its portal's
// 23,765 where the model's goal was 26,056, another at 4,803.66 against 5,103 (both 300 short of the portal to
// within a tick), the replay of its solution dying 15 ticks after GD's end in the pit the level ends over.
// The portal comes from the mod's levelsettings dump (endPortalX, endPortalFlipX). With no portal (an older
// dump, or a level without one) or a flipped one (this goal test runs rightward), the goal stays L.maxX + 60.
// An End trigger (id 3600) can finish a level earlier; the portal's rule still holds as a point GD does not
// pass, so the model then asks for more than GD does, never less. The zoom is unwitnessed: every completion
// measured ended at camscale 1.
inline bool g_goalGd = false;
inline double g_endPortalX = 0.0;   // levelsettings endPortalX (0 = not in the dump)
inline int g_endPortalFlipX = 0;    // levelsettings endPortalFlipX
inline bool reachesGoalUnderMaxPlayY(double x, double y, double vy, double dx, bool rev) {
    if (!g_goalSpare || g_maxPlayY >= 1e17 || g_goalX >= 1e17) return false;
    if (rev || dx <= 1e-6) return false;
    if (x >= g_goalX) return true;
    const double n = std::ceil((g_goalX - x) / dx);
    // The fastest rise per tick: terminal 15 at the wider integration coefficient (0.25; every
    // mode but the wave uses 0.225), or a wave's line at up to twice the x advance (mini). A time
    // warp scales the x advance and the rise alike, so the ratio holds through one.
    const double rise = std::max(std::max(std::fabs(vy), 15.0) * 0.25, 2.0 * dx);
    return y + n * rise <= g_maxPlayY;
}
// --upsidecoyote (on by default since 2026-09-26; --no-upsidecoyote is the off arm): GD's upside-down cube can still jump for a few ticks after it walks
// off the surface it hangs from. playerIsFallingBugged's classic arm tests `vy > 2g` when flipped
// where the upright arm tests `vy < 2g` -- the flipped side never negates g -- and updateJump
// clears m_isOnGround only when that test says "falling". Walking off a ceiling starts at vy 0
// and gains 0.216 a tick, so og stays set until vy passes 2g (accelSwitchVyForSpeed), and the
// buttons phase, which reads og after the update, jumps from mid-air.
// Measured on a custom level (worker probe, a press swept tick by tick through t=18,414-18,431; the
// cube leaves the underside at 18,419): a press whose effect tick is 18,419 .. 18,427 jumps
// (vy -11.23 on that tick), 18,428 does not; og in the dump drops on 18,428, the first tick after
// vy(18,427) = 1.944 > 1.9224. On the air jump's tick y still moves by that tick's gravity step
// (255.0486 -> 255.1458 at k=2), where a jump off the surface leaves y where it was. The upright
// walk-off test (vy 0 < 2g) says "falling" on the first tick, which is why the upright cube
// cannot do this and why the model's support scan was right for it.
// The ball has the same window (its gravity step is 0.129, so it lasts 15 ticks). Measured on
// a custom level (mini ball, speed 0.9, leaves the underside at t=9,387): a press at every tick k=1..11
// taps with vy -2.6832 -- the mini ground tap -- and flips gravity, with y moved by that tick's
// step; k=12 there is a gravity portal. Frame 0, single body only.
// ONLY WHERE THE LEVEL KEEPS THE BUG: the level setting fixGravityBug (g_fixGravityBug, from
// --levelsettings) is what the "Bugged" in playerIsFallingBugged is about. Counted in the GD dumps of
// 201 old custom levels, every upside-down cube or ball leaving its surface with 0 < vy < 2g: with
// fixGravityBug=0, 560 kept og (the linger) and 1 dropped it; with fixGravityBug=1, 28 dropped it
// and none kept it. Official lv22 sets it, and ungated the flag jumped a mini cube off a moving
// block's edge at t=2,810 where GD had already cleared og -- the one cold wall this flag caused.
// The single fixGravityBug=0 drop (a custom level t=19,481, a mini ball) had its head 1.1 px under the
// band's top, not under an object; it is not covered here.
inline int g_upsideCoyote = 1;
// --lingerhold (off): a button already down (m_jumpBuffered, +0x985, State::jumpBuf) jumps an upside-down cube
// from the og GD left set, on the next tick's updateJump. updateJump tests `+0x985 && +0x9c1` FIRST and runs
// its falling test (playerIsFallingBugged, which is what clears the og above) only on the path without a
// jump, so the vy > 2g that ends a fresh press's window (the press comes AFTER the test, in the buttons
// phase) does not end a held button's. The drop ring is where the two meet: ringJump's type-0x20 arm writes
// neither +0x985 nor +0x9c1 for a cube, so a ring taken in the window leaves both set and the cube jumps
// on the next tick with vy 15 behind it. A custom level (fixGravityBug=0) t=6,150: a mini cube
// walking off block uid 19746's underside (og 1 from t=6,148, vy 0.215 / 0.430) presses at 6,149, the drop
// ring takes the press at 6,150 (vy 15, og 1 still), and GD's row 6,151 is a jump (vy -9.136, y moved by
// 9.136 x kYScale in that same tick, og 0). Swept on the game with one press at P instead (the plan up to
// 6,072 kept): P = 6,153 / 6,154 / 6,155 jump on the next row, P = 6,156 onwards does not (og is already 0
// by then, vy 1.935 > 2g), so the fresh press's own gate stays as it is. Cube only: the drop ring clears
// +0x985 for a ball, and the model has no linger for the other modes' hold.
inline bool g_lingerHold = true;   // on by default (release flags, 2026-10-17); --no-lingerhold is the off arm
// --rawvalues (EXPERIMENT): the gravity step BEFORE the 0.001 grid. The literals the cube branch
// uses (0.216 / 0.215 / 0.212, the ball's 0.129, the robot's 0.194 / 0.195) are GD's steps as
// they show on the grid, and they are right whenever vy is already on it. From an off-grid vy
// they are not: a mini ball's tap leaves at -2.6832, and GD's next vy is -2.813 where
// -2.6832 - 0.129 rounds to -2.812 (a custom level t=9,386, and the drift is where eight of the old
// custom levels' divergences start). GD steps by g x 0.225 (x0.6 ball, x0.9 robot) with g its
// per-speed gravity (accelSwitchVyForSpeed's table) and rounds after; -2.6832 - 0.129357 =
// -2.8126 -> -2.813. Every literal above is that value on the grid, so an on-grid vy is unchanged.
// The ball's is flat across speeds (0.129 measured at all four), so it takes the 0.9 row.
// The same holds for a ring's value, which ringJump sets as a raw product: the ball's gravity
// ring is 4.472 x 0.7 = 3.1304 in GD (a custom level t=18,078 stores 3.13039995), where the model's
// constant was the rounded 3.130 -- on-grid itself, so the next tick rounds the other way.
// On by default (with --padtable); --no-rawvalues turns it off.
constexpr int kRawValuesDefault = 1;
inline int g_rawValues = kRawValuesDefault;
// ...and the other half of the same grid: the launches GD writes ON it. A slope's exit launch
// (6.906 at lv16 t=5,401 / lv22 t=3,806, 2.499 at lv18 t=10,824, the ceiling release, the
// swing portal's 2.762) and a ring's jump x ratio (red 11.18 x 1.38 -> 15.428 at lv22 t=297,
// pink 8.05 / 8.222) come out on the grid, where the model's formulas leave them a fraction off
// (6.905590, 2.499375, 15.4284). With the rounded gravity that fraction happened to round away
// on the next tick; with the raw one it rounds the other way, so under --rawvalues the launch
// is rounded where it is written. A ring's POST multipliers (ball / spider x0.7, the gravity
// ring's x0.5) come after the rounding and stay raw: 3.1304 and the pink ball's 6.0263 =
// q(11.18 x 0.77) x 0.7 are both what GD stores.
// The float cast first: lv17 t=18,572's slope exit is -3.7025 exactly on paper, GD's float of it
// (-3.70250010) rounds to -3.703 where the double rounds to -3.702.
inline double rawQ(double v) { return g_rawValues ? qVy((double)(float)v) : v; }
// --portalunpin (EXPERIMENT): a gravity portal taken by a cube standing on a block leaves y
// where that tick's gravity step put it (step.hpp, at vAtPortal).
// On by default (with --portalpress); --no-portalunpin turns it off.
constexpr int kPortalUnpinDefault = 1;
inline int g_portalUnpin = kPortalUnpinDefault;
// --hazendpoint (EXPERIMENT): the cube's and the flying body's hazard test at the tick's END only,
// not at the samples between its endpoints. The in-between samples pair the old x with a partly
// moved y -- a place the body never was -- and lv9 t=11,830 / 13,078 die on one (the ship's box
// 0.01 px into spike uid 2537 at f=0.75, clear of it in x at the endpoint) where GD lives.
// Sweeping y at the new x instead was tried first and is worse: deathref lost 4 of 42 (the model
// died 112-342 ticks before GD, on (new x, old y) samples -- just as fictional). Endpoints only
// keeps deathref at 42/47 and quick_regress's exact tracking loses nothing (lv9 gains 10,416
// ticks, 8 segments stop dying). kSubSteps = 1 was rejected on 2026-07-31 because lv11's COLD got
// worse on the model of that day; that is the measurement this flag still owes.
// Swept on the game over the modes and shapes the first sweep (165 phases, 10 classes) left out,
// on phases where only an interior sample overlaps: spider 12, swing 65, a turned hazard 51 and a
// saw 16 -- GD killed none. On by default; --no-hazendpoint turns it off.
constexpr int kHazEndpointDefault = 1;
inline int g_hazEndpoint = kHazEndpointDefault;
// --portalpress (EXPERIMENT): a press on the tick a MODE portal fires is the new mode's press. GD
// consumes a press in the buttons phase, after the collision pass that fired the portal (the
// coyote sweep showed the press line P acting at P+1 after that tick's move). The model ran the
// old mode's action in its branch and the portal after it: a custom level t=7,099 cube -> ship, the model
// jumped (11.18, halved by the portal to 5.59) where GD sat as a ship with vy 0 and held from the
// next tick; another custom level t=15,425 cube -> ball, GD tapped (3.354); a third custom level t=1,606 flipped ball -> cube,
// GD jumped (-11.18). Old mode cube/ball, new mode cube/ball/ship/wave: the transitions seen.
// On by default (with --portalunpin); --no-portalpress turns it off.
constexpr int kPortalPressDefault = 1;
inline int g_portalPress = kPortalPressDefault;
// --padtable (EXPERIMENT): a pad's launch from GD's own table instead of the cube's value with a
// ball ratio. GJBaseGameLayer::bumpPlayer (0x217b66) picks a strength by pad type and mode, and
// PlayerObject::propellPlayer (0x39f850) sets vy = float(sign x strength x 16 x mini 0.8), then
// x0.6 (double) for ball, spider and swing:
//   pink (9)  ship 0.35  UFO 0.4  ball/spider 0.7  else 0.65
//   red (34)  ship 0.63 (mini 0.95)  UFO 0.6 (mini 0.98)  else 1.25
//   any other pad (yellow)  1.0
// The model gave every flying mode the cube's pink 10.4: GD launches a ship off a pink pad at
// 5.6 (old custom levels, t=4,391 and t=3,158) and a mini UFO at 5.12 (another custom level, t=13,008).
// The cube's 16 / 10.4 / 20, the ball's 9.6 / 6.72 are this table's values; the swing's x0.6 is
// new (the model had none), and red on ball/spider keeps its x0.6.
// On by default (with --rawvalues); --no-padtable turns it off.
constexpr int kPadTableDefault = 1;
inline int g_padTable = kPadTableDefault;
inline double padTableVy(int type, int mode, bool mini) {
    const bool ship = mode == 1, ufo = mode == 3, ball = mode == 2, spider = mode == 6;
    float s = 1.0f;
    if (type == 9)
        s = ship ? 0.35f : ufo ? 0.4f : (ball || spider) ? 0.7f : 0.65f;
    else if (type == 34)
        s = ship ? (mini ? 0.95f : 0.63f) : ufo ? (mini ? 0.98f : 0.6f) : 1.25f;
    double v = (double)(s * 16.0f * (mini ? 0.8f : 1.0f));
    if (ball || spider || mode == 7) v *= 0.6000000238418579;
    return v;
}
// Fly-rings (was --flyrings, always on since 2026-09-26):
// a SHIP or UFO ring fires one tick before the model's input reaches the
// body. Both modes read the button 2 ticks after the press line (planEdges' latOf), but pushButton
// calls ringJump in the buttons phase of the tick after the press, testing the ring box at that
// tick's end. So GD sets the ring's velocity on the row BEFORE the one the model fired on, runs the
// next tick's update on it, and the press is spent (the UFO does not flap). Old custom levels,
// replayed without fixups:
//   a custom level, t=2,528, ship, gravity ring: GD 4.472 then 4.364 (held); the model fired a tick later
//   another custom level, t=7,319, mini UFO, gravity ring: GD 3.5775 then 3.679; a tick later the model flapped
//     (6.648), moved, and set 3.5775 after the move
// It also gives the pink ring its ship and UFO ratios (ringJump: ship 0.37, UFO 0.42) in place of
// the cube's 0.72: a custom level t=8,678 and another custom level t=11,196, mini ship, GD -3.309 = 11.18 x 0.37 x 0.8.
inline thread_local int g_flyRingNested = 0;
// --flydashring: ...the dash rings (37, and 38 that also turns gravity) take that path too. A custom level's
// gravity dash ring uid 68437 (id 1751, rot -70) and a UFO, the plan's press swept one tick at a time in GD:
// it fires for presses on 10,460..10,464 and not on 10,459 or 10,465, and the row after the press already
// carries the turned gravity at the y the press found -- the buttons phase of that tick, as the impulse
// rings. The model fired it through the late loop a tick later, from the row the flap had moved, for
// presses on 10,458..10,461. On by default since 2026-10-06; --no-flydashring is the off arm.
inline bool g_flyDashRing = true;
// --flypadhead (on by default since 2026-10): a pad that sends a ship, UFO or swing toward its head side hands GD's solid pass
// that vy, and the solid whose head-side face the head has crossed within the flight modes' reach (6 px, now or
// at the previous position) takes the head there: on the face, vy 0. collisionCheckObjects fires the pads in its
// object loop and resolves the solids only after it (the pass-order note), while the model ran its flying
// solids first, on the vy from before the pad, which pointed away from the face. A custom level, t=6,581: a ship
// a spider orb has just moved under the ceiling, upside down, its lower-left corner 0.68 x 3.26 px into block
// 3930; the upside-down yellow pad 2880 above sets vy -16 (GD's padfire), and GD's hitboxtrace has 3930 hit=1:
// y 1,304.75 = the block's top + 15, vy 0 -- then it sits under the ceiling slabs until the next pad, 4 ticks on.
// The model let the pad's -16 carry it away at once. A face that moves on the tick is left alone (unmeasured).
inline bool g_flyPadHead = true;
// --dashframe3 (off, needs --dashclamp): a dash ring met on GD's frame 3 (the player travelling up) takes its slope from the same clamp as on frame 0, with the frame's
// -90 (+0x9c3) put around it, so a ring whose rot is -90 -- along the travel axis -- dashes straight on. Without it the slope is the raw tan(-rot), which the clamp
// turns into tan 70 = 2.747 for a ring at -90 (or infinity): a custom level's UFO-less cube at t=4,542 takes the dash ring uid 8358 (rot -90) with the gameplay
// rotation uid 8364 on the same spot (GD frame 3 from that tick): GD moves y +2.4 a tick for two ticks and x not at all; the model moved y the same and x -6.594 a tick
// (2.4 x 2.747) and was 13 px off the cross axis, then died at t=4,583 where GD dies at 4,624. Only frame 3 is measured; frame 2 (+180) is not.
inline bool g_dashFrame3 = true;   // on by default (release flags, 2026-10-17); --no-dashframe3 is the off arm
// --crossworldx (off): the time warp's and the gravity trigger's crossing queue is read at the player's WORLD x when the frame is turned, not at the frame's own travel
// coordinate (the lists hold the triggers' world x). A custom level's cube is on GD's frame 3 from t=4,542, travelling up at world x 6,571: the model's frame
// coordinate runs 2,772 -> 2,778 and crosses the gravity trigger uid 65 (world x 2,775, x0.8) again, so its cross-axis gravity step drops from -0.216 to -0.173
// at t=4,554 and its trajectory leaves GD's; GD's steps stay -0.216 (the trigger at 2,775 and its partner at 3,075 fired long before, in frame 0).
inline bool g_crossWorldX = true;   // on by default (release flags, 2026-10-17); --no-crossworldx is the off arm
// --ringmemrot (off): applyRotation forgets the rings the body has used (usedOrb / usedOrbOld / usedOrbHist) on every frame change; with the flag they stay (by uid: usedOrb moves into usedOrbOld, as a pointer into the old frame's objects means nothing in the new one), as GD's
// once-per-attempt activation mark does. A dash ring (uid 8358, rot -90) fires at t=4,541 and the body turns into frame 3 on the next tick; the model wipes the ring's
// record there and the press at t=4,560, with the body back over the ring's top edge, fires it again (the dash line from 4,562: vy 0 and the cross axis frozen). Measured
// in GD with the plan flown with and without that press: the rows are identical (vy -3.456, -3.672, -3.888 ... on 4,560.. with no second dash), and the ring is never offered
// to the body again -- no ringJump call for it after the fire although the body overlaps its rect until t~4,560 (orbtrace: calls every tick from 4,530 to 4,541, none after).
inline bool g_ringMemRot = true;   // on by default (release flags, 2026-10-17); --no-ringmemrot is the off arm
// --ringrotbox: the contact a fly ring is fired from (step.hpp flyRingContact, the previous tick's end)
// tests a rotated ring against the player's own rotated box -- GD's collision pass rebuilds the player's
// oriented box for an object that has one (the portal pass's pHalfLive note). The same sweep: with the
// player's rotation (3.4 to 4.1 degrees) the two boxes overlap on exactly the five rows GD fired from
// (tightest margins +0.08 and -0.21 at the two ends); without it the window moves a row early.
// On by default since 2026-10-06; --no-ringrotbox is the off arm.
inline bool g_ringRotBox = true;
// --ringrectgate: ...only after the rect test (step.hpp flyRingContact), as GD's collision pass does
// (collisionCheckObjects, the object pass: the object's rect against the player's, then for an object with an
// oriented box both OBB2D::overlaps1Way, the player's box rebuilt by vfunc 0x578 at that moment). The rect is
// getObjectRect2's: for an oriented object the bound of its turned box, cached at +0x358 and rebuilt when
// setPosition / setRotation mark it (+0x368). A custom level's dash ring uid 2576 (1751, rot 15, own box 36)
// and an upside-down cube spinning past it: GD's seven-press sweep dashes from the row 35.78 px out and not
// from 37.40 (rect 22.05 + 15 = 37.05), where the turned boxes still overlap; another level's ring at -45
// (uid 93, a cube the right way up) is not fired from 44.2 px out, past 25.46 + 15, where the model dashed.
// A ring a Rotate turns (Obj::rotTurned) is left out: a cube the right way up fires a yellow ring turned to
// 340.8 from a row 34.95 px out, past the rect of that angle (33.59), so when GD rebuilds its rect is not
// known. Off from 10-12 10:39 while it was taken for a gravity rule (--ringflipunturned); on again since.
inline bool g_ringRectGate = true;
// --ringflipunturned (OFF since 10-12): an UPSIDE-DOWN cube's touched-ring contact tested with its box unturned.
// It was fitted to the uid 2576 sweep above, which the rect gate accounts for; GD's collision pass rebuilds the
// player's box at its rotation in either gravity (GameObject::updateOrientedBox reads getObjectRotation).
inline bool g_ringFlipUnturned = false;
// --ringheldnow: a turned ring entered with the button held from an earlier tick is met where the player is on this
// tick, not at the previous tick's end (--ringrotground's position, which is the press path's). GD's two fire paths
// (the tick map's ring note): pushButton walks m_touchedRings from the previous collision pass, while
// playerTouchedRing fires during this tick's object pass when the held and buffered copies (+0x989, +0x98a) are set;
// the player's box there is rebuilt (vfunc 0x578) at this tick's position and at the rotation updateRotation left a
// tick ago (it runs after the collisions). A custom level's dash ring uid 93 (1704, rot -45), a cube the right way up
// pressing on 4,741 and holding: GD dashes on 4,743, the first row whose box meets the ring by that rule (+0.09 px;
// -0.20 on 4,742); the model, reading the rows before, never met it.
inline bool g_ringHeldNow = true;
// --spiderpadafter: the pads GD's pass reaches after a spider pad (type 44) that warped the body are judged
// where the pad put it, on the same tick (step.hpp, the pad loop; the order is gdTeleAfter's, as for a
// teleport's landing). The loop used to stop at the warp. A custom level: a cube rides a floor onto a spider
// pad (uid 1553) and is put at 675 under two ceiling blue pads (uids 1554 / 1604); GD's dump has it the
// right way up again on that tick with vy -6.4 (the blue pad's), where the model sat it on the ceiling
// upside down for a tick and fell from the next -- 1.5 px off GD from there on, and the run's first plan
// went another way in GD. On by default; --no-spiderpadafter is the off arm.
inline bool g_spiderPadAfter = true;
// --spiderorbpass: a spider orb fired by a held press goes off inside GD's collision pass (playerTouchedRing's
// ringJump), so the portals the pass reaches after it (gdTeleAfter's order) are met where it put the body, on the
// same tick, and the solid pass that follows the walk seats the body on the solid face it found (vy 0, onGround).
// A fresh press fires it from pushButton after the pass, which is the arrival lv22 shows (onGround 0, vy +-1).
// A custom level: a cube holding the button meets a spider orb (uid 149) and is put under a ceiling at 735 with a
// mini portal (uid 182) there; GD's row has it mini, onGround 1 and vy 0 on that tick (hitboxtrace: the
// ceiling block resolved at size 0.6), where the model took the portal and the landing a tick later.
// On by default; --no-spiderorbpass is the off arm.
inline bool g_spiderOrbPass = true;
// --dashstopdead: a dash stop (id 1829) that ends a dash also clears m_jumpBuffered (collisionCheckObjects: id 0x725
// with +0x9e4 up calls stopDashing, then writes +0x985 = 0), so the button held through the dash is dead until it
// is let go (State::holdDead). A custom level: a cube presses into a dash ring (uid 11681) and holds through the
// stop and a teleport down to a floor; GD's pflags read +0x985 0 from the stop on and it lands without a jump, where
// the model jumped off the landing.
// On by default; --no-dashstopdead is the off arm.
inline bool g_dashStopDead = true;
// --tppinvy: a teleport portal met by a cube or ball that sits on a block keeps the update phase's gravity step
// in vy. GD's update has taken the step, the teleport moves the body inside the collision walk, and the solid pass
// after the walk finds no block at the exit to seat it on; the model's pin had already put vy back to 0. A custom
// level: a ball rolling on a floor at 2,505 is teleported (uid 615) to 1,635; GD's row has vy -0.129 there (the
// ball's step at speed 0.9), the model 0, and it ran 0.13 px high from there on.
// On by default; --no-tppinvy is the off arm.
inline bool g_tpPinVy = true;
// --portalunseat: a mode portal that gives a body sitting on a block a smaller box (a cube turned spider: 30 to 27)
// fires inside the collision walk, before the solid pass, so the pass meets the new box at the height the update
// phase left; with no solid under that box nothing seats the body and it keeps the update's gravity step. The
// model seated the old box first. A custom level: a cube rides a floor onto a spider portal at its edge; GD's
// hitboxtrace has no contact on that tick (box 27 x 27, its foot 1.46 above the floor) and the row is the cube's
// step, vy -0.195, where the model sat on the floor and fell a tick later.
// On by default; --no-portalunseat is the off arm.
inline bool g_portalUnseat = true;
// --pinwarp: the gravity step a body sitting on a block takes in the update phase (step.hpp prePinY, which a pad, a
// ring or a ramp's release leaves it at) is the time warp's, as every airborne step is: vy += g x warp, y += vy x
// warp. The pin took the unwarped one. A custom level under a 0.9 warp: a grounded cube takes a pad and GD's row
// is 1,334.9605 (1,335 - 0.195 x 0.2025), the model's 1,334.9514 (1,335 - 0.216 x 0.225); a ball, 1,274.9765
// against 1,274.9709.
// On by default; --no-pinwarp is the off arm.
inline bool g_pinWarp = true;
// --dashwarp: a dash ring's line climbs by its slope times the warped x step, the step x itself takes (step.hpp,
// x += useDx x warp). The dash branches took the unwarped one, so under a warp the line came out steeper by
// 1 / warp. A custom level under a 0.9 warp: a ball dashes off a ring at -4 degrees; GD's rows climb 0.1227 a
// tick (tan 4 x 1.755), the model's 0.1364 (tan 4 x 1.950).
// On by default; --no-dashwarp is the off arm.
inline bool g_dashWarp = true;
// --swingheldash: a dash ring does not spend the press that fires it (+0x986 stays up while held: GD's pflags through
// a held cube dash and a held ball dash), and the swing's updateJump flips on +0x985 and +0x986 alone, so a body
// that enters a swing portal dashing with that press held flips on the next tick as if pressed, the dash running
// that tick still; the flip clears the press and the dash ends after it. A custom level: a ball dashes with the
// button held into a swing portal (t=11,884); GD's row 11,885 is upside down still on the dash line, 11,886 falls
// toward the ceiling, where the model took the swing's gravity at once and went down.
// On by default; --no-swingheldash is the off arm.
inline bool g_swingHelDash = true;
// --ringride: a ring fired off a seat that rides a moving face leaves from the face's new height, as a jump does
// (step.hpp, jumpRide: the collision pass seats the rider on the face's NEW height and only then does the
// impulse fire). A custom level: a spider on a block easing down (568.587 -> 568.173) takes a ring on the
// next tick; GD's row is the seat 0.414 lower with the ring's vy 5.7554, the model's the old height with the
// same vy, 0.414 off from there on. On by default; --no-ringride is the off arm.
inline bool g_ringRide = true;
// --snapfacebelow: the stair snap's contact (step.hpp, the touch list before checkSnapJumpToObject) does not count a
// solid whose face lies below the foot by more than kSnapFaceBelowEps. GD calls the snap once per solid its
// collision pass resolves, and a face the cube stands clear of is not one. The old test took any face within
// 0.6 either way, which on grid-aligned blocks never mattered. A custom level: a cube rests on two moved blocks
// whose tops are 3180.03 (uid 2202, the one it stands on) and 3179.99 (uid 2201, 0.04 lower); GD's snaptrace
// has one call a tick, for 2202, and keeps it as the snap object into the next landing (90, 60.02 off, the
// `big` stair: +1.0 px). The model called both, ended on 2201, matched nothing and ran a pixel behind GD.
// On by default; --no-snapfacebelow is the off arm.
inline bool g_snapFaceBelow = true;
constexpr double kSnapFaceBelowEps = 0.01;
// --ringrotground (on by default since 2026-10): ...and the late ring loop (every mode but the fly rings' early
// press) does the same, where it projected an unturned player square onto the ring's axes, and it
// places that box where the player was at the previous tick's end, as the fly rings' contact does.
// Two airborne cubes spinning past a dash ring (id 1704, 36 x 36) that does not move (GD's ccl orect
// is its load position), the press swept one tick at a time in GD with the rest of the plan kept:
//   a custom level, ring rot -70, sprite ~254 deg: presses 7,960..7,964 dash (the sprite snaps to the
//     ring on the next row), 7,965 and 7,966 do not
//   another, ring rot -11, sprite ~147 deg: presses 1,095..1,098 dash, 1,099..1,101 do not
// Scored on GD's own rows, only the turned box at the previous row fits all four edges (+0.43 / -1.72
// and +1.77 / -1.43 px with the start-of-tick rotation); at the fire row it misses both last dashes
// (-1.59, -1.50). The unturned square at the previous row fires a tick late on the second (+3.01) and
// misses the first's last dash (-1.19), at the fire row that one too (-3.21). The model without the
// flag fell past the first ring and died on a hazard 20 ticks later.
// NOT the wave: --waveringbox's sweep (below) wants a turned half in (5.86, 6.50], which the wave's own
// 10 x 10 (half 5) misses, so the wave keeps that measured unturned square of half 6 rather than having
// the two rules stacked.
inline bool g_ringRotGround = true;
// --flystepup (default on): a grounded flying body whose foot is below a solid's top face, within the landing
// tolerance, is landed on that face (step.hpp, the flight landing gate), as an airborne one already was.
inline bool g_flyStepUp = true;
// --ringpobb (default on since 2026-10): ...and the late ring loop's rotated ring too, for every mode but the wave
// (step.hpp, its ring box was swept against the unturned test). GJBaseGameLayer::collisionCheckObjects
// (0x214960), after the rect test: when the object's latch (+0x2e8) is set it asks OBB2D::overlaps1Way both
// ways between the object's getOrientedBox and the player's, which updateOrientedBox has just turned by
// the player's rotation. The late loop projected an unturned player square onto the ring's axes. A custom
// level's dash ring (id 1751, rot 26.94, own box 36) and a cube in the air at 116.06 degrees, the model's
// angle equal to GD's: on the ring's axis the cube's turned box reaches 15.2, the unturned square 20.2, and
// the cube's centre is 37.39 out. The model dashed and GD did not, the same death ten attempts running.
inline bool g_ringPobb = true;
constexpr double kWaveRingHalf = 6.0;
// --tporbown: a teleport orb has its own mark of the press, so it fires alongside another ring the same press
// fires (step.hpp, the ring loop: the other ring first, then the orb). PlayerObject::ringJump (0x398c00) spends a
// press with one of three bytes by the ring's type -- +0x98c for type 0x24, +0x98d for the teleport orb (0x2e),
// +0x98b for every other ring -- and refuses a ring only when its own byte is already set. A custom level t=1,158:
// a robot presses inside a spider orb (uid 78) and a teleport orb (uid 153) placed on one spot; GD flips to the
// ceiling and is put at the orb's target (1605, 570) on that tick, the model took the spider orb alone and stayed
// at x 1,502 under the ceiling. On by default; --no-tporbown is the off arm, where one ring is picked.
inline bool g_tpOrbOwn = true;
// --dashrot: ...and a dashing CUBE's angle is the dash line's too, -atan(slope), with no spin (step.hpp,
// the cube's rotation); the release puts it back to 0 (on its own tick, which still moves along the
// line), and nothing turns it until a spin is staked. The cube spun on through the dash at the air rate, so its box came out of the dash at whatever
// angle the spin had reached. Two custom levels, GD's own rows:
//   a horizontal dash ring at t=12,490: rot 0.000 from the ring's tick on, 0.000 on the first tick after
//   the dash (vy -0.216), +1.731 a tick from the next (vy past -0.25). The model's had turned 165 degrees,
//   and a rotated yellow ring 20 ticks on met that box (the two-box test with the player's turn) where
//   GD's upright box missed it, so the model jumped where GD fell;
//   a -45 degree gravity dash: rot 45.000 through the dash, 0.000 on the release's tick (y still on the
//   line), held while vy is 0.216, then -1.731 a tick, flipped (the fall stake with the gravity in force).
// --no-dashrot turns it off.
inline bool g_dashRot = true;
// --dashsprite (off): ...and the cube's angle after the release is not 0. updateDashArt (0x3961c0) starts, on the
// player's two icon sprites (+0x738, +0x750), a CCRepeatForever of CCRotateBy(0.3 s, 288 x time warp x -+1) -- 960
// degrees a second, 16 a scheduler frame, the sign negative while the gravity is flipped XOR reversed -- and
// stopDashing (0x396650, a cube: none of the vehicle flags) stops the actions, reads the first sprite's angle and
// puts the PLAYER at it, so the box the next ticks run with is turned by the spin the dash accumulated. The action
// manager steps once a scheduler frame (4 physics ticks at the normal time warp), the step that finds the action
// new counts for nothing, and the sprite is read in the frame of the stop after that frame's step:
//   angle = 16 x (frame(stop) - frame(start) - 1), frame(t) = (t - 1) >> 2 for the dump's row t
// Measured with the vtime reference clock (cfg vtime = 1/60; the previous 0.000 came from the mod's batch-end
// action sweep, which stepped the sprites by whatever the batch was): a custom level, a horizontal dash ring
// at row 672 released at row 740: the sprite reads 0 at 672-675 and 16, 32 .. 240 at the frames from 676 on, and the
// player's angle on the release's row is 256.000 (frame 184 - frame 167 - 1 = 16 steps), then +1.731 a tick from the
// fall stake as before. A platformer (the dash velocity scales the spin) and a time warp are not modelled: the
// angle stays 0 there. --no-dashsprite is the off arm.
inline bool g_dashSprite = true;   // on by default (release flags, 2026-10-17); --no-dashsprite is the off arm
// --robotrot (off): the ROBOT has an angle. A dash ring (1751 here, rot -19) puts it on the dash line's -atan(slope)
// for the whole dash -- GD's rows: rot -19.000 from the ring's tick, the line rising 0.8264 over 2.4 a tick -- and
// the dash's end hands it to updateRotation's robot branch (0x391020 -> 0x391260), which eases it to 0 by
// 0.175 x the speed multiplier x 0.25 x 0.5 a tick (0.035 at 1.6: -19.000, -18.335, -17.693, -17.074 ...). The model
// never gave the robot an angle, so a turned portal's box was met by an upright robot: a custom level's teleport
// portal (25 x 90 turned 45 degrees, uid 153453) is touched by the robot at row 28,755 in GD and 28,754 in the
// model, one tick early (the tilted box leaves 0.4 px of margin there). Spider and the robot's other turns are
// left as they were. --no-robotrot is the off arm.
inline bool g_robotRot = true;   // on by default (release flags, 2026-10-17); --no-robotrot is the off arm
// --spiderrot: the SPIDER's angle, by the robot's law (needs --robotrot's constants, not the flag). A custom level's spider
// leaves a dash at rot -23.000 (the UFO before it took the dash line's angle at the ring: t=20,649 -23.000, held through the mode
// portal to the spider at t=20,675) and from the first tick off the dash eases toward 0 by 0.021875 x the speed multiplier:
// -20.0115, -19.6175, -19.2313, -18.8527 ... a constant ratio 0.98031 = 1 - 0.021875 x 0.9, the robot's. The model's spider was
// upright all the way (hazdbg: rot=0.0000), so its box met a 63-degree spike's corner 0.07 px deep where GD's, 17 degrees
// turned, is clear of it by 3 px: the model killed at t=20,730 a plan GD flies. A UFO takes the line's angle while it dashes
// (it is eased by its own law off the dash). --no-spiderrot is the off arm.
inline bool g_spiderRot = true;   // on by default (release flags, 2026-10-17); --no-spiderrot is the off arm
// --waveslopemini <s>: the mini wave's ramp-kill scale (constants.hpp kWaveSlopeMiniScale = 0.700, "UNVERIFIED", bracketed to (0.633, 0.779]).
// A GD-verified plan of a custom level puts a mini wave 4x fast, upside down, through a ramp's corner at t=25,302: injecting y offsets in GD,
// in the plan's own context, GD kills at +0.27 px and the model at -0.03, with the model's s=0.700 (the kill line is sy0 + (x - 4.068 s - sx0) -
// 4.0 s: 0.30 px lower than GD's) -- s = 0.663 reproduces GD's line there; that is one measurement, bracketing s to (0.633, 0.663]. 0.700 stays
// the default until a second measurement or a rig agrees.
inline double g_waveSlopeMini = 0.700;
// --waveslopeminik <R,L,Y> (default none): the mini wave's three ramp constants set one by one instead of by one scale (constants.hpp: kWaveSlopeDxR 3.072,
// DxL 4.068, Dy 4.0 at full size). The scale 0.663 above fits one corner pose and misses the next: a second one of the same plan (t=37,578, a ramp's
// corner where the sample is clamped to the box edge, so only Dy shows) has GD alive up to +0.25..0.50 px above the pose where 4.0 x 0.663 = 2.65 kills, i.e.
// Dy is 1.97..2.22, not 2.65 -- and the first one (t=25,302, unclamped) has Dx + Dy = 5.35. A rig of the mini wave 4x fast against ramp 1338 in its four
// orientations (GD only, injection four ticks before the pose, window matched to the model's: py/mklevel.py waveslope_mini, oneoff wsl_sweep.py)
// gives Dy = 2.003 (the clamped flat part), Dx_L = 3.237 and Dx_R = 2.241 (the two sloped parts), i.e. 0.50 / 0.80 / 0.73 of the full-size values. The Dx_L
// used here (3.246) is the first plan pose's (the larger of the two witnesses, so the more careful side of the kill line) and Dy 2.1 sits inside the
// second's bracket; Dx_R is the rig's alone. NOT a calibration: it is two corners of one level and one rig.
inline double g_wsMiniR = 2.241, g_wsMiniL = 3.246, g_wsMiniY = 2.1;   // on by default (release flags, 2026-10-17): the rig's Dx_R, the first plan pose's Dx_L, a Dy inside the second pose's bracket (see above); --no-waveslopeminik restores the single scale
// --waveringminipobb (off): a MINI wave meets a TURNED ring with its own 6 x 6 object rect (half kWaveHazHalfMini = 3) turned by its angle
// -- the same box-vs-box test (orientedHit) the other modes got from --ringpobb -- instead of --waveringminibox's unturned 3.6. A
// custom level's dash ring (id 1704 at 2.0 scale, rot -11, a 72 x 72 box) and the verified plan's mini wave at t=26,438 (the wave turned
// 2 degrees at the start of the tick, 13 degrees off the ring's axes): GD fires the ring from an offset along the ring's axis of 39.6 down,
// not from 39.75 (y injected in the plan's own context, the press and everything else unchanged): 36 + 3 (|cos 13| + |sin 13|) = 39.60. The
// unturned 3.6 (36 + 3.6 (|cos 11| + |sin 11|) = 40.22) fires the ring on the plan's own row, 0.22 px deep: the model dashes at t=26,439 and
// GD flies on. The earlier witness of the 3.6 (a -45 ring, 22.58 off its axis, the dash fires) is the same test only if the wave was
// turned about 13 degrees too; that row's angle is not recorded here.
inline bool g_waveRingMiniPobb = true;   // on by default (release flags, 2026-10-17); --no-waveringminipobb is the off arm
// --sectionround (off): the section cell of an object is its x / 100 ROUNDED, not truncated (step.hpp, gdSectionCell). A custom level's mini wave
// at t=29,517: a teleport portal (uid 158635, x 56,907) moves it from y 771 to 730 and GD fires a cube portal (uid 158644, x 56,895, y 674..760) on the
// same tick, which it can only do if the pass reaches the portal after the teleport; truncated, the cells are 568 and 569 and the model walks the
// cube portal first (at the old y, no contact), then never meets it (the next tick the wave is 20.9 px off in x): it stays a wave where GD is a
// cube. Rounded, both are cell 569 and the uid order puts the cube portal after the teleport.
// SUPERSEDED by --sectionuid (below): rounded cells put the same plan's next teleport wrong (t=30,399, a speed portal whose cell becomes the one after the
// teleport's: the model steps 1.35 px short from t=30,400). Kept for the record; do not turn it on.
inline bool g_sectionRound = false;
// --dropa1c (off): the drop ring's "terminal clamp skipped once" follows the byte [player+0xa1c], not playerIsFallingBugged on the firing tick.
// PlayerObject::updateJump (0x38c...): with 0xa1c set the gravity step has NO clamp (then the fall test clears the byte when falling); with it clear the
// step is clamped at +-15. ringJump sets 0xa1c for every ring but the drop ring, so after a drop ring the next tick is unclamped exactly when the byte was
// already set -- a jump or another ring since the last fall test said "falling". The model's rule (the rig's 2.0 / dropfall's 2g) is that fall test's
// proxy. A custom level (4x fast, flipped cube): the cube takes a drop ring at t=30,227 with the byte clear, and GD's next rows are vy 15.000, 15.000
// (injected from -14 to +14 in the plan's own context: never 15.216), where the proxy says "not falling" and skips: the model ran 0.05 px high for 2,000
// ticks, to a moving hazard at t=30,713. The latch the model tracks is a1cNow (step.hpp).
inline bool g_dropA1c = true;   // on by default (release flags, 2026-10-17); --no-dropa1c is the off arm
// --sectionuid (off): the x section cells do not order the collision pass across a teleport; the uid does (gdTeleAfter keeps its reach gate and the
// dynamicLevelHeight y split). Three teleports, each measured against GD, agree with the uid alone and no x-cell rule fits all of them:
//   official lv20 t=15,777: teleport uid 12,654 (x 22,909, cell 229); a ufo portal (uid 12,605, x 22,891) and a size portal (12,644, x 22,893), cell 228,
//     smaller uids: GD does not fire them on the landing tick (ascending cells and the uid agree; descending cells break it: quick lv20 37 segments differ);
//   Tidal t=29,517: cube portal uid 158,644 (x 56,895, cell 568) after teleport 158,635 (x 56,907, cell 569): GD fires it on the landing tick -- the one
//     ascending cells get wrong (the model walks the cube portal first and never meets it);
//   Tidal t=30,399: speed portal uid 165,872 (x 59,055) and teleport 165,876 (x 59,037), one cell: GD's speed change shows a tick later (a smaller uid).
// --sectionround got the middle row and the lv20 row right and this last one wrong. The official levels' own cross-cell cases do not tell the two apart (quick
// is byte-identical with the flag on), and no one has read the walk's cell order out of the binary for x -- do that before this is on by default.
inline bool g_sectionUid = false;
// --balldashrot: ...and the BALL's. A dashing ball holds -atan(slope) and the release's tick puts it to 0, as the
// cube's; and a dash ring (1704) stakes no spin -- ringJump leaves for the dash before its ball stake, so the roll
// goes on at the stake it had. A custom level's ball, upright, rolling +3.755 a tick: dash ring uid 108022 (rot -68)
// at t=20,916, GD's rot -68.000 through the dash, 0.000 on the release's row 20,920, +3.755 from 20,921. The
// model's spun on through the dash and came out turning -2.66 a tick (a ring's air stake), and its box, 15 degrees
// the wrong way, met the second dash ring on the same spot (uid 20866, rot -68) that GD's had left: the model
// dashed again at 20,924 where GD fell. The gravity dash ring (1751) flips gravity as well and is left as it was.
inline bool g_ballDashRot = true;
constexpr double kWaveRingHalfMini = kWaveRingHalf * 0.6;
// --portalslopeorder's re-run (frames.hpp; the end of stepOne): the uid of the gravity portal
// the nested step fires ahead of the ramp pass, -1 when no re-run is in progress.
inline thread_local int g_prePortalUid = -1;
// --modeslopeorder (on by default since 2026-10-14): the same re-run for a MODE portal that turns a flying body (ship, UFO,
// wave, swing) into a ground one (ball, robot, spider) on a tick a ramp with a larger uid would seat the
// new body. The game's object loop reaches the portal first and the ramp then meets the new mode's box
// (collisionCheckObjects: the portal at 0x214d8e, the ramp at 0x215e73, one loop); the model's ramp pass
// runs before its portal pass, so the new body sits a tick late. Official lv21 t=16,610: a wave rising
// off ramp uid 22024 takes spider portal uid 22017; GD's spider sits on the ramp at y 163.5 with vy 0 on
// that tick and leaves it with the exit launch (vy 4.203) on the next, while the model came out at
// y 161.0 with the halved 2.597, sat a tick later, and its held press then warped the spider to the
// ceiling. Kept to a non-dual body in frame 0, and taken only when the re-run seats the body on a ramp
// whose uid is larger than the portal's.
inline bool g_modeSlopeOrder = true;
// --spiderportalfirst (on by default since 2026-10-14): a grounded spider's press warps it after the collision pass, so a
// mode portal the spider stands in on that tick turns it first and the press is the new mode's. The
// model warps in the step, before its portal pass, and the portal then misses the warped body.
// Measured on SubZero 4002 t=17,217-17,218 (a Windows worker, the died plan with its press moved): a
// press on 17,215 or 17,216 warps the spider to the ceiling on the next tick, while the press on
// 17,217 -- whose next tick takes wave portal at the spider's feet -- leaves it on the floor as a
// wave, which climbs from 17,219 on the held button. So a warp is re-stepped without the tap when the
// body would have met a mode portal where it stood, and that run is taken if the portal turned it.
inline bool g_spiderPortalFirst = true;
// --spiderportalcube: ...a cube portal (type 6) among them, which the list left out. A custom level: an upside-down
// spider on a ceiling presses with cube portal uid 14683 at its feet; GD's row is a cube at y 3,345 with vy -11.23 (the
// press a flipped cube's jump), and the model warped the spider 63 px down to the floor and took the portal a tick later.
inline bool g_spiderPortalCube = true;
// --padbandin: a spider pad's search stops at a walled band only when the body is inside that band. GD's band
// (getMinPortalY / getMaxPortalY) holds the player; the model's state band can be one a portal left that the player
// is no longer in. A custom level: an upside-down cube under the zoomed camera band [3,851.6, 4,209.4] takes a
// ceiling spider pad at y 4,079.5 and GD drops it to the floor at 3,945; the model's band was [4,227.7, 4,567.2],
// above the cube, so the downward search found nothing above that floor and teleported the cube up to 4,242.7.
inline bool g_padBandIn = true;
inline thread_local bool g_noSpiderTap = false;
// --flipridepast (default on): a FLIPPED cube riding the solid-up side of a ceiling ramp past the
// ramp's far end is still on the ramp's extrapolated line, as the upright cube riding downhill is, so
// the press that tick still finds ground. The support test widened its window past the end only for an
// upright cube (step.hpp, the ramp support), and a flipped mini cube's press was refused with
// groundedNow=0 while GD jumped. A custom level (a mini cube flipped on a ceiling ramp, m=+0.339,
// t=9,191..9,199, 16..28 px beyond the ramp's x1): GD answers a press at 9,191 or 9,194 with the mini
// jump -8.944 on the next row and lives past the spike at 9,207 (a press sweep over 46 starts: only
// those two survive); the model rode on with vy 0 and died there, and so did every route the search
// tried. The window is the upright one's, with the slope's sign mirrored.
inline bool g_flipRidePast = true;
// --sizeseat (default on): a flipped mini cube seated under a solid that takes a regular-size portal is
// seated under it at the regular size on the portal's own tick (the model did it a tick later, from the
// next tick's support test). A custom level t=9,663: GD's row has y 1,874.750 (face 1,889.750 - 15), the
// model's 1,880.750 and then 1,874.750 on the next row; the 6 px was a divergence at every plan through there.
inline bool g_sizeSeat = true;
// --sizeexit (default on): a ship whose last ramp seat was made at the other size (a size portal fired
// since) does not hold the ramp window: the gap to the new size's line is the size change, not a stair, and
// GD loses the ramp and launches the ship. A custom level t=3,859 (cube turned ship on a ramp, then the
// mini portal): GD vy +1.802 on the exit, the model 5.73 px inside the 5.85 px window and no launch.
inline bool g_sizeExit = true;
// --padwaveold (default on): a pad after a mode portal that turned a WAVE this tick is judged with the
// wave's box, as before the portal (see the --padafterportal block in step.hpp).
inline bool g_padWaveOld = true;
// --rideoverspeed (default on): a ramp contact the 5.0 landing threshold refuses (the body arrives faster
// than that on its gravity side) still is a ride: collidedWithSlopeInternal writes the slope velocity at its
// tail whichever way the landing went, so the exit launch happens (see the rideLanded note in step.hpp).
inline bool g_rideOverSpeed = true;
// --tpportalforce (default on): --tptrigforce's rule for a teleport PORTAL: no push the next tick when the
// place the pass moved the body from had none, judged with the size the body had when the pass began.
inline bool g_tpPortalForce = true;
// --forceminifly (default on): a force box pushes a MINI ship or UFO 1.25 times as hard as a full-size
// one. PlayerObject::update scales the push by a per-mode constant that has a second value for a mini body
// (ship 0.47 -> 0.5875, UFO 0.58 -> 0.725, both x1.25), and the cube's and ball's stay (measured mini rigs).
// A custom level, mini ship in a 1.7 box (t=17,553..): GD's rows climb 0.045 a tick faster than the model's,
// the push 0.225 where the model's was 0.180 (1.7 x 0.1058).
inline bool g_forceMiniFly = true;
// --lawfrombelowship (default on): --lawfrombelow's seat from below (a contact the acquisition gate throws
// away goes to GD's law, which has no bound on how far below the seat the body may be) for a SHIP too.
inline bool g_lawFromBelowShip = true;
// --tpsaveoff (default on): an id-747 teleport portal with m_saveOffset (objrects tpsave, tprx, tpry) keeps the
// body's place relative to the entry's REAL position: y' = exit y + (y - real y), x' = exit x + (x - real x).
// A custom level t=3,193: GD takes the cube from y 705 to 1,355.95 (1,371 - 15) and x back 14.70 (3,347 -
// 3,361.7; cx holds the bounding box centre 3,369), the same x shift whatever height it enters at (press sweep:
// entry y 705 to 762); the model put it at 1,371 and left x alone, then died 13 ticks before GD.
inline bool g_tpSaveOff = true;
// --waverampend (default on, with --waveramp): the wave rides plain ramps also on the tick its END position
// first touches a 1755. The counter that arms the ramp ride is set in collisionCheckObjects before the solid and
// ramp passes of that tick (the note at --slideend), but the ramp gate read the start-of-tick touch only. A custom
// level t=17,830: a wave coming down onto a rising ramp, its end box 1.8 px into a 1755, sits on the ramp's line
// (y 423.84 = line + 5 sqrt 2) where the model seated it on the block beside it (425.0).
inline bool g_waveRampEnd = true;
// --ceilheldnoland (default on): a UFO held under a ceiling ramp (the model's ceilT, GD's m_isOnSlope with the
// under-side byte set and the ceiling ramp as m_currentSlope) takes no floor ramp. GD's slope record for a custom
// level, t=6,118 to 6,128: onSlope 1, under 1, uid 128564 (a ceiling ramp, m -2) every tick while the UFO slid down
// its underside inside the 60 px channel a floor ramp of the same footprint leaves (67 px of body along the normal),
// vy -2.000 falling by gravity alone (-2.344 on 6,122); no floor ramp landed it and none zeroed vy. The model seated
// it on the floor ramp (uid 129559) from 6,122, 7.08 px above GD's line from 6,123.
inline bool g_ceilHeldNoLand = true;
// --ceilowner (default on): a flipped UFO pushed by a ceiling-side ramp takes that ramp as its current slope even
// when a floor ramp seated it earlier in the tick. GD's slope record for a custom level, t=12,752: a flipped mini
// UFO at the crossing of a rising ramp (uid 164426, m +1) and a descending one (uid 164427, m -1) reads onSlope 1 with
// uid 164427 and rides its line down 2.4 a tick at vy -2.000; the model kept 164426 as the owner, so its next
// tick's seat carried the different-top-ness offset (bVar9, 12 px for a mini) and ended 5.944 px high.
inline bool g_ceilOwner = true;
// --waveportalrot (default on, gravity portals only): a wave meets a turned portal's box at its rotation one easing step back. A custom
// level t=15,210: a mini wave (rotation -12.759 at the tick's start, -7.128 the tick before) comes 0.017 px short
// of a gravity portal's oriented box (7.67 x 23.25, turned -22.2) at the first, and 0.205 px inside it at the
// second; GD flips it on t=15,210, the model on 15,211, and every plan after it ran 9.6 px off GD's.
inline bool g_wavePortalRot = true;
// --nocolceil (default on): a STATIC solid with the passable byte (--nocolstatic, Obj::oneway 2) is no
// ceiling for a ship: it rises through the block's underside. A custom level t=18,300: a ship rising at 8.0
// (gravity just turned by a green orb) enters the underside of such a block (uid 162458, bottom 480) and GD
// flies on through it (y 466.147, 467.947, ... vy 8.000) where the model pinned it at 465.000 with vy 0.
// The moving plates (oneway 1) and the type-21 plates keep their ceiling (the note above).
inline bool g_noColCeil = true;
// --slideunderdir (default on): an armed wave is pushed down out of a static underside only while it moves
// toward it. A custom level t=18,743: a flipped wave leaving a 1755 block's bottom face (static, bottom 540)
// downward, its previous head 4.04 px inside the face and 3.95 px across it, is not pushed in GD (y 536.641,
// 534.241, ... vy -9.6) where the model seated it at 535.000 with vy 0 and every route died there.
inline bool g_slideUnderDir = true;
// --waveseathaz (default on): the wave's hazards are also tested at the position the slide push-out leaves it
// in (see the block after the push-out in step.hpp).
inline bool g_waveSeatHaz = true;
// --tpramplaunch (on by default since 2026-10-14): a ramp's rider that a teleport takes off the ramp gets the ramp's exit
// launch (slopeExitVy x the ride's factor), as postCollision gives any rider that has left its ramp on
// the tick; the teleport runs in the object pass, before it. The model seats in its ramp pass before
// the teleport and the ride never ended for it. SubZero 4003 t=12,468: a cube climbing an m=1 ramp
// (reversed, 1x) walks into a teleport after 19 ticks on it; GD comes out with vy 6.171 =
// 7.405 x 20/24, the model with the gravity step.
inline bool g_tpRampLaunch = true;
// --waveholddead (on by default since 2026-10-14): the wave climbs on +0x985, the same hold latch the ship reads, so a
// button that a spider's warp or a ball's flip left dead while it stays down (State::holdDead) moves
// a wave as a released one. SubZero 4002 t=17,220-17,222: the spider warps on a held press, a wave
// portal turns it on 17,221, and GD's flipped wave falls toward its gravity on 17,222 while the
// model, reading the raw button, climbed.
inline bool g_waveHoldDead = true;
// --downdyrev (on by default since 2026-10-14; --no-downdyrev is the off arm): the floor ramp's feet band (+1, kWaveSlopeDescendDy) is bVar22's "descending", which XORs
// m_isGoingLeft into the ramp's own direction (slopes.hpp); the model read the ramp alone. SubZero 4003 t=12,328
// (Worker 99 hitboxtrace): a reversed wave runs down-left along ascending plain 1338 uid5320 at a constant 7.71 px
// above the line; GD destroys it (no object) at x=9,624.64 y=302.35, inside 301.71 + 1, where the cap (304, a
// fresh contact) had spared it the tick before.
inline bool g_downDyRev = true;
// --wavebandzoom (on by default since 2026-10-14; --no-wavebandzoom is the off arm): the wave's band clamp (step.hpp yMaxW) read the state's band as the portal wrote it, at
// zoom 1. GD's is getMinPortalY's branch B (win 0x213690): centre - H/2 / camScale, floored at 90, and the max
// is that plus H / camScale. SubZero 4003 coin run t=10,320: zoomed to 0.833333 at x=9,795, GD's band is
// [150, 510] and its wave rises to 470.95 and into a teleport; the model clamped it at 480 - 10 = 470.
inline bool g_waveBandZoom = true;
// --flybandcentre: ...and the same for the ship / UFO / ball band (step.hpp floorY / bandCeilNow), which kept the
// portal's floor and grew only the ceiling by H / camScale. SubZero 4001 t=20,379: zoomed to 0.833333, GD's ship
// band is [540, 900] around the portal's centre 720; the model's was [570, 930], put the falling ship on a floor
// of 585 and flew it over the spike GD kills it on at 20,393.
inline bool g_flyBandCentre = true;
// --spidernoextcol: the spider's surface search (step.hpp spiderTargetGd) does not see an Extended Collision object.
// spiderTestJumpInternal asks staticObjectsInRect / damagingObjectsInRect, which walk the section grid, and
// addToSection files such an object in the layer's always-checked list instead (memory: the --bigsection note).
// A custom level t=7,037: an upside-down spider taps down past a 477 x 30 id-3610 hazard with extcol=1; GD's
// hazard query returned another object, its solid strip a block it landed on 588 px down, where the model
// counted the 3610 as found, widened the solid query and landed on a column beside the strip 78 px down.
inline bool g_spiderNoExtCol = true;
// --autolockmoves: an object an x-crossing Move locks to the player's x (alock / alockY, level_loader.hpp) counts as
// moved by its autonomous controller, so a touch box that only switches it on or off (no offset) does not make it a
// touch-controlled object held at its first row until the box fires. The same custom level: block uid 51884 is
// locked to the player's x from the trigger at x 315 (its recording follows the player to x 9,712) and box 1 only
// turns it off; the model kept it at x 315, and the upside-down spider GD lands on it 588 px down missed it.
inline bool g_autoLockMoves = true;
// --tpskipfly (on by default since 2026-10-14; --no-tpskipfly is the off arm): teleportPlayer (win 0x20fdb0) sets player+0x560 first thing, and checkCollisions (0x2137f0)
// reads and clears it at its top and skips the whole band block when it was set -- so on the tick after a teleport
// no flying mode is clamped to the band. The ground modes had it (State::tpSkip bit 1, --tpbandskip); the wave's
// and the flight loop's band clamps did not. SubZero 4003 t=10,278 / 10,322 (Worker 99): a wave teleported to
// y=159 goes on down to 157.05 under the band floor's 160 and is put back on the tick after; the model clamped at
// once and ran one tick ahead of GD's zig-zag from there.
inline bool g_tpSkipFly = true;
// --balltapexit's re-run (frames.hpp; the top of stepOne): 1 while the nested step runs with the
// ball's fresh-press taps held back, and whether the uphill launch fired in it.
inline thread_local int g_ballTapNested = 0;
inline thread_local int g_ballTapLaunch = 0;
// Ring once (was --ringonce, always on since 2026-09-26): a fired ring never fires again in the
// run, remembered for the last five rings a body fired (usedOrb, usedOrbOld and
// State::usedOrbHist) instead of two. GD refuses a ring it has fired even after other rings in
// between (a custom level t=2,548, the field's note).
// Portal once (was --portalonce, always on since 2026-09-26): a MODE portal fires only on the
// first tick the player overlaps it -- the latch the gravity portals already have (step.hpp,
// portalLatch), for the mode portals. GD
// activates a portal on its first overlap whether or not it changes anything. A custom level t=4,300: a
// cube enters cube portal uid 637 at about t=4,275 (no change), then UFO portal uid 651 at 4,299
// while still inside 637. GD stays a UFO falling at -2.991; the model fired 637 and 651 again on
// every tick inside both, halving vy twice a tick (-2.905 -> -0.748 -> -0.209). The size portals
// the same: a custom level t=16,674, a mini cube inside mini portal uid 2359 enters normal portal 2356
// and GD stays full size at y=135; the model fired 2359 and 2356 again and re-seated to 141. It
// remembers one tick of overlaps (State::portSeen), not a run-long latch, so a portal left and
// re-entered fires again.
// --portseenx: ...but a body still inside the portal's x window keeps it as seen when it leaves the
// box only in y (step.hpp, the portal pass's y gate), so leaving and coming back over the same x
// does not fire it again -- GD's mark is the object's own, per player and attempt. A custom level
// t=9,979..9,982 (dual mini cubes): player 1 takes mini portal uid 24070 at 9,979, its foot rises
// 1.1 px clear of the box on 9,980..9,981 and comes back on 9,982, the tick it also enters the
// normal portal uid 24068 beside it. GD comes out full size; the model fired 24068 and then 24070
// again and stayed mini, which put the dual solo / robot / gravity cluster at x=17,665 three ticks
// late for player 1. On by default since 2026-10-06; --no-portseenx is the off arm. A portal
// re-entered after the body's x has left its window (reverse play) still fires again.
inline bool g_portSeenX = true;
// The --start hold field (its 7th, held_before(plan, t0)); -1 when no --start was given.
inline int g_startHoldField = -1;
// Ring first (was --ringfirst, always on since 2026-09-26):
// a press that finds a ring is the ring's. pushButton fires the touched
// rings and returns; it calls updateJump only when there are none. The model runs the grounded
// jump or tap in its physics branch and the ring loop after it, so a grounded ball pressing inside
// a gravity ring flipped twice. A custom level t=11,833: a ball on the ceiling presses in gravity ring
// uid 1981; GD comes out upright at -3.1304 (the ring alone), the model tapped (flip) and then
// fired the ring (flip back) and stayed on the ceiling at +3.1304. The tap's flip and spin are
// undone when a ring fires on the tick of the press that made them.
// Ring order (was --ringorder, always on since 2026-09-26): which touched ring a press fires is
// GD's m_touchedRings order, carried in State::touchRing, instead of "in contact a tick ago beats
// not, then the lowest uid". The array
// keeps a ring's first-contact position; at a tick's head resetTouchedRings drops the rings the
// tick before did not touch, and the collision pass appends new contacts in ascending uid. One
// tick of history cannot tell apart two rings entered on different earlier ticks: a custom level t=2,741,
// a ship in contact with yellow 447 (entered ~2,735) and gravity 443 / yellow 446 (a tick later,
// one square), and GD fires 447 where the lowest-uid rule fired 443.
// Gravity pad chain (was --gravpadchain, always on since 2026-09-26):
// the gravity pads a tick enters are taken in uid order and every one
// the facing gate lets through flips. collisionCheckObjects (0x2158f9) sets gravity to the pad's
// own direction (!isFacingDown) when it differs, then propellPlayer and flipGravity, pad by pad
// in the bucket's uid order -- so two pads facing opposite ways flip twice. The model's "one flip
// per tick" (gravPadFlipped) is right only for pads that agree, which the gate already reduces to
// one. A custom level t=1,307 enters a wall of sideways blue pads (rot -90, flipY alternating = facing
// alternating); GD comes out upside down at +6.4 where the model stopped after the first flip at
// -6.4 (another custom level t=6,960 and a third custom level t=8,103 are the same wall kind).
// Pad first (was --padfirst, always on since 2026-09-26):
// a pad that fires on the tick of a grounded press leaves the press
// unspent. The pad fires in the collision pass and propellPlayer drops the ground flag; the buttons
// run after it, so updateJump finds the player airborne and does not jump, and +0x986 stays set
// for a ring touched later in the hold. The model jumped in its physics branch (spending the
// press) and let the pad overwrite the velocity. A custom level t=15,114: an upside-down cube presses on
// the tick a blue pad flips it (both give -6.4), and on the next tick GD fires yellow orb uid 1503
// with that press (11.18) while the model, its press spent, did not.
// One-way ceiling (was --onewayceil, always on since 2026-09-26):
// a one-way platform's world underside is a ceiling for the flying
// modes. collidedWithObjectInternal lets ship, UFO, wave and swing into its ceiling resolution
// unconditionally; the cube's head branch is gated (the arms), which is what the "transparent from
// below" measurement saw (lv13, a cube jumping up through type-21 blocks). A custom level t=9,220: an
// upright ship holding up into plate uid 840 (id 143, underside 136) stops at y=121 with vy 0 in
// GD, where the model flew through. It still never kills (the plate's side test stays off).
// Portal flap (was --portalflap, always on since 2026-09-26):
// a fresh press on the tick a cube enters a UFO portal is the UFO's, and
// it flaps on the NEXT tick. The press reaches pushButton in the buttons phase, after the collision
// pass made the player a UFO, and a UFO flaps in updateJump on the next update -- the ship->UFO
// rule's pending flap (State::pFlap). The model jumped as the cube and re-issued the flap on the
// portal tick itself, one tick early (a grounded cube), or dropped the press (an airborne one).
// A custom level t=15,402 and the calib_portalpress cells cube->ufo at offset -1: GD flaps at the press + 2,
// grounded and airborne. The reverse (ufo/ship -> cube, press one tick before the portal: GD jumps
// on the portal tick) is not representable in the model's per-tick input -- its in(t) = 1 on that
// tick already means the old mode's action -- and a plan the search writes never contains it.
// --heldall (EXPERIMENT): State::held is the button level of the tick in EVERY mode. Only the wave
// and flying branches wrote it, so a body that entered a wave from the ground read whatever the
// last flying section (or the anchor) left: the wave's first direction is `s.held`. Official lv21's
// quick_regress section from t=13,000 anchors in a robot section with the button down, the robot
// never rewrites held, and at the robot -> wave portal (t=14,391) the model climbs at +6.457 where
// GD dives at -6.457 with the button up. The ship's release re-clamp (holdRelU) reads it too.
inline int g_heldAll = 1;
// --heldcellcap (EXPERIMENT, only with --heldall): the alive cap counts CELLS, not states. Under
// --heldall a cell whose states differ only in the button (held, jumpBuf) keeps them apart, as it
// should, but each twin then takes a slot of the cap and the water-fill's stride runs over twins
// standing side by side, so the layer keeps fewer cells than it would without the flag (lv19:
// x~3,541 needs cap 2,000 with --heldall where 125 did without). With this the water-fill samples
// one state per cell and every state of a sampled cell is kept.
inline int g_heldCellCap = 0;
// --heldcelltwins N (with --heldcellcap; 0 = no limit): keep at most N states of a sampled cell --
// its first state, then the states whose held differs from it, then the rest, each in layer order.
// A cell holds up to four (held x jumpBuf), so without a limit a layer can keep four times the cap:
// measured over the official 22 at t=0 (2026-09-26), lv20's stepped children went
// 60.1M -> 346.7M and its peak frontier to 22x the cap, lv14 2.8M -> 125.7M.
inline int g_heldCellTwins = 0;
// --sametick (default on since 2026-10): a replayed plan's press and release on ONE press tick are read as GD's button
// pass takes them, not as the tick's last level. The mod feeds both to handleButton in that tick, in order.
// pushButton (0x397f40) sets the hold bytes (+0x985/+0x986) and acts AT ONCE: ringJump on each touched
// ring, or -- none touched -- updateJump(0) when the body is on the ground (+0x9c1) and is the ball
// (+0x9bb), or is none of ship (+0x9b9), UFO (+0x9ba), wave (+0x9bc) and swing (+0x9c4). releaseButton
// (0x3981d0) clears the hold bytes (and stops a dash) before the update, and updateJump's flap, thrust and
// climb all read them, so nothing else of the press survives. Here a tap holds for its effect tick only
// when a trial step shows that work (a ring used, or a grounded ball's, cube's, robot's or spider's jump);
// otherwise the old reading (no press) stands. A release then a press while held is a fresh press, seen
// as a rising edge on the effect tick.
// Measured in GD: a custom level t=16,500 (a loop splice left a mini UFO's press and the tail's seam
// release, written a latency early, on one tick): GD jumps one row after the tap wherever it is put
// (16,499 / 16,500 / 16,502), through an orb that moves with the player. Two custom levels whose mini UFO
// taps touch no ring (one at t=11,678, one at 4,387, both upside down): GD does not flap. The trunk
// solutions' pairs are a ship's: no thrust (lv20 t=14,095).
inline bool g_sameTick = true;
inline bool sameTickTapMode(uint8_t mode) { return mode == 0 || mode == 2 || mode == 5 || mode == 6; }
inline bool toggleScalesNow(uint8_t mode) {   // ship, UFO, wave, robot, spider
    return mode == 1 || mode == 3 || mode == 4 || mode == 5 || mode == 6;
}
// --forcecircle (default on since 2026-10): a force block 3645 with neither target mode nor range pushes like a 2069 --
// m_force along (flipY ? 180 : 0) + 90 - rot at the mode's unit, a tick after the contact -- and its
// contact is GD's circle test, gated by the collision sections. collisionCheckObjects sends 0x815 (2069)
// and 0xe3d (3645) down one branch (m_stateForce = 2, calculateForceToTarget 0x4c1ec0); the falloff there
// only runs with +0x74d (range) set. An object with m_objectRadius > 0 is met through playerCircleCollision
// (0x211df0): with the level's fixRadiusCollision, centre distance <= player width / 2 + radius * the
// larger scale; without it, the circle's centre inside the player's rect or a corner of the rect inside the
// circle. And the pass reaches the object only from the 3 x 3 cells around its centre's (--bigsection).
// A custom level t=17,650: uid 23888 (radius 15, scale 9 x 1, m_force 1.5, rot 0) under a falling cube;
// GD adds a constant +0.337 per tick from the tick the player's cell reaches the block's, at any height
// up to the circle's edge (a y sweep: 5 of 6 heights start on the same tick, the sixth two ticks later at
// 148.36 / 150.76 from the centre). The model's field started 7 ticks early and weakened with height.
inline bool g_forceCircle = true;
// --forcelive (default on since 2026-10): a constant 3645 (--forcecircle) is met where the moving-geometry recording has it on the
// tick, not where the level put it. A custom level t=21,135..21,167: uid 26764 (rot 180, scale 1 x 4, m_force 5),
// carried by a Lock to Player X Move from x 41,265 and lifted 75 px, pushes a falling cube to vy -16.125 (= the -15
// cap - 5 x 0.225) while its circle (15 + 60) holds the player's centre; the model met it at its load position, 186 px
// behind, and fell at -15, 6 px high by the landing, where it took a jump GD never had. ...and only while the recording
// has it on (modifiers.hpp forceFieldAt has the rows it reads): at t=15,447 a Toggle turns uid 12729 off as the cube it
// follows leaves its floor, and GD pushes once (vy -1.341) and then lets it fall at plain gravity.
inline bool g_forceLive = true;
// --ringpresseat (default on since 2026-10): a cube's or ball's fresh press inside ANY toggle ring (1594) that the recording has on
// goes to the ring -- pushButton hands a press to the touched rings and makes no jump of it -- including the rings the
// activator construction leaves out because their effect is not modelled (--orbspawn's ring takes the press only when
// it is taken). The held button does not jump either (holdDead, as --orbspawn has it). A custom level t=21,176: a cube
// carried on a rising block presses inside spawn ring uid 26528 (on; its Toggles reach a spike, so the model left it
// out); GD's y follows the block with vy 0 through the hold (21,176..21,177) and after, where the model jumped 11.23.
// ...and only a ring THIS body has not fired: its uid goes into the body's ring memory (usedOrbOld / usedOrbHist,
// per body through swapHalves, seeded at an anchor from --spentorb / --spentorb2 in their order; a Multi Activate
// ring never). GD marks a fired ring used per player -- activatedByPlayer (0x1a4a90) sets +0x5b4 for the player
// whose m_uniqueID is 1 and +0x5b5 for the other -- and collisionCheckObjects skips a ring
// hasBeenActivatedByPlayer (0x1a4af0) answers yes for before playerTouchedRing. A custom level's dual
// balls: toggle ring uid 13578 rides beside p2; p1 fired it at t=7,736 and p2 with a tap at 10,625 that shows
// nothing else. GD's p2 then flips on its presses inside the ring at 11,575 and 11,607; with the 10,625 tap taken
// out of the plan, GD's p2 press at 11,575 goes to the ring and the flip comes a tick late.
// The cost: a toggle ring takes a place in the same five-deep memory as the rings proper, so on a level that fires
// toggle rings the oldest ring proper is forgotten one fire sooner. A ring firing twice there is the first thing to
// suspect when this change turns a replay from passing to failing.
inline bool g_ringPressEat = true;
// --spawnringon (default on since 2026-10): the toggle rings (1594) take presses the way the recording says GD's did.
// pushButton hands a press to the rings the previous tick's collisions touched, and a fired ring's Toggles switch
// the next ring on after that, in the row the fire is recorded in (the press step's own K.t). So:
//  - a ring the recording has OFF on row K.t-1 takes no press, as a spawn ring (--orbspawn, --acthazoff) or under
//    --ringpresseat (which read its "on" from row K.t+1);
//  - a spawn ring takes a held press (markTouched) only with the button down on this step and the press not spent
//    (it took the parent's button, a tick after a release, and every overlapping box at once);
//  - its jump (--spawnringjump) is the held press's own, a tick late: given only to a cube that pressed from the
//    ground, and not while another ring the recording has on in the fire's row holds the body.
// A custom level x 41,355 .. 41,520: three rings stacked per spot, A ("A off, B on"), B ("B off, C on") and C (opens
// the spike wall at x 41,685). GD's cube fires A on the press at 21,165 in the air (row 21,166: A off, B on) and B on
// the press at 21,175 on a rising block (row 21,176: B off, C on), never leaves its line, presses no more and dies on
// the wall at 21,225. The model fired all three boxes when it entered them at 21,162, a tick after the release, then C
// on the second press, jumped both times and lived. With the flag the anchored replay agrees with GD on every column
// to the death.
inline bool g_spawnRingOn = true;
// --acthazoff (default on since 2026-10): a spawn ring (--orbspawn) whose spawned Toggles switch hazards OFF is taken as an activator;
// activatorTargetSafe refuses a hazard because switching one ON can kill later, which a Toggle with togon 0 cannot do.
// A custom level x 41,355 .. 41,520: three stacked toggle rings per spot, each press turning the next one on; the third,
// uid 26528, spawns group 317, whose Toggle uid 26957 switches group 340 -- the spike wall at x 41,685 (uids 26960 ..
// 26965) -- off. Left out, the ring could not open the wall, and every search from anchors back to t=19,724 ended on it
// (t=21,224, "doomed") or took the fall route down to the floor.
inline bool g_actHazOff = true;
// --actlookchain (off): a touch toggle block (1594) whose target group holds triggers is not left out ("a chain this does not follow")
// when every trigger it reaches is look-only (a Spawn whose group holds Alpha/Pulse/colour triggers, say), which cannot change what is
// touched. A custom level's block uid 8770 (x 2,714, 50.9 square) toggles group 197, the 129-object platform at x 2,775..2,895; the group
// holds one Spawn (uid 34174) that spawns group 556, two Alpha triggers. Left out, the model read the platform's on/off from the
// recording of an attempt that never entered the box (off), where GD's low route does enter it and rides under the platform's underside:
// t=1,744 GD stands on the underside at y 254.752, the model rises on to a saw at t=1,771.
inline bool g_actLookChain = true;   // on by default (release flags, 2026-10-17); --no-actlookchain is the off arm
// Ground-grow (was --groundgrow, always on since 2026-09-26):
// a cube or ball that a size portal grows while it stands on the WORLD
// ground is lifted to the new seat on the NEXT tick, by the ground clamp, not on the portal's own
// tick. GD's ground plane is enforced in PlayerObject::update, before checkCollisions fires the
// portal, so the grown body spends one tick embedded; a block is resolved in checkCollisions and
// can re-seat it the same tick (lv12 t=3,286, which stays). Old custom level t=17,939: a
// mini ball on the ground (y=99) meets RegularSize portal uid 2257 -- GD reads vsize 1 at y=99
// and 105 only at t=17,940, where the model went to 105 at once. The calibration rig's reading
// (cube next tick, ball and UFO the same tick; the retracted r69 note at the re-seat) is left
// alone for the flying modes, which have their own floor clamp.
// Ring press pin (was --ringpresspin, always on since 2026-09-26):
// a ring taken off a block by a FRESH press leaves y on the block. GD
// reaches a ring by two paths: a button already down fires it from the collision pass
// (playerTouchedRing), before the solid pass puts the body back on the block, so y keeps that
// tick's gravity step; a press fires it from pushButton in the buttons phase, after the solid pass
// has re-seated it. The model released the block pin for every ring. Old custom level
// t=2,126: a mini cube on block uid 204 taps (press line 2,125) inside yellow orb uid 200; GD
// leaves at y=150.000 with 8.944, the model at 149.951. The orb evidence at the release (lv19
// t=254/506, lv16 t=6,404, lv18 t=10,824) has no press at those ticks in today's plans.
// Solid order (was --solidorder, always on since 2026-09-26):
// the two main collision loops (ground and flight) take the near list
// in DESCENDING uid, the order GD resolves solids in. The cx sort leaves equal-cx objects in the
// unstable sort's order. Read from GD's own collidedWithObject calls (hbox lines, the ppre of each
// call being the previous call's result) over a custom level t=1..3,099: 306 ticks with two or more
// solids, 306 in descending uid, 0 ascending, 86 of them across two 100-unit x cells.
// The same custom level, t=9,235: slab 4959 lands the ball (y 240), one-way plate 4956 then pushes its head
// down to y=231, slab 4931 does nothing, and spike 4927 kills at 231. This is the solids only:
// the bucket walk that meets rings and pads (ascending uid, 0x205210) is a different pass.
// --solidorddbg t0:t1 (diagnostic): print `solidord t= uid=` for each solid a collision loop visits
// while the player's box overlaps it, in visiting order, for ticks t0..t1 -- the model's side of
// GD's hbox call order, for the consumer that checks the two orders tick by tick. Replays only:
// the search visits the same tick once per state.
inline long long g_solidOrdDbgT0 = -1, g_solidOrdDbgT1 = -1;
// Ball ceiling (was --ballceil, always on since 2026-09-26):
// an upright ball whose head is past a solid's underside, having been no
// more than the landing tolerance past it at the start of the tick and not falling, is put under
// the face with vy 0 -- one-way plates included. collidedWithObjectInternal lets the ball into its
// ceiling resolution unconditionally (the cube's head branch is the gated one), which the
// ground-mode loop had only for a ball entering from the side (lv9 t=18,354). The same custom level t=9,235
// (hbox lines): after slab 4959 lands the ball at y=240, one-way plate 4956 (underside 240,
// 9 px into the head, x overlap 0.28) puts it at y=231; the model passed through the plate.
// Takes GD's solid order (--solidorder) to put the landing before the push.
// The boundary, from injections under normal slab uid2511 (underside 330) in the same level, a mini
// ball at x=6,225: rising at vy +2, a head 9.9 px past the underside at the start of the tick is
// put at y=321 with vy 0 and onGround 1 (and falls from the next tick); 10.1 dies. Falling at vy -1
// it is never pushed -- the head sits 9.4 px inside and the ball falls on, alive -- and 10.3 dies.
// So the tolerance is kLandTol on the tick's start, the gate is "not falling", and the push grounds
// the ball. The falling case's survival is --faceclass's head side.
// Flipped ball ceiling (was --ballceilflip, always on since 2026-09-26):
// --ballceil's push on a flipped ball, whose head side is a solid's
// top: head past the top, no more than kLandTol past it at the start of the tick, not falling (in
// its own gravity) -> y on the top, vy 0, and NOT grounded -- GD's onGround stays 0 on this side.
// Old custom level t=7,686: a gravity portal flips a ball falling at -10.6 (7,683); it
// decelerates to -5.1 and its head reaches block 2274's top (360); GD's row is y=375, vy 0,
// onGround 0, then +0.129 a tick. The model went on into the block.
// Snap past a one-way (was --snaponeway, always on since 2026-09-26):
// the stair snap's nudge is blocked by a one-way (breakable, type 21)
// PREVIOUS snap object only, not by a one-way landing target. checkSnapJumpToObject (0x393cb0)
// skips the nudge when the stored object (+0x888) is missing, is the same uid, or its object type
// (vtable+0x660) is not 0 -- the new object's type is never read. The counted "0 moved / 108 still"
// on calls with a type 21 in them is consistent (lv13 t=2,142 had both one-ways). Old custom
// level t=12,161: a flipped cube snapped on solid 4136 lands on breakable 4127's underside
// (dx 60, dy -60 = the slow `big` stair, mirrored); GD moves x by +1.0 (snapdist 1.2129).
// Flight re-land (was --flyreland, always on since 2026-09-26):
// in the flight loop, once a ceiling has pushed the player down this tick,
// a floor may land it even though it is already grounded. GD resolves each solid on its own; the
// model's gate `!c.grounded` let the ceiling have the last word. Old custom level t=3,536
// (hbox lines), a mini ship squeezed between floors 998/969 (top 224) and ceiling 996 (underside
// 240): 998 lands it at y=233, 996 pushes it down to 231, and 969 lands it at 233 again, every tick,
// so GD stays at 233 on the floor. The model ended on the ceiling at 231. Takes --solidorder for
// GD's order. Opening the gate without the ceiling condition cost lv19 t=19,801 (a grounded UFO
// re-landed on a face 0.002 px off GD's), so the plain case keeps it.
// Pad then land (was --padthenland, always on since 2026-09-26):
// pads that fire on a landing tick, leave gravity where it was and leave
// vy pointing along it do not undo the landing; the player stays on the face with vy 0.
// checkCollisions runs the activation pass before the solid pass (releasePin's note), so the solid
// pass lands the player on the pads' vy. The model runs the pads last and undid the landing for
// any pad. Old custom level t=8,103 (fg and hbox lines): two gravity pads flip the cube
// and back (vy +6.4, then -6.4), then floors 2871 and 2870 land it at y=225 with vy 0. The model
// left it at vy -6.4 for a tick.
// Face class (was --faceclass, always on since 2026-09-26):
// a solid whose face the player's foot (in its own gravity) is inside by
// no more than the landing tolerance -- kShipLandTol for the ship, kLandTol for cube and ball -- is
// a ground contact. Neither the ship's side kill nor the crush applies to it. GD classifies a
// contact by the face before the inner box decides anything. Measured on an old custom level by
// injecting a rising player (vy +2, centre over the block, x fixed) above a top face:
//   full ship  over uid441:  dies at centre 4.487 above the face, lives at 4.507  (inner box 4.5)
//   mini ship  over uid997:  dies at 2.993, lives at 3.003 -- depth 9 - 3.0 = 6.0 = kShipLandTol
//   full cube  over uid4491, full ball over uid3860: die at 3.8, live at 4.8   (inner box 4.5)
//   mini cube  over uid1570, mini ball over uid3980: live at 1.601 and 1.621 (depth 7.4 < kLandTol)
// With the full size the inner box never reaches a face the foot is within tolerance of (15 - 10 =
// 5 > 4.5, 15 - 6 = 9); with the mini size it does, so only the mini bodies see the rule. The crush
// rig's mini cube (kCrushHalf) was measured on the HEAD side, under a ceiling, which this leaves
// alone. A custom level t=3,603: a mini ship rising past uid997's corner, 5.96 into the top face; GD's hbox
// is hit=0 and the model died on fly/solid-side, then on crush.
// The BALL's head side is a face too (it is on GD's unconditional ceiling list): a mini ball
// falling at vy -1 under slab uid2511 of a custom level lives with its head 9.45 px past the underside and
// dies at 10.05 (--ballceil's note). The cube's head side is not (the crush rig).
// Cube, ship and ball only: the wave dies 1.5 px from a face (lv17 t=7,040, lv20 t=839), well
// inside the tolerance, and the UFO (which lands even while rising), robot, spider and swing are
// unmeasured.
// Portal ceiling (was --portalceil, always on since 2026-09-26):
// when a mode portal turns a cube, ball, robot or spider into a flying
// body, the new body meets the ceilings the same tick. It uses the flight loop's ceiling-ride test
// (head no more than kShipLandTol past the underside at the start of the tick, past it now, not
// falling): y goes under the face and vy to 0. GD's activation pass switches the mode before its
// solid pass; the model resolves the solids with the old body and runs the portals afterwards.
// Old custom level t=8,382 (dhh and hbox lines): a cube jumping at 10.99 enters a ship
// portal with its head 7.4 into ceiling 2988 (4.99 at the start of the tick). GD's didHitHead puts
// the ship at y=225, vy 0. The model kept the halved 5.39 and rose inside the ceiling.
// Flip grace (was --flipgrace, always on since 2026-09-26):
// 004b's grace, re-landed narrow. Within kFlipGraceTicks of a gravity flip
// a cube, ball, robot or spider whose inner box would kill it on a static solid (the side test or
// the crush) is put on that solid's face instead -- its head-side face, vy 0 -- provided GD
// classifies the contact as head side (collidedWithObjectInternal: the foot moved 10 px toward the
// head, now or at the previous position, reaches no further than the solid's far edge;
// flipGraceSeat). The withdrawn 004b gate, and this flag until 2026-09-25, used the static proxy
// "centre outside the solid's y span", which also spares side entries GD kills (a custom level t=19,794).
// It is limited to static solids in frame 0: the classifier also widens by the surface's speed.
// Old custom level t=15,121: a blue pad flips the cube upright at 15,114, a yellow orb sends
// it up into the underside of the block at y 300, and on the tick its inner box crosses GD puts it
// at y=285 (head on the face) with vy 0; the model died there. The lv18 probe behind 004b (a block's
// top face, flip age 0/12/24 lives, 25/30/36 dies) is the same window.
// --flipgracedyn: the flip grace for a MOVING solid as well. collidedWithObjectInternal classifies the
// contact the same way for any object -- the foot plus the reach (10) against the solid's MinY, at the
// current and the previous player position, on the solid's current rect -- and widens the reach by the
// solid's speed only when it moved toward the body's head (upright: up); a solid moving the other way,
// or not at all, is classified as a static one. A custom level t=5,296: a blue pad flips a mini cube
// upright at 5,294, and block uid 9160 (30 x 30, coming down 4.69 px a tick to its rest at 945) meets
// its head two ticks later; GD seats it at 921.000 (= the underside 930 - 9) with vy 0 and onGround 1,
// and it falls away from 5,297. The model, whose grace took static solids only, killed it as a crush.
// A solid moving toward the head keeps the old behaviour (its widened reach is not modelled).
// On by default; --no-flipgracedyn is the off arm.
inline bool g_flipGraceDyn = true;
// --spentorb uid,uid,... : the rings GD fired earlier in the attempt, oldest first, seeded into the
// anchor state through noteRingFired. A ring fires once per attempt, but an anchored state starts
// with usedOrb empty, so the model re-fired a ring GD had spent just before t0. Old custom level
// six kitref episodes anchored at t=2,561 in a field of stacked yellow and gravity rings:
// GD fired a ring at 2,555..2,558 and, on the next press, the one beside it; the model fired the
// spent one again. The mod's cfg dpspentorb passes the list (ringseed).
inline std::vector<int> g_spentOrb;
// --spentp2 (default on since 2026-10): ...and --spentorb2, the second body's, into its own ring memory (usedOrb2 /
// usedOrbOld2 / usedOrbHist2, the halves swapHalves trades). GD keeps a ring's "fired" per body
// (EnhancedGameObject::hasBeenActivatedByPlayer: +0x5b4 for p1, +0x5b5 for p2), and collisionCheckObjects
// skips a ring that answers yes. --spentorb carried p1's alone, so an anchored model's p2 fired a ring GD's
// p2 had spent: a custom level's dual cube took yellow ring 18683 at t=12,912 in GD (its act / actP went
// 0 -> 1 that tick, on the ring's edge), and after the 12,918 anchor the model took it again at 12,938.
// The mod passes --spentorb2 only to a call that does not carry --no-spentp2.
inline std::vector<int> g_spentOrb2;
// --histstat <file> (print only): after a replay, one line appended to <file> with how full the
// fixed-length histories got and how often one dropped an entry: touchRing
// (4 slots, --ringorder), portSeen (3, --portalonce), usedPad (4) and the fired-ring memory
// (usedOrb + usedOrbOld + usedOrbHist = 5, --ringonce); plus how many ring fires in the replay were
// of a ring that had already fired in it -- what a memory without a limit would have refused.
inline bool g_histStatOn = false;
inline std::string g_histStatPath;
struct HistStat {
    int touchMax = 0, touchDrop = 0, portMax = 0, portFull = 0, padMax = 0, padFull = 0;
    int orbFired = 0, orbDrop = 0, orbRefire = 0;
    // ...and pads fired a second time in the replay: usedPad forgets a pad once the player is
    // clear of it in x, where GD's latch holds for the whole attempt (cli.hpp's --spentpad note)
    int padRefire = 0;
};
inline thread_local HistStat g_hist;
inline thread_local std::vector<int> g_histFired;
inline thread_local std::vector<int> g_histPads;
inline double pinkRingRatio(int mode) {
    return mode == 2 ? 0.77 : mode == 1 ? 0.37 : mode == 3 ? 0.42 : 0.72;
}
inline double rawGravStep(bool ballLike, bool robot, double dx) {
    const double mul = speedMulForDx(dx);
    const double g = mul < 0.8 ? 0.940199 : mul < 1.0 ? 0.958199024 : mul < 1.2 ? 0.957199 : 0.961199;
    if (ballLike) return -0.958199024 * 0.6 * 0.225;
    return -(robot ? 0.9 : 1.0) * g * 0.225;
}
// --offboard <margin> (EXPERIMENT): the repair loop's own definition of a run that has left the
// playfield (repair.hpp offBoardTick: world y more than `margin` outside the recorded band at that
// tick), applied in the search. GD does not kill there; the loop credits the death there instead.
// Without it the search keeps offering arcs through the sky that the loop scores as dead on
// arrival -- lv22 t~2677, 70 rounds with nothing learnt (2026-09-19). 0 = off.
inline double g_offBoardMargin = 0.0;
inline int g_shiftDbgUid = -1;       // --shiftdbg <uid>
inline bool g_shiftDbgDone = false;
// --ceilfarbox (default off): a flipped cube arriving fast over the far end of a ceiling ramp is taken by it (step.hpp).
inline bool g_ceilFarBox = false;

}  // namespace dp
