#pragma once
// The replay panel: GD's own record of the deepest verified attempt, played in a corner of the screen while the
// search is working. (The pure half -- the readers, the clipping, the silhouettes -- is replay_panel_geom.hpp.)
//
// WHAT IS PLAYED. Not the model's guess: the rows `anchors::g_deepest` holds, which the player wrote as each tick of
// the attempt ended, so any place the model was wrong and GD's state was taken instead is already in them. The level
// is `dpsolve::g_csv` (the table the session opened, not the sliced copy the scene may hold), the moving geometry is the run's
// own recording (`dp_groups.txt`, the deepest replay's), and the points where a fixup says the model parted from GD
// are marked on the bar and ringed as the path passes them.
//
// WHERE. The free area from the middle of the screen to the top-right: the overlay column keeps the left edge, the
// iteration map's strip the bottom-right. One CCDrawNode child of the scene, CPU-clipped to the panel's box.
//
// WHEN. On by default (cfg replaypanel), the key turns it off, and only while a solve runs: the showing of the solution
// is a replay and the panel is not drawn there. Both ends of a solve empty it.
//
// WHAT IT NEVER DOES. It writes nothing the solver reads, holds no pointer into the game, takes the main thread's
// copy of the rows once per change and does its file reading and parsing on one background thread per input. Off, or
// outside a solve, a frame costs a few tests.
#include <chrono>
#include <future>
#include <memory>

#include "mod/itermap.hpp"
#include "mod/replay_panel_geom.hpp"

namespace p1 {
namespace replaypanel {

namespace pg = panelgeom;

// Tags are looked up with getChildByTag on the scene, where every overlay hangs, so they must not repeat one another's:
// 0x51D50.. are hud.hpp's, 0x51D60.. itermap's, 0x51D70 touch_controls' PAD_TAG, 0x51D71 progress_bars' BAR_TAG. (The panel
// first took 0x51D70/71, and its label lookup returned the progress bar: a CCLabelBMFont cast on another node.)
constexpr int PANEL_TAG = 0x51D80;
constexpr int LABEL_TAG = 0x51D81;

// What the panel plays, and whether: cfg `replaypanel` is the start (0 off, 1 the whole verified path -- the default --,
// 2 its last stretch) and the key (Tab) goes round full -> recent -> off -> full. -1 = follow cfg.
enum Mode { Full = 0, Recent = 1, Off = 2 };
inline int g_modePref = -1;
inline int mode() {
    if (g_modePref >= 0) return g_modePref;
    return g_cfg.replayPanel == 0 ? Off : g_cfg.replayPanel == 2 ? Recent : Full;
}
inline bool wanted() { return mode() != Off; }
inline const char* modeName() { return mode() == Full ? "full" : mode() == Recent ? "recent" : "off"; }

// ------------------------------------------------------------------ reading a file the solver may be writing

// The whole file, opened so that the solver can still replace or delete it while this holds it (a plain ifstream shares
// read and write but not delete, and the loop removes and re-copies its recordings). Empty when it cannot be read.
inline std::string readShared(const std::string& path, size_t cap = (size_t)512 << 20) {
    std::string out;
#ifdef GEODE_IS_WINDOWS
    const int wn = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (wn <= 0) return out;
    std::wstring w((size_t)wn, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, w.data(), wn);
    HANDLE h = CreateFileW(w.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return out;
    LARGE_INTEGER sz{};
    if (GetFileSizeEx(h, &sz) && (unsigned long long)sz.QuadPart <= cap) {
        out.resize((size_t)sz.QuadPart);
        size_t got = 0;
        while (got < out.size()) {
            DWORD n = 0;
            const DWORD ask = (DWORD)std::min<size_t>(out.size() - got, 1u << 24);
            if (!ReadFile(h, out.data() + got, ask, &n, nullptr) || n == 0) break;
            got += n;
        }
        out.resize(got);
    }
    CloseHandle(h);
#else
    std::ifstream f(path, std::ios::binary);
    if (!f) return out;
    f.seekg(0, std::ios::end);
    const std::streamoff n = f.tellg();
    if (n < 0 || (unsigned long long)n > cap) return out;
    f.seekg(0);
    out.resize((size_t)n);
    f.read(out.data(), n);
    out.resize((size_t)f.gcount());
#endif
    return out;
}

// ------------------------------------------------------------------ what the panel holds

struct LevelJob {
    std::string sig;
    std::shared_ptr<const pg::Level> level;
    size_t obb = 0;
    double ms = 0.0;
};
struct MoversJob {
    std::shared_ptr<const pg::Level> level;     // the level the rows' object indices are into
    std::shared_ptr<const pg::Movers> movers;
    long long serial = -1, depth = -1;
    double ms = 0.0;
};

struct State {
    // the level
    std::string levelSig;                        // dpsolve::g_csvSig of the table in `level` ("" = none read)
    std::shared_ptr<const pg::Level> level;
    std::future<LevelJob> levelJob;
    // GD's path
    std::shared_ptr<const pg::Track> track;
    long long trackSerial = -2;
    size_t trackSrcRows = 0;
    long long staleSerial = -3;                  // the solver's rows when the last level / solve ended: not the new one's
    size_t staleRows = 0;
    // the run's recording of the moving geometry
    std::shared_ptr<const pg::Movers> movers;
    std::vector<int> moverOf;                    // object index -> index in movers->m, -1 for a static one
    long long moversSerial = -1, moversDepth = -1;   // the recording's attempt and depth that were read
    long long askedSerial = -2, askedDepth = -2;     // ...and the pair last asked for (so one failure is not retried every frame)
    std::future<MoversJob> moversJob;
    // the corrections
    std::vector<pg::Mark> marks;
    size_t fixupsSeen = (size_t)-1;
    // the clock and the camera
    bool clockOn = false;
    pg::Clock clock;                             // where in the stretch the picture is
    std::chrono::steady_clock::time_point last;
    pg::Camera cam;                              // eased toward the player's y; off = snap on the next frame
    // where the panel's left edge is: it keeps clear of the overlay's text and only moves right within a solve, so a
    // line that grows and shrinks does not make the picture breathe
    float leftEdge = 0.f;
    // bookkeeping
    bool wasSolving = false;
    int levelId = -1;                            // g_cfg.levelId the panel is holding data for (-1 = none yet)
    bool visible = false;                        // the nodes are shown (so they need hiding when it goes off)
    long long frames = 0;
    double usSum = 0.0;
    long long runFrames = 0;                     // frames drawn and their cost since the panel was last emptied
    double runUs = 0.0;
    long long runSkipped = 0;                    // ...and frames left alone because the game was busy
    // The frame interval: a frame that follows a long one is not drawn (see update)
    std::chrono::steady_clock::time_point lastFrame = std::chrono::steady_clock::now();
    bool drawn = false;                          // something has been emitted since the panel was last emptied
    bool held = false;                           // the picture is held because the game is busy
    // what the keys do to it while the screen is dark
    bool paused = false;                         // F2: the clock stands still
    int speedIdx = -1;                           // the pace, a notch of WATCH_SPEEDS (-1 = from cfg when first wanted)
    bool owning = false;                         // on screen with a path to play: the transport keys are the panel's
    std::string lastText;                        // the status line that goes with the held picture
};
inline State g_s;

// A frame that comes this long after the one before it is a frame of a game that is busy (the fast loop's batches, a section
// search's slices, a recording pass: 50 ms to seconds a frame on a heavy level, against 16.7 ms when it is only waiting for the
// search). The panel is an animation, and an animation drawn on frames like those jerks; it is held where it was instead.
constexpr double kBusyMs = 40.0;

// The stretch played: the whole verified path (mode full) or its last `replaypanelspan` ticks, ending at the frontier (recent).
inline void stretch(const pg::Track& tr, long long& a, long long& b) {
    pg::stretchOf(tr, mode() == Recent ? g_cfg.replayPanelSpan : 0, a, b);
}

inline void say(const std::string& line) {
    writeResult("replaypanel: " + line);
}

// ------------------------------------------------------------------ taking the inputs in

// The rows of the deepest attempt, as the few numbers a picture needs. Main thread (g_deepest is written there, in
// onDeath), once per change.
inline std::shared_ptr<const pg::Track> copyTrack(const std::vector<AnchorRow>& rows, long long serial) {
    auto tr = std::make_shared<pg::Track>();
    tr->f.resize(rows.size());
    for (size_t i = 0; i < rows.size(); ++i) {
        const AnchorRow& r = rows[i];
        if (!r.valid) continue;
        pg::Frame& f = tr->f[i];
        f.x = r.x; f.y = r.y; f.vy = r.vy;
        f.x2 = r.x2; f.y2 = r.y2;
        f.mode = (signed char)r.mode;
        f.flip = (signed char)(r.flip != 0);
        f.mini = (signed char)(r.mini != 0);
        f.dual = (signed char)(r.dual != 0);
        f.og = (signed char)(r.onGround != 0);
        f.gframe = (signed char)(r.gframe & 3);
        f.valid = true;
    }
    tr->serial = serial;
    tr->finish();
    return tr;
}

// Show this path from now on (the clock starts at the start of its stretch the first time).
inline void adoptTrack(std::shared_ptr<const pg::Track> tr, size_t srcRows, const char* what) {
    g_s.track = std::move(tr);
    g_s.trackSerial = g_s.track->serial;
    g_s.trackSrcRows = srcRows;
    g_s.fixupsSeen = (size_t)-1;
    char b[160];
    snprintf(b, sizeof(b), "path of the %s (attempt %lld): ticks %lld..%lld", what, g_s.trackSerial, g_s.track->first,
             g_s.track->last);
    say(b);
    if (!g_s.clockOn) {
        long long a, z;
        stretch(*g_s.track, a, z);
        g_s.clock = pg::Clock{(double)a, 0.0};
        g_s.clockOn = true;
        g_s.last = std::chrono::steady_clock::now();
    }
}

// What belongs to the path and the recording (not the level): drop it, and refuse the rows the solver still holds --
// they are the last level's until its next attempt replaces them.
inline void resetPath() {
    g_s.track.reset();
    g_s.trackSerial = -2;
    g_s.trackSrcRows = 0;
    g_s.staleSerial = anchors::g_deepestSerial;
    g_s.staleRows = anchors::g_deepest.size();
    g_s.movers.reset();
    g_s.moverOf.clear();
    g_s.moversSerial = g_s.moversDepth = -1;
    g_s.askedSerial = g_s.askedDepth = -2;
    g_s.marks.clear();
    g_s.fixupsSeen = (size_t)-1;
    g_s.clockOn = false;
    g_s.cam.on = false;
}

// A solve has ended, or one has begun: whatever the panel holds is the last solve's. The panel is shown only while a
// solve runs, so after the clear (the showing of the solution is a replay) it is emptied, and the next solve starts
// from nothing -- not from the last level's picture for the moment before its own data arrives.
inline void retire() {
    const bool had = g_s.track || g_s.level || g_s.movers;
    resetPath();
    g_s.level.reset();
    g_s.levelSig.clear();
    g_s.leftEdge = 0.f;
    g_s.levelId = -1;
    g_s.drawn = false;
    g_s.held = false;
    g_s.paused = false;
    g_s.owning = false;
    g_s.lastText.clear();
    if (had) {
        char b[160];
        snprintf(b, sizeof(b), "emptied (%lld frames drawn, %lld held while the game was busy, %.0f us a frame on average)",
                 g_s.runFrames, g_s.runSkipped, g_s.runFrames > 0 ? g_s.runUs / (double)g_s.runFrames : 0.0);
        say(b);
    }
    g_s.runFrames = 0;
    g_s.runUs = 0.0;
    g_s.runSkipped = 0;
}

inline void startLevelJob() {
    if (g_s.levelJob.valid()) return;
    const std::string sig = dpsolve::g_csvSig;
    std::string csv = dpsolve::g_csv;
    const std::string obbPath = std::string(DATA_DIR) + "/obb.txt";
    g_s.levelJob = std::async(std::launch::async, [sig, csv = std::move(csv), obbPath]() {
        const auto t0 = std::chrono::steady_clock::now();
        LevelJob j;
        j.sig = sig;
        const std::string obbText = readShared(obbPath, (size_t)64 << 20);
        const pg::ObbMap obb = pg::parseObb(obbText);
        j.obb = obb.size();
        j.level = std::make_shared<const pg::Level>(pg::parseObjrects(csv, &obb));
        j.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        return j;
    });
}

inline void startMoversJob(long long tMax) {
    if (g_s.moversJob.valid() || !g_s.level || dpsolve::g_groupsPath.empty()) return;
    g_s.askedSerial = dpsolve::g_groupsSerial;
    g_s.askedDepth = dpsolve::g_groupsDepth;
    const std::string path = dpsolve::g_groupsPath;
    auto level = g_s.level;
    const long long serial = dpsolve::g_groupsSerial, depth = dpsolve::g_groupsDepth;
    g_s.moversJob = std::async(std::launch::async, [path, level, serial, depth, tMax]() {
        const auto t0 = std::chrono::steady_clock::now();
        MoversJob j;
        j.level = level;
        j.serial = serial;
        j.depth = depth;
        const std::string text = readShared(path);
        j.movers = std::make_shared<const pg::Movers>(pg::parseGroups(text, *level, tMax));
        j.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        return j;
    });
}

inline bool ready(const std::future<LevelJob>& f) {
    return f.valid() && f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}
inline bool ready(const std::future<MoversJob>& f) {
    return f.valid() && f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

// The level in `level` is the one the solver holds now.
inline bool levelCurrent() { return g_s.level && g_s.levelSig == dpsolve::g_csvSig; }

// Everything the panel reads, brought up to date. Cheap when nothing changed: a few comparisons.
inline void refresh() {
    // the level: a table the panel has not read (a new level, or the same one again after retire())
    if (!dpsolve::g_csv.empty() && dpsolve::g_csvSig != g_s.levelSig && !g_s.levelJob.valid()) {
        if (!g_s.levelSig.empty()) resetPath();      // another table in the middle of a session: another level
        startLevelJob();
    }
    if (ready(g_s.levelJob)) {
        LevelJob j = g_s.levelJob.get();
        if (j.sig == dpsolve::g_csvSig) {            // (a table that has been replaced since is read again next frame)
            g_s.level = j.level;
            g_s.levelSig = j.sig;
            g_s.movers.reset();
            g_s.moverOf.clear();
            g_s.askedSerial = g_s.askedDepth = -2;
            char b[160];
            snprintf(b, sizeof(b), "level %zu boxes (%zu not drawn), %zu oriented boxes, read in %.1f ms",
                     j.level->objs.size(), j.level->hidden, j.obb, j.ms);
            say(b);
        }
    }
    // GD's path: the deepest attempt's rows, when they have changed
    const std::vector<AnchorRow>& rows = anchors::g_deepest;
    if (!rows.empty() && (anchors::g_deepestSerial != g_s.trackSerial || rows.size() != g_s.trackSrcRows)
        && !(anchors::g_deepestSerial == g_s.staleSerial && rows.size() == g_s.staleRows)) {
        adoptTrack(copyTrack(rows, anchors::g_deepestSerial), rows.size(), "deepest attempt");
    }
    // the moving geometry: the run's recording, read again whenever the solver has kept a new one
    if (ready(g_s.moversJob)) {
        MoversJob j = g_s.moversJob.get();
        if (j.level == g_s.level) {
            g_s.movers = j.movers;
            g_s.moversSerial = j.serial;
            g_s.moversDepth = j.depth;
            g_s.moverOf.assign(g_s.level->objs.size(), -1);
            for (size_t k = 0; k < g_s.movers->m.size(); ++k) g_s.moverOf[(size_t)g_s.movers->m[k].obj] = (int)k;
            char b[200];
            snprintf(b, sizeof(b), "moving geometry of attempt %lld: %zu objects, %lld rows up to tick %lld, read in %.1f ms",
                     g_s.moversSerial, g_s.movers->m.size(), g_s.movers->rowsRead, g_s.movers->cover, j.ms);
            say(b);
        }
    }
    if (g_s.level && g_s.track && !g_s.moversJob.valid() && !dpsolve::g_groupsPath.empty() && dpsolve::g_groupsDepth >= 0
        && (dpsolve::g_groupsSerial != g_s.askedSerial || dpsolve::g_groupsDepth != g_s.askedDepth))
        startMoversJob(g_s.track->last + 600);
    // the corrections: fixups of the run that lie on this path
    if (g_s.track) {
        std::lock_guard<std::mutex> lk(itermap::g_mu);
        if (itermap::g_fixups.size() != g_s.fixupsSeen) {
            g_s.fixupsSeen = itermap::g_fixups.size();
            g_s.marks.clear();
            for (const itermap::Fixup& fx : itermap::g_fixups) {
                const pg::Frame* f = g_s.track->at(fx.tick);
                if (f && std::fabs(f->x - fx.x) < 3.f && std::fabs(f->y - fx.y) < 3.f)
                    g_s.marks.push_back(pg::Mark{fx.tick, fx.x, fx.y, fx.kill != 0});
            }
            std::sort(g_s.marks.begin(), g_s.marks.end(),
                      [](const pg::Mark& a, const pg::Mark& b) { return a.t < b.t; });
        }
    }
}

// ------------------------------------------------------------------ the panel's place and the clock

// The free area, from the middle of the screen to the top-right. The overlay column keeps the left edge and the
// iteration map's strip the bottom-right; getWinSize() is the whole design rectangle (see itermap::stripBox).
//
// The left edge is the middle of the screen unless the overlay's own text reaches past it: the elapsed time at the end of
// the first line and the render state at the end of the keys line do, on a long phase or a long held-state line, and a
// panel drawn over the end of a word hides it. So the edge is moved right of the right-most of those lines that stands on
// the panel's rows (`textTags` are their tags; getChildByTag is the lookup the overlay itself uses), and kept there.
inline cocos2d::CCRect panelBox(cocos2d::CCNode* parent, std::initializer_list<int> textTags) {
    using namespace cocos2d;
    const CCSize v = CCDirector::sharedDirector()->getWinSize();
    const float m = v.width * 0.012f;
    const float y0 = v.height * 0.50f, y1 = v.height - m;
    float right = 0.f;
    for (const int tag : textTags) {
        CCNode* n = parent->getChildByTag(tag);
        if (!n || !n->isVisible()) continue;
        const CCRect b = n->boundingBox();
        if (b.getMaxY() < y0 || b.getMinY() > y1) continue;     // not on the panel's rows
        right = std::max(right, b.getMaxX());
    }
    const float want = std::min(v.width * 0.60f, right + 5.f);  // (a line past 60% is let overlap: the picture needs room)
    g_s.leftEdge = std::max(g_s.leftEdge, std::max(v.width * 0.42f, want));
    return CCRect(g_s.leftEdge, y0, v.width - m - g_s.leftEdge, y1 - y0);
}

// Where the pace comes from: the notch of the game's own spectating speeds nearest to cfg `replaypanelspeed` the first time
// it is wanted, then the arrows move it.
inline void initSpeed() {
    if (g_s.speedIdx >= 0) return;
    const float want = g_cfg.replayPanelSpeed > 0.f ? g_cfg.replayPanelSpeed : 1.f;
    int best = WATCH_SPEED_1X;
    for (int i = 0; i < WATCH_SPEED_N; ++i)
        if (std::fabs(std::log(WATCH_SPEEDS[i] / want)) < std::fabs(std::log(WATCH_SPEEDS[best] / want))) best = i;
    g_s.speedIdx = best;
}

// The tick kept inside the stretch (the mode can change it under a clock that stands still).
inline void clampTick() {
    if (!g_s.track) return;
    long long a, z;
    stretch(*g_s.track, a, z);
    g_s.clock.tick = std::max((double)a, std::min((double)z, g_s.clock.tick));
}

inline void advanceClock(const pg::Track& tr) {
    using clk = std::chrono::steady_clock;
    const auto now = clk::now();
    double dt = std::chrono::duration<double>(now - g_s.last).count();
    g_s.last = now;
    if (dt > 0.1) dt = 0.1;                     // a stalled frame (a level swap, a breakpoint) is not a jump
    if (g_s.paused) return;                     // F2 stopped it: the clock stands still
    initSpeed();
    long long a, z;
    stretch(tr, a, z);
    if (g_s.clock.advance(dt, (double)WATCH_SPEEDS[g_s.speedIdx], a, z)) g_s.cam.on = false;   // started over: the camera snaps
}

// ------------------------------------------------------------------ the keys, while the screen is dark

// With the screen off (a solve's fast loop) the transport keys have nothing of the game to move: the level is the loop's own
// verification replay, held between its rounds. They are the panel's then -- F2 stops and starts it, F3 / F4 step it one
// tick / ten (a step stops it), the arrows seek it (a tap is five seconds, holding accelerates) and change its pace. With the
// screen on the panel is not shown and the keys are the game's, as ever. The panel's state (mode, place, stop, pace) lives
// through that, so turning the screen off again finds it where it was left.
//
// `owning` is set by update(): the panel is on screen with a path to play. Without one the keys stay the game's.
inline bool owns() { return g_s.owning; }

inline void keyPause() {
    g_s.paused = !g_s.paused;
    say(g_s.paused ? "paused (F2)" : "playing (F2)");
}
inline void keyStep(int ticks) {
    g_s.paused = true;
    g_s.clock.hold = 0.0;
    g_s.clock.tick += (double)ticks;
    clampTick();
}
inline void keySeek(long long ticks) {
    g_s.clock.hold = 0.0;
    g_s.clock.tick += (double)ticks;
    clampTick();
    g_s.cam.on = false;
}
inline void keySpeed(int dir) {
    initSpeed();
    g_s.speedIdx = std::max(0, std::min(WATCH_SPEED_N - 1, g_s.speedIdx + dir));
    char b[48];
    snprintf(b, sizeof(b), "pace %gx", (double)WATCH_SPEEDS[g_s.speedIdx]);
    say(b);
}

// ------------------------------------------------------------------ one frame

// A CCDrawNode blends with (GL_ONE, GL_ONE_MINUS_SRC_ALPHA), the game's premultiplied-alpha default, so a colour handed to
// it must already be multiplied by its alpha: a plain (1, 1, 1, 0.12) adds a full white and the faint bar under the path
// came out solid white (seen in the game, 2026-10-17). Black and fully opaque colours were already right.
inline cocos2d::ccColor4F premul(const pg::Color& c) {
    return cocos2d::ccColor4F{c.r * c.a, c.g * c.a, c.b * c.a, c.a};
}

inline void emit(cocos2d::CCDrawNode* n, const pg::Scene& sc, float ox, float oy) {
    using namespace cocos2d;
    static thread_local std::vector<CCPoint> v;
    n->clear();
    for (const pg::Prim& p : sc.prims) {
        const float* q = sc.pts.data() + p.off;
        switch (p.kind) {
            case pg::Prim::Poly: {
                v.resize((size_t)p.n);
                for (int i = 0; i < p.n; ++i) v[(size_t)i] = CCPoint(ox + q[2 * i], oy + q[2 * i + 1]);
                n->drawPolygon(v.data(), (unsigned)p.n, premul(p.fill), p.lw, premul(p.line));
                break;
            }
            case pg::Prim::Seg:
                n->drawSegment(CCPoint(ox + q[0], oy + q[1]), CCPoint(ox + q[2], oy + q[3]), p.lw, premul(p.line));
                break;
            case pg::Prim::Dot:
                n->drawDot(CCPoint(ox + q[0], oy + q[1]), p.lw, premul(p.line));
                break;
        }
    }
}

inline cocos2d::CCLabelBMFont* label(cocos2d::CCNode* parent) {
    using namespace cocos2d;
    auto* lbl = static_cast<CCLabelBMFont*>(parent->getChildByTag(LABEL_TAG));
    if (!lbl) {
        lbl = CCLabelBMFont::create("", "chatFont.fnt");
        if (!lbl) return nullptr;
        lbl->setTag(LABEL_TAG);
        lbl->setID("replay-panel-label"_spr);
        lbl->setAnchorPoint({0.f, 1.f});
        lbl->setScale(0.34f);
        lbl->setZOrder((1 << 20) + 3);
        parent->addChild(lbl);
    }
    return lbl;
}

// Hidden is emptied as well: a node that keeps its drawing in the scene shows wherever its visibility is not honoured. The panel's own state
// (g_s) is kept, and the picture is drawn afresh every frame it is shown, so emptying costs nothing.
inline void hide(cocos2d::CCNode* parent) {
    if (auto* n = parent->getChildByTag(PANEL_TAG)) {
        n->setVisible(false);
        static_cast<cocos2d::CCDrawNode*>(n)->clear();
    }
    if (auto* l = parent->getChildByTag(LABEL_TAG)) {
        l->setVisible(false);
        static_cast<cocos2d::CCLabelBMFont*>(l)->setString("");
    }
}

// Called once per frame from the overlay pass. `show` is the overlay pass's own (F1 hides it with the rest); `textTags`
// the overlay's own text lines, which the panel keeps clear of.
//
// On screen only while a solve runs (showingSolve(): a candidate replay in the loop is still the solve, the showing of the
// solution is not) AND the screen is dark: with the screen on, the game is what is watched and the keys are its, so the
// panel is hidden -- not emptied: its state is kept and it comes back as it was. Both ends of a solve empty it, so a level's
// picture is never what the next level starts with.
inline void update(cocos2d::CCNode* parent, bool show, std::initializer_list<int> textTags) {
    using namespace cocos2d;
    if (!parent) return;
    const auto tNow = std::chrono::steady_clock::now();
    const double gapMs = std::chrono::duration<double, std::milli>(tNow - g_s.lastFrame).count();
    g_s.lastFrame = tNow;
    const bool solving = showingSolve();
    if (solving != g_s.wasSolving) {
        g_s.wasSolving = solving;
        retire();
    }
    // A suite goes from one level to the next without ever leaving the solve: there the level the session is asked for
    // changing is the edge, and the picture is emptied the same way.
    if (solving && g_s.levelId != g_cfg.levelId) {
        const bool changed = g_s.levelId != -1;
        if (changed) retire();
        g_s.levelId = g_cfg.levelId;
    }
    if (!solving || !show || !wanted() || renderingOn()) {
        g_s.owning = false;
        if (g_s.visible) { hide(parent); g_s.visible = false; }     // off costs this test, not two tree lookups
        return;
    }
    if (!g_s.visible) {                          // back from hidden (the screen went off again, or the key): no jump
        g_s.last = tNow;
        g_s.cam.on = false;
    }
    g_s.visible = true;
    g_s.owning = (bool)g_s.track;
    // A busy game (a long gap since the last frame): hold the picture, say so, and let the clock stand still so that it does
    // not jump when the frames come back. The first frame of a solve is drawn whatever the gap, and so is any frame on a scene
    // that has none of the panel's nodes yet: the scene is replaced when the level is (and the slice swaps it), and a held frame
    // there would leave nothing on screen for as long as the game stays busy.
    if (gapMs > kBusyMs && g_s.drawn && parent->getChildByTag(PANEL_TAG) && parent->getChildByTag(LABEL_TAG)) {
        ++g_s.runSkipped;
        g_s.last = tNow;
        if (!g_s.held) {
            g_s.held = true;
            if (auto* l = static_cast<CCLabelBMFont*>(parent->getChildByTag(LABEL_TAG))) {
                const std::string held = g_s.lastText + "  [held: the game is busy]";
                l->setString(held.c_str());
            }
        }
        return;
    }
    g_s.held = false;
    const auto c0 = std::chrono::steady_clock::now();
    refresh();

    auto* n = itermap::drawNode(parent, PANEL_TAG, "replay-panel"_spr, (1 << 20) + 2);
    auto* lbl = label(parent);
    if (!n || !lbl) return;
    n->setVisible(true);
    lbl->setVisible(true);

    const CCRect box = panelBox(parent, textTags);
    static pg::Scene S;                          // the buffers are kept from frame to frame
    std::string text;
    const pg::Track* tr = g_s.track.get();
    if (!tr || tr->last < 0 || !levelCurrent()) {
        S.reset(pg::Box{0.f, 0.f, box.size.width, box.size.height});
        S.rect(0.f, 0.f, box.size.width, box.size.height, pg::Color{0.f, 0.f, 0.f, 0.72f});
        text = !levelCurrent() ? "replay panel: reading the level..."
                               : "replay panel: waiting for a verified path";
    } else {
        advanceClock(*tr);
        clampTick();
        pg::FrameIn in;
        in.level = g_s.level.get();
        in.movers = g_s.movers.get();
        in.moverOf = &g_s.moverOf;
        in.moversOfThisAttempt = g_s.movers && g_s.moversSerial == tr->serial;
        in.track = tr;
        in.marks = &g_s.marks;
        stretch(*tr, in.a, in.z);
        in.t = std::max(in.a, std::min(in.z, (long long)std::floor(g_s.clock.tick)));
        in.W = box.size.width;
        in.H = box.size.height;
        text = pg::buildFrame(S, in, g_s.cam);
        initSpeed();
        char st[64];
        snprintf(st, sizeof(st), "  %s %gx%s", modeName(), (double)WATCH_SPEEDS[g_s.speedIdx],
                 g_s.paused ? " [paused]" : "");
        text += st;
    }
    emit(n, S, box.origin.x, box.origin.y);
    g_s.drawn = true;
    g_s.lastText = text;
    if (text != lbl->getString()) lbl->setString(text.c_str());
    lbl->setPosition({box.origin.x + 3.f, box.origin.y + box.size.height - 2.f});

    // what a frame costs, now and then (the search's overlay timer counts it too)
    ++g_s.frames;
    const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - c0).count();
    g_s.usSum += us;
    ++g_s.runFrames;
    g_s.runUs += us;
    if (g_s.frames % 2000 == 0) {
        char b[160];
        snprintf(b, sizeof(b), "%lld frames, %.0f us a frame on average, %zu primitives in the last",
                 g_s.frames, g_s.usSum / 2000.0, S.prims.size());
        say(b);
        g_s.usSum = 0.0;
    }
}

// Tab: the whole verified path -> its last stretch -> off -> the whole path.
inline void toggle() {
    g_modePref = (mode() + 1) % 3;
    say(modeName());
}

}  // namespace replaypanel
}  // namespace p1
