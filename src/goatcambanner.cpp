// goatcambanner.cpp - GOAT Cam promotional banner. Implementation of goatcambanner.h.
//
// WHAT THIS IS A PORT OF
//   The GOAT Cam banner spec (brand\goatcam-banner\SPEC.md in the GOAT Cam repo): two copy
//   lines, then a dark plate holding the real coffee photo in a viewfinder, the GOAT lockup
//   and the App Store QR, in the goatcam.dagoat.io "Golden-hour Desk" palette.
//   - Lockup paths: byte-for-byte the `d` attributes of GOAT_LOCKUP_MARKUP in the Maps
//     extension's widget.js (outer viewBox "46 -16 220 250"), by way of Game Optimizer's
//     src\sponsor.cpp, whose SVG path reader, spark timings and easing solver are reused.
//   - Durations and curves: goat-lockup-hover.css (the lockup) and the site's --ease-out.
//   - The photo: resources\goatcam-coffee.jpg, the coffee-tight box (0,500)-(440,870) of
//     the site's hero-coffee-1600.jpg (CC0, Alex Knight), embedded as RCDATA.
//
// DRAWING
//   Each control owns one 32bpp DIB as its back buffer, so nothing flickers. GDI+ draws the
//   shapes, the photo and everything with alpha straight into the DIB's bits; GDI then draws
//   the text (ClearType and letter-spacing, which GDI+ lacks) and the QR modules, which must
//   land on exact integer pixels to stay scannable.
//
// HOVER LIFT
//   The web plate scales to 1.05. Here it rises S(2) px and casts a soft shadow instead:
//   scaling would resample the QR off its integer module grid and make the text shimmer on
//   every frame. Both are kept honest by moving only in whole pixels.
//
// THE CPU RULE, WHICH OUTRANKS EVERYTHING ABOVE
//   The 15 ms animation timer exists only while a transition is still running or the spark
//   is inside its window. Motion::busy() is the single gate and syncTimer() kills the timer
//   the moment it goes false, so the banner at rest - hovered or not - costs nothing. The
//   looping parts of the web banner (meteors, steam, the pulsing OSD dot) are deliberately
//   NOT ported: they would hold the timer open for as long as the pointer stays, in an app
//   that exists to keep a fan quiet. SPEC section 6 lists the native hover without them.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "goatcambanner.h"

#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <objidl.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

namespace Gdiplus { using std::min; using std::max; }
#include <gdiplus.h>

namespace ui {

const wchar_t* kGoatCamClass = L"MacFanCtlGoatCam";

namespace {

namespace gp = Gdiplus;

// GdiplusStartup's result. While it is false nothing in this file touches GDI+: render()
// paints a plain GDI plate (no photo, no mark) and no GDI+ object is ever constructed.
bool g_gdiplusOk = false;

// Fault seams for the offscreen test harness, which #includes this file and defines them
// first. In the product both are the constant 0 and compile away.
#ifndef GOATCAM_STARTUP_FAULT
#define GOATCAM_STARTUP_FAULT() 0   // nonzero: treat GdiplusStartup as failed
#endif
#ifndef GOATCAM_WRAP_FAULT
#define GOATCAM_WRAP_FAULT() 0      // 1: the back-buffer wrapper allocation returns null; 2: its status is not Ok
#endif

// ===========================================================================
// Content. v1 of GOAT Cam is camera-only: never a microphone, never a price.
// ===========================================================================
const wchar_t* const kUrl   = L"https://goatcam.dagoat.io";
const wchar_t* const kLabel = L"GOAT Cam — open goatcam.dagoat.io";   // tooltip + accessible name

const wchar_t* const kLine1a = L"GOAT Cam — your iPhone, ";
const wchar_t* const kLine1b = L"NOW";
const wchar_t* const kLine1c = L" your PC’s webcam.";
const wchar_t* const kLine2  = L"Free Windows 11 app · USB or Wi‑Fi · Scan for the iPhone app.";
const wchar_t* const kName   = L"GOAT Cam";
const wchar_t* const kTag    = L"IPHONE APP · ON THE APP STORE";
const wchar_t* const kCap1   = L"iPhone app";
const wchar_t* const kCap2   = L"On the App Store";
const wchar_t* const kOsd    = L"1080p · 60 fps";

// ===========================================================================
// The App Store QR.
//
// GENERATED from brand\goatcam-banner\assets\qr-matrix.json (its "rows" array, verbatim) by
//   python -I -c "import json; [print('    \"%s\",' % r) for r in json.load(open('qr-matrix.json'))['rows']]"
// Do not edit by hand. 37 x 37 modules INCLUDING the 4-module quiet zone, '1' = dark.
// [M] the matrix decodes to https://apps.apple.com/app/id6816268316.
// ===========================================================================
constexpr int kQrN = 37;
constexpr const char* kQr[kQrN] = {
    "0000000000000000000000000000000000000",
    "0000000000000000000000000000000000000",
    "0000000000000000000000000000000000000",
    "0000000000000000000000000000000000000",
    "0000111111101100010010101011111110000",
    "0000100000101010101001010010000010000",
    "0000101110101000011110011010111010000",
    "0000101110100010111101011010111010000",
    "0000101110101001000001110010111010000",
    "0000100000100111000011101010000010000",
    "0000111111101010101010101011111110000",
    "0000000000000100110110000000000000000",
    "0000100111111111100101000100101110000",
    "0000010111001001100110010000101100000",
    "0000011101100001010101100010001000000",
    "0000111111010110000111110001110010000",
    "0000101100110101111100011010000010000",
    "0000000010010110001100000011111110000",
    "0000111111100110101110110110101010000",
    "0000100100001100111100110110101010000",
    "0000000110111110100110111000010000000",
    "0000110111010100100110110100101100000",
    "0000111000111111111100010111110010000",
    "0000111101000011110000010000111000000",
    "0000111001101101010101011111111100000",
    "0000000000001000010111101000110000000",
    "0000111111101010110101111010110000000",
    "0000100000101010110011011000100100000",
    "0000101110101110010000011111110010000",
    "0000101110101100101100111101000010000",
    "0000101110100010101110001001101110000",
    "0000100000100110011100100011111010000",
    "0000111111101101001110111101100000000",
    "0000000000000000000000000000000000000",
    "0000000000000000000000000000000000000",
    "0000000000000000000000000000000000000",
    "0000000000000000000000000000000000000",
};

constexpr bool qrWellFormed() {
    for (int y = 0; y < kQrN; ++y) {
        if (kQr[y] == nullptr) return false;
        for (int x = 0; x < kQrN; ++x)
            if (kQr[y][x] != '0' && kQr[y][x] != '1') return false;
        if (kQr[y][kQrN] != '\0') return false;
    }
    return true;
}
static_assert(qrWellFormed(), "kQr must be 37 rows of exactly 37 '0'/'1' - regenerate it");

// ===========================================================================
// Palette - site\public\styles.css :root.
// ===========================================================================
struct Rgb { BYTE r, g, b; };
constexpr Rgb kEspresso950 {0x14, 0x0d, 0x08};
constexpr Rgb kEspresso900 {0x1c, 0x13, 0x0c};
constexpr Rgb kEspresso800 {0x26, 0x1a, 0x11};
constexpr Rgb kCaramel     {0xc9, 0x8a, 0x46};
constexpr Rgb kCaramelDeep {0x8f, 0x4f, 0x1c};
constexpr Rgb kGold        {0xf0, 0xc0, 0x7a};
constexpr Rgb kCream       {0xf8, 0xee, 0xdf};
constexpr Rgb kCreamSoft   {0xd8, 0xc2, 0xa5};
constexpr Rgb kGlyph       {0xf6, 0xef, 0xe6};   // 羊 / thumbs-up
constexpr Rgb kQrLight     {0xfb, 0xf5, 0xea};
constexpr Rgb kQrDark      {0x1c, 0x13, 0x0c};
constexpr Rgb kCaptionSub  {0xb0, 0x98, 0x78};   // .app-qr figcaption span
constexpr Rgb kInkMuted    {0x5c, 0x46, 0x32};   // copy line 2 on a light host, 7.7:1 on #f0f0f0
constexpr Rgb kBlack       {0x00, 0x00, 0x00};

inline double clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }
inline BYTE chan(double v) {
    const long i = std::lround(v);
    return static_cast<BYTE>(i < 0 ? 0 : (i > 255 ? 255 : i));
}
inline COLORREF cr(Rgb c) { return RGB(c.r, c.g, c.b); }
inline gp::Color gc(Rgb c, double a = 1.0) { return gp::Color(chan(clamp01(a) * 255.0), c.r, c.g, c.b); }
inline Rgb mix(Rgb a, Rgb b, double t) {
    t = clamp01(t);
    return Rgb{chan(a.r + (b.r - a.r) * t), chan(a.g + (b.g - a.g) * t), chan(a.b + (b.b - a.b) * t)};
}

// One 96-DPI design pixel at `dpi`.
inline int   px(double design, int dpi)  { return static_cast<int>(std::lround(design * dpi / 96.0)); }
inline float pxf(double design, int dpi) { return static_cast<float>(design * dpi / 96.0); }

// ===========================================================================
// Design metrics, in 96-DPI px. Everything scales from these through px().
// [M] Text widths at 96 DPI (GDI+ MeasureString, this machine): "GOAT Cam" Palatino
// Linotype 21 px ~111; the tagline Segoe UI Semibold 8 px ~124 + 28 px tracking; copy
// line 1 ~298, line 2 ~315 - all inside the 400 px column.
// ===========================================================================
constexpr int kWidth       = 400;   // the sensor list's width, which the banner sits under
constexpr int kLine1H      = 18;
constexpr int kLine2H      = 16;
constexpr int kCopyGap     = 6;
constexpr int kRise        = 2;     // hover lift
constexpr int kShadow      = 5;     // room under the plate for the lift's shadow
constexpr int kPlateH      = 116;
constexpr int kPlateRadius = 10;
constexpr int kPhotoW      = 106;
constexpr int kMarkGap     = 12;    // photo -> the mark's ink
constexpr int kMarkH       = 76;    // .goat-lockup__mark height (the 250-unit viewBox)
constexpr int kWordGap     = 8;     // the seal's right edge -> the wordmark
constexpr int kTagGap      = 6;     // the mark's ink -> the tagline
constexpr int kTile        = 74;    // QR tile: 37 modules x 2 px at 96 DPI
constexpr int kTilePad     = 9;
constexpr int kBracketArm  = 11;
constexpr int kBracketW    = 2;
constexpr double kBracketRest  = 6.0;   // inset from the photo edge
constexpr double kBracketLock  = 9.0;   // focus lock: tightens INWARD by 3 px on hover, as the master CSS

int totalHeight(int dpi) {
    return px(kLine1H, dpi) + px(kLine2H, dpi) + px(kCopyGap, dpi) + px(kRise, dpi) +
           px(kPlateH, dpi) + px(kShadow, dpi);
}

// ===========================================================================
// Motion constants. Leaving is always faster than arriving.
// ===========================================================================
constexpr double kOutMs        = 360.0;   // --gl-t-out
constexpr double kPushInMs     = 1200.0;  // photo push-in
constexpr double kPushScale    = 0.06;    // scale(1) -> scale(1.06)
constexpr double kLiftInMs     = 420.0;
constexpr double kBracketInMs  = 300.0;   // "focus lock"
constexpr double kBracketOutMs = 240.0;
constexpr double kBloomMs      = 690.0;   // seal face + glow
constexpr double kFadeMs       = 450.0;   // --gl-t-fade
constexpr double kThumbDelayMs = 180.0;
constexpr double kAccentInMs   = 630.0;   // tagline colour, QR ring
constexpr double kGlowPeak     = 0.7;

// The spark (goat-lockup-hover.css gl-ray / gl-ring / gl-ember), copied from Game Optimizer.
constexpr double kRayMs = 690.0;
constexpr double kRayLagMs[6] = {720, 765, 738, 792, 752, 816};
constexpr double kRingMs = 930.0, kRingLagMs = 705.0;
constexpr double kEmberMs = 1350.0;
constexpr double kEmberLagMs[3] = {840, 975, 908};
constexpr double kEmberDx[3] = {-13, 15, 3};
constexpr double kEmberDy[3] = {-28, -21, -34};
constexpr double kSparkWindowMs = 975.0 + 1350.0;   // the last ember's lag + its duration

// ===========================================================================
// Easing - the stylesheet's cubic-bezier curves, solved properly (Game Optimizer's solver).
// ===========================================================================
double bezComponent(double t, double a, double b) {
    const double u = 1.0 - t;
    return 3.0 * u * u * t * a + 3.0 * u * t * t * b + t * t * t;
}
double bezDerivative(double t, double a, double b) {
    const double u = 1.0 - t;
    return 3.0 * u * u * a + 6.0 * u * t * (b - a) + 3.0 * t * t * (1.0 - b);
}
double cubicBezier(double x, double x1, double y1, double x2, double y2) {
    if (x <= 0.0) return 0.0;
    if (x >= 1.0) return 1.0;
    double t = x;
    for (int i = 0; i < 8; ++i) {
        const double err = bezComponent(t, x1, x2) - x;
        if (err < 1e-7 && err > -1e-7) break;
        const double d = bezDerivative(t, x1, x2);
        if (d < 1e-7 && d > -1e-7) break;
        t -= err / d;
        if (t < 0.0) t = 0.0;
        if (t > 1.0) t = 1.0;
    }
    return bezComponent(t, y1, y2);
}
double easeOut(double t) { return cubicBezier(t, 0.16, 1.0, 0.3, 1.0); }   // --ease-out = --gl-in
double glOut(double t)   { return cubicBezier(t, 0.4, 0.0, 0.2, 1.0); }    // --gl-out
double linear(double t)  { return t; }
using Ease = double (*)(double);

// One CSS-style transition of a 0..1 value. Retargeting starts from wherever the value is
// NOW, and a part-finished transition reverses in the matching part of its duration (CSS
// "reversing shortening"), so a quick in-and-out never jumps.
struct Tween {
    double from = 0.0, to = 0.0, t0 = 0.0, delay = 0.0, dur = 0.0;
    Ease ease = linear;

    double at(double now) const {
        if (dur <= 0.0) return to;
        const double t = (now - t0 - delay) / dur;
        if (t <= 0.0) return from;
        if (t >= 1.0) return to;
        return from + (to - from) * ease(t);
    }
    bool busy(double now) const { return dur > 0.0 && now < t0 + delay + dur; }
    void go(double now, double target, double fullMs, Ease e, double delayMs = 0.0) {
        const double cur = at(now);
        from = cur; to = target; t0 = now; delay = delayMs; ease = e;
        dur = fullMs * std::fabs(target - cur);
        if (dur < 1.0) { from = to; dur = 0.0; delay = 0.0; }
    }
};

struct Motion {
    Tween push, lift, bracket;            // travelling parts: off under reduced motion
    Tween face, glow, chr, thumb, accent; // fades: always
    double sparkT0 = -1.0;                // when the spark fired; < 0 = not firing
    bool active = false;

    // `travel` is SPI_GETCLIENTAREAANIMATION. Off = the cross-fades only.
    void setActive(bool on, double now, bool travel) {
        if (on == active) return;
        active = on;
        if (on) {
            if (travel) {
                push.go(now, 1, kPushInMs, easeOut);
                lift.go(now, 1, kLiftInMs, easeOut);
                bracket.go(now, 1, kBracketInMs, easeOut);
                sparkT0 = now;
            }
            face.go(now, 1, kBloomMs, glOut);      // fill: 690ms, the base rule's --gl-out
            glow.go(now, 1, kBloomMs, easeOut);    // opacity 690ms --gl-in
            chr.go(now, 1, kFadeMs, linear);
            thumb.go(now, 1, kFadeMs, linear, kThumbDelayMs);
            accent.go(now, 1, kAccentInMs, glOut);
        } else {
            push.go(now, 0, kOutMs, glOut);
            lift.go(now, 0, kOutMs, glOut);
            bracket.go(now, 0, kBracketOutMs, glOut);
            face.go(now, 0, kOutMs, glOut);
            glow.go(now, 0, kOutMs, glOut);
            chr.go(now, 0, kOutMs, glOut);
            thumb.go(now, 0, kOutMs, glOut);
            accent.go(now, 0, kOutMs, glOut);
            sparkT0 = -1.0;                        // .gl-burst has no out transition
        }
    }

    bool sparkOn(double now) const { return sparkT0 >= 0.0 && now - sparkT0 < kSparkWindowMs; }

    // THE single gate on the animation timer.
    bool busy(double now) const {
        return push.busy(now) || lift.busy(now) || bracket.busy(now) || face.busy(now) ||
               glow.busy(now) || chr.busy(now) || thumb.busy(now) || accent.busy(now) ||
               sparkOn(now);
    }
};

double nowMs() {
    static const double perMs = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return static_cast<double>(f.QuadPart) / 1000.0;
    }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return static_cast<double>(c.QuadPart) / perMs;
}

// ===========================================================================
// A minimal SVG path-data reader - Game Optimizer's, minus the arc command the lockup does
// not use. It reads M L H V C S Q T Z (absolute and relative) and flattens curves to
// polylines once, in source units.
// ===========================================================================
struct PtF { double x = 0.0, y = 0.0; };
typedef std::vector<PtF>     SubPath;
typedef std::vector<SubPath> Contours;

constexpr int kCubicSteps = 20;
constexpr int kQuadSteps  = 14;

class PathReader {
public:
    explicit PathReader(const char* d) : s_(d) {}

    void run(Contours& out) {
        char cmd = 0;
        while (true) {
            skipSep();
            if (*s_ == '\0') break;
            if (isAlpha(*s_)) {
                cmd = *s_;
                ++s_;
            } else if (cmd == 'M') {
                cmd = 'L';   // repeated moveto pairs are implicit linetos
            } else if (cmd == 'm') {
                cmd = 'l';
            } else if (cmd == 0) {
                break;       // leading garbage: refuse rather than guess
            }
            if (!command(cmd, out)) break;
        }
        flush(out);
    }

private:
    static bool isAlpha(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
    static bool isSep(char c)   { return c == ' ' || c == ',' || c == '\t' || c == '\r' || c == '\n'; }
    void skipSep() { while (*s_ != '\0' && isSep(*s_)) ++s_; }

    bool num(double& out) {
        skipSep();
        const char* p = s_;
        if (*p == '+' || *p == '-') ++p;
        if (!((*p >= '0' && *p <= '9') || *p == '.')) return false;
        char* end = nullptr;
        out = std::strtod(s_, &end);
        if (end == nullptr || end == s_) return false;
        s_ = end;
        return true;
    }

    void emit(double x, double y) {
        cur_.x = x;
        cur_.y = y;
        open_.push_back(cur_);
    }

    void flush(Contours& out) {
        if (open_.size() >= 2) out.push_back(open_);
        open_.clear();
    }

    void cubic(double x1, double y1, double x2, double y2, double x, double y) {
        const double x0 = cur_.x, y0 = cur_.y;
        for (int i = 1; i <= kCubicSteps; ++i) {
            const double t = static_cast<double>(i) / kCubicSteps, u = 1.0 - t;
            emit(u * u * u * x0 + 3 * u * u * t * x1 + 3 * u * t * t * x2 + t * t * t * x,
                 u * u * u * y0 + 3 * u * u * t * y1 + 3 * u * t * t * y2 + t * t * t * y);
        }
        c1_.x = x2; c1_.y = y2;
        hasCubicCtl_ = true;
        hasQuadCtl_  = false;
    }

    void quad(double x1, double y1, double x, double y) {
        const double x0 = cur_.x, y0 = cur_.y;
        for (int i = 1; i <= kQuadSteps; ++i) {
            const double t = static_cast<double>(i) / kQuadSteps, u = 1.0 - t;
            emit(u * u * x0 + 2 * u * t * x1 + t * t * x, u * u * y0 + 2 * u * t * y1 + t * t * y);
        }
        q1_.x = x1; q1_.y = y1;
        hasQuadCtl_  = true;
        hasCubicCtl_ = false;
    }

    bool command(char cmd, Contours& out) {
        const bool rel = (cmd >= 'a' && cmd <= 'z');
        const char c = static_cast<char>(rel ? cmd - 32 : cmd);
        double a[6];
        const double bx = rel ? cur_.x : 0.0, by = rel ? cur_.y : 0.0;

        switch (c) {
        case 'M':
            if (!num(a[0]) || !num(a[1])) return false;
            flush(out);
            start_.x = bx + a[0];
            start_.y = by + a[1];
            cur_ = start_;
            open_.push_back(start_);
            hasCubicCtl_ = hasQuadCtl_ = false;
            return true;
        case 'L':
            if (!num(a[0]) || !num(a[1])) return false;
            emit(bx + a[0], by + a[1]);
            hasCubicCtl_ = hasQuadCtl_ = false;
            return true;
        case 'H':
            if (!num(a[0])) return false;
            emit(bx + a[0], cur_.y);
            hasCubicCtl_ = hasQuadCtl_ = false;
            return true;
        case 'V':
            if (!num(a[0])) return false;
            emit(cur_.x, by + a[0]);
            hasCubicCtl_ = hasQuadCtl_ = false;
            return true;
        case 'C':
            for (int i = 0; i < 6; ++i) if (!num(a[i])) return false;
            cubic(bx + a[0], by + a[1], bx + a[2], by + a[3], bx + a[4], by + a[5]);
            return true;
        case 'S': {
            for (int i = 0; i < 4; ++i) if (!num(a[i])) return false;
            const double rx = hasCubicCtl_ ? 2 * cur_.x - c1_.x : cur_.x;
            const double ry = hasCubicCtl_ ? 2 * cur_.y - c1_.y : cur_.y;
            cubic(rx, ry, bx + a[0], by + a[1], bx + a[2], by + a[3]);
            return true;
        }
        case 'Q':
            for (int i = 0; i < 4; ++i) if (!num(a[i])) return false;
            quad(bx + a[0], by + a[1], bx + a[2], by + a[3]);
            return true;
        case 'T': {
            if (!num(a[0]) || !num(a[1])) return false;
            const double rx = hasQuadCtl_ ? 2 * cur_.x - q1_.x : cur_.x;
            const double ry = hasQuadCtl_ ? 2 * cur_.y - q1_.y : cur_.y;
            quad(rx, ry, bx + a[0], by + a[1]);
            return true;
        }
        case 'Z':
            if (!open_.empty()) {
                emit(start_.x, start_.y);
                flush(out);
            }
            cur_ = start_;
            open_.push_back(start_);
            hasCubicCtl_ = hasQuadCtl_ = false;
            return true;
        default:
            return false;   // an unsupported command: stop rather than draw nonsense
        }
    }

    const char* s_;
    PtF  cur_{}, start_{}, c1_{}, q1_{};
    bool hasCubicCtl_ = false;
    bool hasQuadCtl_  = false;
    SubPath open_;
};

Contours parsePath(const char* d) {
    Contours out;
    PathReader(d).run(out);
    return out;
}

// ===========================================================================
// The artwork. Every string is the `d` attribute of the matching element in
// GOAT_LOCKUP_MARKUP (widget.js), as copied into Game Optimizer's kArt.
// ===========================================================================
namespace art {

// .gl-head - five paths plus one eye circle, outer viewBox "46 -16 220 250".
const char* const kHead[5] = {
    "M62 210 C74 148 120 98 190 80 C152 106 106 150 82 216 Z",
    "M186 84 C214 62 226 32 210 8 C216 36 200 62 174 80 Z",
    "M170 82 C190 60 194 34 180 16 C186 40 172 62 156 78 Z",
    "M188 82 C206 90 218 104 222 122 C212 106 200 94 182 90 Z",
    "M206 122 C215 134 217 149 209 160 C212 147 208 133 200 127 Z",
};
constexpr double kEyeCx = 197.0, kEyeCy = 99.0, kEyeR = 3.6;

constexpr double kVbX = 46.0, kVbY = -16.0, kVbW = 220.0, kVbH = 250.0;
// The ink's own box inside the viewBox (head x 62.., seal right edge 250; y 8..218).
constexpr double kInkL = 62.0, kInkR = 250.0, kInkT = 8.0, kInkB = 218.0;

// .gl-seal-glow / .gl-seal-face - x=214 y=182 w=36 h=36 rx=4.
constexpr double kSealX = 214.0, kSealY = 182.0, kSealW = 36.0, kSealRx = 4.0;
// .gl-glyph - an inner <svg> at the seal, 36x36, viewBox "0 0 100 100".
constexpr double kInner = 0.36;

// .gl-thumb, with its own transform="translate(-5 -3)".
const char* const kThumb =
    "M30 50 C26 50 24 52 24 56 L24 87 C24 91 26 93 30 93 L73 93 C79 93 84 90 85 84 "
    "L89 55 C90 49 86 44 80 44 L63 44 C65 38 67 25 65 18 C63 12 55 12 53 18 "
    "C51 24 53 33 51 42 C50 47 47 50 42 50 Z";
constexpr double kThumbDx = -5.0, kThumbDy = -3.0;

// .gl-char - the SimSun 羊 outline baked to a path.
const char* const kChar =
    "M33.5 18.12Q39.14 21.51 41.54 23.62Q43.93 25.74 43.65 27.85Q43.37 29.97 42.24 30.96"
    "Q41.11 31.94 40.55 31.94Q39.42 31.94 38.57 29.12Q37.45 25.17 32.93 18.97Z"
    "M20.52 33.07H52.68Q58.89 23.76 60.86 16.99L66.79 20.66Q63.96 22.07 61.14 25.74"
    "Q58.04 29.41 54.37 33.07H68.2L72.43 28.84L77.79 34.77H51.55V47.46H63.96L68.2 43.79"
    "L72.99 49.15H51.55V62.13H71.58L76.1 57.9L81.46 63.82H51.55V73.7Q51.55 76.8 51.83 81.32"
    "L47.04 83.01Q47.32 78.78 47.32 63.82H27.85Q24.19 63.82 21.08 64.67L18.54 62.13H47.32"
    "V49.15H35.47Q31.8 49.15 28.7 50L26.16 47.46H47.32V34.77H29.83Q26.16 34.77 23.06 35.61Z";

// .gl-burst, transform="translate(53 0)", inside the 100x100 glyph viewBox.
constexpr double kBurstCx = 53.0, kBurstCy = 0.0;
constexpr double kRingR = 12.0, kRingStroke = 3.0;
constexpr double kRayR0 = 10.0, kRayR1 = 40.0, kRayStroke = 3.2;
constexpr double kRayDeg[6] = {-168.0, -138.0, -108.0, -76.0, -44.0, -14.0};
constexpr double kRayDashOn = 9.0, kRayDashOff = 32.0;
constexpr double kEmberCx[3] = {-7.0, 8.0, 0.0};
constexpr double kEmberCy[3] = {-2.0, -5.0, -9.0};
constexpr double kEmberR[3]  = {2.2, 1.9, 1.6};

}  // namespace art

// Source contours -> one GDI+ path, x' = x*sx + tx, y' = y*sy + ty. Nonzero fill, as SVG.
void addContours(gp::GraphicsPath& p, const Contours& c, double sx, double sy, double tx, double ty) {
    for (const SubPath& sp : c) {
        if (sp.size() < 3) continue;
        std::vector<gp::PointF> pts;
        pts.reserve(sp.size());
        for (const PtF& q : sp)
            pts.push_back(gp::PointF(static_cast<float>(q.x * sx + tx), static_cast<float>(q.y * sy + ty)));
        p.StartFigure();
        p.AddLines(pts.data(), static_cast<INT>(pts.size()));
        p.CloseFigure();
    }
}

void addRoundRect(gp::GraphicsPath& p, const gp::RectF& r, float rad) {
    rad = std::min(rad, std::min(r.Width, r.Height) / 2.0f);
    if (rad <= 0.0f) { p.AddRectangle(r); return; }
    const float d = rad * 2.0f;
    p.StartFigure();
    p.AddArc(r.X, r.Y, d, d, 180.0f, 90.0f);
    p.AddArc(r.X + r.Width - d, r.Y, d, d, 270.0f, 90.0f);
    p.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0.0f, 90.0f);
    p.AddArc(r.X, r.Y + r.Height - d, d, d, 90.0f, 90.0f);
    p.CloseFigure();
}

gp::RectF rectF(const RECT& r) {
    return gp::RectF(static_cast<float>(r.left), static_cast<float>(r.top),
                     static_cast<float>(r.right - r.left), static_cast<float>(r.bottom - r.top));
}

// Mark-local geometry for one DPI: x' = (x - 46) * s, y' = (y + 16) * s.
struct Art {
    int dpi = 0;
    bool ok = false;   // every path below was allocated and built
    float s = 0.0f, inner = 0.0f;
    std::unique_ptr<gp::GraphicsPath> head, chr, thumb, seal;
    gp::RectF sealRc;
    gp::PointF burst;

    void build(int d) {
        dpi = d;
        ok = false;
        head.reset(); chr.reset(); thumb.reset(); seal.reset();
        s = pxf(kMarkH, d) / static_cast<float>(art::kVbH);
        inner = s * static_cast<float>(art::kInner);
        const double ox = -art::kVbX * s, oy = -art::kVbY * s;
        sealRc = gp::RectF(static_cast<float>(art::kSealX * s + ox), static_cast<float>(art::kSealY * s + oy),
                           static_cast<float>(art::kSealW * s), static_cast<float>(art::kSealW * s));
        burst = gp::PointF(sealRc.X + static_cast<float>(art::kBurstCx) * inner,
                           sealRc.Y + static_cast<float>(art::kBurstCy) * inner);
        if (!g_gdiplusOk) return;   // the layout still needs s; the paths need GDI+

        // GDI+ objects come from GdipAlloc, which can return null: check before any use.
        head.reset(new gp::GraphicsPath(gp::FillModeWinding));
        seal.reset(new gp::GraphicsPath());
        chr.reset(new gp::GraphicsPath(gp::FillModeWinding));
        thumb.reset(new gp::GraphicsPath(gp::FillModeWinding));
        if (!head || !seal || !chr || !thumb) return;

        for (const char* d0 : art::kHead) addContours(*head, parsePath(d0), s, s, ox, oy);
        head->AddEllipse(static_cast<float>((art::kEyeCx - art::kEyeR) * s + ox),
                         static_cast<float>((art::kEyeCy - art::kEyeR) * s + oy),
                         static_cast<float>(2 * art::kEyeR * s), static_cast<float>(2 * art::kEyeR * s));
        addRoundRect(*seal, sealRc, static_cast<float>(art::kSealRx * s));
        addContours(*chr, parsePath(art::kChar), inner, inner, sealRc.X, sealRc.Y);
        addContours(*thumb, parsePath(art::kThumb), inner, inner,
                    sealRc.X + art::kThumbDx * inner, sealRc.Y + art::kThumbDy * inner);
        ok = true;
    }
};

// ===========================================================================
// Fonts. GDI substitutes silently when a face is missing, so each candidate is verified
// with GetTextFaceW (Game Optimizer's resolve-then-verify).
// ===========================================================================
HFONT makeFont(std::initializer_list<const wchar_t*> faces, int pxH, int weight, bool italic) {
    HDC screen = GetDC(nullptr);
    HFONT chosen = nullptr;
    for (const wchar_t* face : faces) {
        HFONT f = CreateFontW(-std::max(1, pxH), 0, 0, 0, weight, italic ? TRUE : FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              DEFAULT_PITCH | FF_DONTCARE, face);
        if (f == nullptr) continue;
        if (screen == nullptr) { chosen = f; break; }
        HGDIOBJ old = SelectObject(screen, f);
        wchar_t actual[LF_FACESIZE] = {};
        GetTextFaceW(screen, LF_FACESIZE, actual);
        SelectObject(screen, old);
        if (lstrcmpiW(actual, face) == 0) { chosen = f; break; }
        DeleteObject(f);
    }
    if (chosen == nullptr)
        chosen = CreateFontW(-std::max(1, pxH), 0, 0, 0, weight, italic ? TRUE : FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    if (screen != nullptr) ReleaseDC(nullptr, screen);
    return chosen;
}

#define GC_SERIF L"Palatino Linotype", L"Book Antiqua", L"Georgia"
#define GC_MONO  L"Cascadia Mono", L"Consolas"

struct Fonts {
    int dpi = 0;
    HFONT line1 = nullptr, now = nullptr, line2 = nullptr, osd = nullptr;
    HFONT name = nullptr, tag = nullptr, cap1 = nullptr, cap2 = nullptr;

    Fonts() = default;
    Fonts(const Fonts&) = delete;
    Fonts& operator=(const Fonts&) = delete;
    ~Fonts() { destroy(); }

    void destroy() {
        for (HFONT* f : {&line1, &now, &line2, &osd, &name, &tag, &cap1, &cap2}) {
            if (*f != nullptr) DeleteObject(*f);
            *f = nullptr;
        }
        dpi = 0;
    }
    void build(int d) {
        destroy();
        dpi   = d;
        line1 = makeFont({L"Segoe UI"}, px(12.5, d), FW_SEMIBOLD, false);
        now   = makeFont({GC_SERIF}, px(13.5, d), FW_BOLD, true);      // the site h1's gold italic NOW
        line2 = makeFont({L"Segoe UI"}, px(11.5, d), FW_NORMAL, false);
        osd   = makeFont({GC_MONO}, px(8, d), FW_NORMAL, false);
        name  = makeFont({GC_SERIF}, px(21, d), FW_NORMAL, false);     // --serif, not uppercase
        tag   = makeFont({L"Segoe UI"}, px(8, d), FW_SEMIBOLD, false); // tracked micro-caps, sans
        cap1  = makeFont({L"Segoe UI"}, px(9.5, d), FW_SEMIBOLD, false);
        cap2  = makeFont({L"Segoe UI"}, px(9.5, d), FW_NORMAL, false);
    }
};

TEXTMETRICW metricsOf(HDC dc, HFONT f) {
    TEXTMETRICW tm{};
    HGDIOBJ old = SelectObject(dc, f);
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, old);
    return tm;
}

int capHeightOf(HDC dc, HFONT f) {
    HGDIOBJ old = SelectObject(dc, f);
    OUTLINETEXTMETRICW otm{};
    otm.otmSize = sizeof(otm);
    int cap = 0;
    if (GetOutlineTextMetricsW(dc, sizeof(otm), &otm) != 0) cap = static_cast<int>(otm.otmsCapEmHeight);
    if (cap <= 0) {
        TEXTMETRICW tm{};
        GetTextMetricsW(dc, &tm);
        cap = (tm.tmAscent - tm.tmInternalLeading) * 7 / 10;
    }
    SelectObject(dc, old);
    return cap;
}

int textWidth(HDC dc, HFONT f, const wchar_t* s, int track = 0) {
    HGDIOBJ old = SelectObject(dc, f);
    const int oldExtra = SetTextCharacterExtra(dc, track);
    SIZE sz{};
    GetTextExtentPoint32W(dc, s, static_cast<int>(wcslen(s)), &sz);
    SetTextCharacterExtra(dc, oldExtra);
    SelectObject(dc, old);
    return static_cast<int>(sz.cx);
}

void drawText(HDC dc, HFONT f, int x, int y, const wchar_t* s, COLORREF col, UINT align, int track = 0) {
    HGDIOBJ old = SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, col);
    SetTextAlign(dc, align);
    const int oldExtra = SetTextCharacterExtra(dc, track);
    TextOutW(dc, x, y, s, static_cast<int>(wcslen(s)));
    SetTextCharacterExtra(dc, oldExtra);
    SelectObject(dc, old);
}

// ===========================================================================
// Layout for one (dpi, width). Every rect is at REST; the paint offsets the plate by the
// lift. Rects are in control client coordinates.
// ===========================================================================
struct Layout {
    int dpi = 0, w = 0, h = 0;
    int line1Base = 0, line2Base = 0;
    RECT plate{}, hit{};
    int rise = 0, radius = 0;
    RECT photo{};
    bool osdOn = false;
    gp::RectF osdPill, osdDot;
    POINT osdText{};
    float markX = 0.0f, markY = 0.0f;
    POINT nameBase{}, tagPos{};
    int tagTrack = 0;
    RECT tile{};
    int module = 0;
    POINT qrOrg{}, cap1{}, cap2{};

    void build(HDC dc, const Fonts& F, const Art& A, int d, int width) {
        dpi = d;
        w = width;
        h = totalHeight(d);
        auto P = [d](double v) { return px(v, d); };

        const TEXTMETRICW tm1 = metricsOf(dc, F.line1), tm2 = metricsOf(dc, F.line2);
        line1Base = (P(kLine1H) - tm1.tmHeight) / 2 + tm1.tmAscent;
        line2Base = P(kLine1H) + (P(kLine2H) - tm2.tmHeight) / 2 + tm2.tmAscent;

        rise = P(kRise);
        radius = P(kPlateRadius);
        const int top = P(kLine1H) + P(kLine2H) + P(kCopyGap) + rise;
        plate = RECT{0, top, w, top + P(kPlateH)};
        hit = plate;
        hit.top -= rise;

        // The photo is flush with the plate's left edge, inside its 1 px border.
        photo = RECT{1, top + 1, 1 + P(kPhotoW), plate.bottom - 1};

        // OSD readout, top-left inside the viewfinder - only when it fits.
        const TEXTMETRICW tmo = metricsOf(dc, F.osd);
        const float pillH = static_cast<float>(tmo.tmHeight + P(4));
        const float dot = pxf(5, d);
        const float textW = static_cast<float>(textWidth(dc, F.osd, kOsd));
        // Just inside the LOCKED top-left bracket (inset 9 + a 2 px bar), so neither the rest
        // nor the hover frame puts a bracket arm under the pill. Its right edge must clear the
        // locked top-right bracket's vertical bar - the paint's own rounding of both.
        const float lockIn = static_cast<float>(std::lround(pxf(kBracketLock, d)));
        const float limit = photo.right - lockIn - std::max(1, P(kBracketW));
        const float x0 = static_cast<float>(photo.left + P(13));
        float padL = pxf(4, d), gap = pxf(3, d), padR = pxf(5, d);
        const float floorPad = pxf(2, d);
        const float excess = (x0 + padL + dot + gap + textW + padR) - limit;
        const float give = (padL - floorPad) + (gap - floorPad) + (padR - floorPad);
        if (excess > 0.0f && give > 0.0f) {   // too wide: shrink the padding, never move the pill
            const float k = std::max(0.0f, 1.0f - excess / give);
            padL = floorPad + (padL - floorPad) * k;
            gap  = floorPad + (gap - floorPad) * k;
            padR = floorPad + (padR - floorPad) * k;
        }
        const float pillW = padL + dot + gap + textW + padR;
        osdPill = gp::RectF(x0, static_cast<float>(photo.top + P(12)), pillW, pillH);
        osdDot  = gp::RectF(osdPill.X + padL, osdPill.Y + (pillH - dot) / 2.0f, dot, dot);
        osdText = POINT{static_cast<LONG>(std::lround(osdDot.X + dot + gap)),
                        static_cast<LONG>(std::lround(osdPill.Y + (pillH - tmo.tmHeight) / 2.0f))};
        osdOn = osdPill.X + osdPill.Width <= limit + 0.01f;   // still too wide at the floor: no OSD

        // Lockup: the mark, the wordmark beside it, the tagline under both.
        const float s = A.s;
        const TEXTMETRICW tmt = metricsOf(dc, F.tag);
        const float inkH = static_cast<float>((art::kInkB - art::kInkT) * s);
        const float block = inkH + pxf(kTagGap, d) + tmt.tmHeight;
        const float blockTop = plate.top + (P(kPlateH) - block) / 2.0f;
        markX = static_cast<float>(photo.right + P(kMarkGap) - (art::kInkL - art::kVbX) * s);
        markY = static_cast<float>(blockTop - (art::kInkT - art::kVbY) * s);

        const float inkMidY = markY + static_cast<float>(((art::kInkT + art::kInkB) / 2.0 - art::kVbY) * s);
        nameBase = POINT{static_cast<LONG>(std::lround(markX + (art::kInkR - art::kVbX) * s + P(kWordGap))),
                         static_cast<LONG>(std::lround(inkMidY + capHeightOf(dc, F.name) / 2.0f))};
        tagPos = POINT{static_cast<LONG>(std::lround(markX + (art::kInkL - art::kVbX) * s)),
                       static_cast<LONG>(std::lround(blockTop + inkH + pxf(kTagGap, d)))};
        tagTrack = std::max(1, P(1));

        // QR tile: an INTEGER module size, never resampled. The tile grows with the DPI and
        // any slack between it and the code is extra quiet zone, never a fractional module.
        const int side = P(kTile);
        module = std::max(2, side / kQrN);
        const int qr = module * kQrN;
        tile = RECT{w - P(kTilePad) - std::max(side, qr), top + P(8), w - P(kTilePad), top + P(8) + std::max(side, qr)};
        qrOrg = POINT{tile.left + (tile.right - tile.left - qr) / 2, tile.top + (tile.bottom - tile.top - qr) / 2};

        const TEXTMETRICW tmc = metricsOf(dc, F.cap1);
        const LONG cx = (tile.left + tile.right) / 2;
        cap1 = POINT{cx, tile.bottom + P(3)};
        cap2 = POINT{cx, cap1.y + tmc.tmHeight - P(1)};
    }
};

// ===========================================================================
// The photo: decoded once per process from the RCDATA resource into a PARGB bitmap, so
// nothing keeps the source stream alive.
// ===========================================================================
HINSTANCE g_inst = nullptr;
ULONG_PTR g_gdiplusToken = 0;
gp::Bitmap* g_photo = nullptr;   // ponytail: never freed - one ~650 KB decode for the process lifetime
bool g_photoTried = false;

bool loadPhotoBytes(const void* data, DWORD size) {
    if (data == nullptr || size == 0) return false;
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, size);
    if (mem == nullptr) return false;
    void* p = GlobalLock(mem);
    if (p == nullptr) { GlobalFree(mem); return false; }
    std::memcpy(p, data, size);
    GlobalUnlock(mem);
    IStream* stream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(mem, TRUE, &stream))) { GlobalFree(mem); return false; }
    bool ok = false;
    {
        gp::Bitmap src(stream);
        if (src.GetLastStatus() == gp::Ok && src.GetWidth() > 0 && src.GetHeight() > 0) {
            const INT w = static_cast<INT>(src.GetWidth()), h = static_cast<INT>(src.GetHeight());
            std::unique_ptr<gp::Bitmap> copy(new gp::Bitmap(w, h, PixelFormat32bppPARGB));
            if (copy && copy->GetLastStatus() == gp::Ok) {   // GdipAlloc can return null
                gp::Graphics g(copy.get());
                g.DrawImage(&src, 0, 0, w, h);
                g_photo = copy.release();
                ok = true;
            }
        }
    }
    stream->Release();   // frees `mem` too (fDeleteOnRelease)
    return ok;
}

gp::Bitmap* photo() {
    if (!g_gdiplusOk) return nullptr;
    if (!g_photoTried) {
        g_photoTried = true;
        if (HRSRC r = FindResourceW(g_inst, L"GOATCAM_COFFEE", MAKEINTRESOURCEW(10) /* RT_RCDATA */)) {
            if (HGLOBAL h = LoadResource(g_inst, r))
                loadPhotoBytes(LockResource(h), SizeofResource(g_inst, r));
        }
    }
    return g_photo;
}

// The cup's centre in the photo, as fractions: the crop is centred on it and the push-in
// scales about it.
constexpr float kFocusX = 0.48f, kFocusY = 0.46f;

// ===========================================================================
// Per-control state.
// ===========================================================================
struct State {
    HWND hwnd = nullptr, tip = nullptr;
    bool hot = false, pressed = false, focused = false, focusVisible = false, tracking = false, timer = false;
    Motion m;
    Fonts F;
    Art A;
    Layout L;

    // back buffer: a top-down 32bpp DIB, also wrapped as a GDI+ bitmap over the same bits
    HDC memDC = nullptr;
    HBITMAP dib = nullptr;
    HGDIOBJ oldBmp = nullptr;
    void* bits = nullptr;
    int bw = 0, bh = 0;
    std::unique_ptr<gp::Bitmap> wrap;

    State() = default;
    State(const State&) = delete;
    State& operator=(const State&) = delete;
    ~State() { freeBuffer(); }

    void freeBuffer() {
        wrap.reset();
        if (memDC != nullptr) {
            if (oldBmp != nullptr) SelectObject(memDC, oldBmp);
            DeleteDC(memDC);
        }
        if (dib != nullptr) DeleteObject(dib);
        memDC = nullptr; dib = nullptr; oldBmp = nullptr; bits = nullptr; bw = bh = 0;
    }

    // Builds the surface, then commits: on any failure everything is rolled back, so a later
    // call at the same size rebuilds instead of returning true on a half-built surface.
    bool ensureBuffer(int w, int h) {
        if (memDC != nullptr && bw == w && bh == h && (wrap || !g_gdiplusOk)) return true;
        freeBuffer();
        if (w <= 0 || h <= 0) return false;
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;   // top-down
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        dib = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (dib == nullptr || bits == nullptr) { freeBuffer(); return false; }
        memDC = CreateCompatibleDC(nullptr);
        if (memDC == nullptr) { freeBuffer(); return false; }
        oldBmp = SelectObject(memDC, dib);
        bw = w; bh = h;
        if (!g_gdiplusOk) return true;   // the plain GDI paint needs no wrapper
        const int fault = GOATCAM_WRAP_FAULT();
        wrap.reset(fault == 1 ? nullptr
                              : new gp::Bitmap(fault == 2 ? 0 : w, h, w * 4, PixelFormat32bppRGB,
                                               static_cast<BYTE*>(bits)));
        if (!wrap || wrap->GetLastStatus() != gp::Ok) { freeBuffer(); return false; }
        return true;
    }

    // Fonts, art and layout depend on the DPI and the width only.
    void ensureLayout(int dpi, int w) {
        if (F.dpi != dpi) F.build(dpi);
        if (A.dpi != dpi) A.build(dpi);
        if (L.dpi != dpi || L.w != w) {
            HDC screen = GetDC(nullptr);
            L.build(screen, F, A, dpi, w);
            if (screen != nullptr) ReleaseDC(nullptr, screen);
        }
    }
};

double luminance(COLORREF c) {
    auto lin = [](BYTE v) {
        const double s = v / 255.0;
        return s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * lin(GetRValue(c)) + 0.7152 * lin(GetGValue(c)) + 0.0722 * lin(GetBValue(c));
}

double segEase(double t, double a, double b, double va, double vb) {
    if (t <= a) return va;
    if (t >= b) return vb;
    return va + (vb - va) * easeOut((t - a) / (b - a));
}

// The spark, in mark-local coordinates; t = ms since it fired. Each element carries its
// own opacity and is drawn source-over, which is what CSS does with separate elements.
void drawSpark(gp::Graphics& g, const Art& A, double t) {
    const double u = A.inner;
    const float cx = A.burst.X, cy = A.burst.Y;

    for (int i = 0; i < 6; ++i) {   // rays: stroke-dashoffset 9 -> -32, opacity 0 -> 1 @22% -> 0
        const double p = (t - kRayLagMs[i]) / kRayMs;
        if (p <= 0.0 || p >= 1.0) continue;
        const double off = art::kRayDashOn + (-art::kRayDashOff - art::kRayDashOn) * easeOut(p);
        const double op = p < 0.22 ? segEase(p, 0.0, 0.22, 0.0, 1.0) : segEase(p, 0.22, 1.0, 1.0, 0.0);
        if (op <= 0.002) continue;
        gp::Pen pen(gc(kGold, op), static_cast<float>(art::kRayStroke * u));
        pen.SetStartCap(gp::LineCapRound);
        pen.SetEndCap(gp::LineCapRound);
        const double a = art::kRayDeg[i] * 3.14159265358979323846 / 180.0;
        const double dx = std::cos(a), dy = std::sin(a);
        const double period = art::kRayDashOn + art::kRayDashOff, len = art::kRayR1 - art::kRayR0;
        for (int k = -1; k <= 2; ++k) {
            double s0 = k * period - off, s1 = s0 + art::kRayDashOn;
            if (s1 <= 0.0 || s0 >= len) continue;
            s0 = std::max(s0, 0.0);
            s1 = std::min(s1, len);
            const double r0 = (art::kRayR0 + s0) * u, r1 = (art::kRayR0 + s1) * u;
            g.DrawLine(&pen, static_cast<float>(cx + dx * r0), static_cast<float>(cy + dy * r0),
                       static_cast<float>(cx + dx * r1), static_cast<float>(cy + dy * r1));
        }
    }

    {   // ring: scale 0.3 -> 1.9, opacity 0 -> 0.85 @18% -> 0
        const double p = (t - kRingLagMs) / kRingMs;
        if (p > 0.0 && p < 1.0) {
            const double k = 0.3 + 1.6 * easeOut(p);
            const double op = p < 0.18 ? segEase(p, 0.0, 0.18, 0.0, 0.85) : segEase(p, 0.18, 1.0, 0.85, 0.0);
            if (op > 0.002) {
                gp::Pen pen(gc(kGold, op), static_cast<float>(art::kRingStroke * k * u));
                const float r = static_cast<float>(art::kRingR * k * u);
                g.DrawEllipse(&pen, cx - r, cy - r, 2 * r, 2 * r);
            }
        }
    }

    for (int i = 0; i < 3; ++i) {   // embers: translate + scale 0.4 -> 1, opacity 0 -> 0.9 @25% -> 0
        const double p = (t - kEmberLagMs[i]) / kEmberMs;
        if (p <= 0.0 || p >= 1.0) continue;
        const double e = easeOut(p);
        const double op = p < 0.25 ? segEase(p, 0.0, 0.25, 0.0, 0.9) : segEase(p, 0.25, 1.0, 0.9, 0.0);
        if (op <= 0.002) continue;
        const double k = 0.4 + 0.6 * e;
        const float ex = cx + static_cast<float>((art::kEmberCx[i] + kEmberDx[i] * e) * u);
        const float ey = cy + static_cast<float>((art::kEmberCy[i] + kEmberDy[i] * e) * u);
        const float r = static_cast<float>(std::max(0.6, art::kEmberR[i] * k * u));
        gp::SolidBrush b(gc(kGold, op));
        g.FillEllipse(&b, ex - r, ey - r, 2 * r, 2 * r);
    }
}

// The plate without GDI+ (GdiplusStartup failed): the espresso plate and its border, the QR
// tile, the hover ring and the focus ring in plain GDI. No photo and no mark - those need GDI+.
void plainPlate(HDC dc, const RECT& plate, int radius, const RECT& tile, int tileRadius,
                double lift, double accent, bool focusRing, int dpi) {
    auto shape = [dc](const RECT& r, int rad, const Rgb* fill, Rgb line, int width) {
        HBRUSH b = fill != nullptr ? CreateSolidBrush(cr(*fill)) : nullptr;
        HPEN p = CreatePen(PS_SOLID, width, cr(line));
        HGDIOBJ ob = SelectObject(dc, b != nullptr ? static_cast<HGDIOBJ>(b) : GetStockObject(NULL_BRUSH));
        HGDIOBJ op = SelectObject(dc, p);
        RoundRect(dc, r.left, r.top, r.right, r.bottom, rad * 2, rad * 2);
        SelectObject(dc, op);
        SelectObject(dc, ob);
        DeleteObject(p);
        if (b != nullptr) DeleteObject(b);
    };
    shape(plate, radius, &kEspresso950, mix(kEspresso950, kCream, 0.14 + 0.12 * lift), 1);
    if (accent > 0.002) {
        RECT rr = tile;
        InflateRect(&rr, px(2, dpi), px(2, dpi));
        shape(rr, tileRadius + px(2, dpi), nullptr, mix(kEspresso950, kCaramel, 0.55 * accent), std::max(1, px(1.5, dpi)));
    }
    shape(tile, tileRadius, &kQrLight, kQrLight, 1);
    if (focusRing) {
        RECT fr = plate;
        InflateRect(&fr, -px(3, dpi), -px(3, dpi));
        shape(fr, std::max(1, radius - px(3, dpi)), nullptr, mix(kEspresso950, kCream, 0.72), std::max(1, px(1, dpi)));
    }
}

// ===========================================================================
// Paint. Renders the whole control into st's back buffer at time `now`, then copies it
// to `target` (nullptr = leave it in the buffer). False = no back buffer could be built;
// the state is left clean and the next paint retries.
// ===========================================================================
bool render(State& st, HDC target, int dpi, int w, int h, double now) {
    st.ensureLayout(dpi, w);
    if (!st.ensureBuffer(w, h)) return false;
    const Layout& L = st.L;
    const Motion& m = st.m;
    auto P = [dpi](double v) { return px(v, dpi); };

    const double push = m.push.at(now), lift = m.lift.at(now), brk = m.bracket.at(now);
    const double face = m.face.at(now), glow = m.glow.at(now), accent = m.accent.at(now);
    const double chrOp = 1.0 - m.chr.at(now), thumbOp = m.thumb.at(now);
    const int dy = st.pressed ? P(1) : -static_cast<int>(std::lround(L.rise * lift));
    HDC dc = st.memDC;

    // ---- 1. host background and the two copy lines (GDI, ClearType) ------------------
    const COLORREF host = GetSysColor(COLOR_BTNFACE);
    {
        RECT all{0, 0, w, h};
        HBRUSH b = CreateSolidBrush(host);
        FillRect(dc, &all, b);
        DeleteObject(b);
    }
    const bool lightHost = luminance(host) > 0.4;
    const COLORREF ink1 = cr(lightHost ? kEspresso900 : kCream);
    const COLORREF inkNow = cr(lightHost ? kCaramelDeep : kGold);   // gold fails contrast on a light host
    const COLORREF ink2 = cr(lightHost ? kInkMuted : kCreamSoft);
    {
        int x = 0;
        drawText(dc, st.F.line1, x, L.line1Base, kLine1a, ink1, TA_BASELINE | TA_LEFT);
        x += textWidth(dc, st.F.line1, kLine1a);
        drawText(dc, st.F.now, x, L.line1Base, kLine1b, inkNow, TA_BASELINE | TA_LEFT);
        x += textWidth(dc, st.F.now, kLine1b);
        drawText(dc, st.F.line1, x, L.line1Base, kLine1c, ink1, TA_BASELINE | TA_LEFT);
        drawText(dc, st.F.line2, 0, L.line2Base, kLine2, ink2, TA_BASELINE | TA_LEFT);
    }
    GdiFlush();

    RECT plate = L.plate;
    OffsetRect(&plate, 0, dy);
    const gp::RectF plateF = rectF(plate);
    const float radius = static_cast<float>(L.radius);

    // ---- 2. shapes, photo, lockup (GDI+, into the same bits) -------------------------
    if (g_gdiplusOk) {   // ensureBuffer guarantees a non-null, Ok wrapper on this path
        gp::Graphics g(st.wrap.get());
        g.SetPageUnit(gp::UnitPixel);
        g.SetSmoothingMode(gp::SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(gp::PixelOffsetModeHalf);
        g.SetInterpolationMode(gp::InterpolationModeHighQualityBicubic);

        // lift shadow - on a light host it is what makes the plate read as raised
        if (lift > 0.01 && !st.pressed) {
            for (int i = 1; i <= 3; ++i) {
                gp::GraphicsPath sp;
                gp::RectF r = plateF;
                r.Inflate(static_cast<float>(i - 1), 0.0f);
                r.Offset(0.0f, pxf(1.0 + i, dpi));
                addRoundRect(sp, r, radius + i);
                gp::SolidBrush b(gc(kBlack, 0.075 * lift));
                g.FillPath(&b, &sp);
            }
        }

        gp::GraphicsPath platePath;
        addRoundRect(platePath, plateF, radius);
        {
            gp::SolidBrush b(gc(kEspresso950));
            g.FillPath(&b, &platePath);
        }

        // the photo, clipped by the plate's rounded corners and the photo box
        RECT ph = L.photo;
        OffsetRect(&ph, 0, dy);
        const gp::RectF phF = rectF(ph);
        {
            gp::GraphicsPath inner;
            gp::RectF ir = plateF;
            ir.Inflate(-1.0f, -1.0f);
            addRoundRect(inner, ir, radius - 1.0f);
            gp::Region clip(&inner);
            clip.Intersect(phF);
            g.SetClip(&clip);
            if (gp::Bitmap* img = photo()) {
                const float iw = static_cast<float>(img->GetWidth()), ih = static_cast<float>(img->GetHeight());
                const float cover = std::max(phF.Width / iw, phF.Height / ih);
                const float sc = cover * static_cast<float>(1.0 + kPushScale * push);
                // Centre the cup at rest, never leave a gap, and push in about the cup.
                float ox = phF.X + phF.Width * 0.5f - iw * cover * kFocusX;
                float oy = phF.Y + phF.Height * 0.5f - ih * cover * kFocusY;
                ox = std::min(phF.X, std::max(phF.X + phF.Width - iw * cover, ox));
                oy = std::min(phF.Y, std::max(phF.Y + phF.Height - ih * cover, oy));
                const float fx = ox + iw * cover * kFocusX, fy = oy + ih * cover * kFocusY;
                const gp::RectF dst(fx - iw * sc * kFocusX, fy - ih * sc * kFocusY, iw * sc, ih * sc);
                gp::ImageAttributes ia;
                ia.SetWrapMode(gp::WrapModeTileFlipXY);   // no soft seam where the image edge meets the clip
                g.DrawImage(img, dst, 0.0f, 0.0f, iw, ih, gp::UnitPixel, &ia);
            } else {
                gp::SolidBrush b(gc(kEspresso800));
                g.FillRectangle(&b, phF);
            }
            // the photo's right edge dissolves into the plate
            const float fadeW = pxf(16, dpi);
            gp::RectF fade(phF.X + phF.Width - fadeW, phF.Y, fadeW, phF.Height);
            gp::LinearGradientBrush lg(gp::PointF(fade.X - 0.5f, 0.0f), gp::PointF(fade.X + fade.Width + 0.5f, 0.0f),
                                       gc(kEspresso950, 0.0), gc(kEspresso950, 1.0));
            g.FillRectangle(&lg, fade);

            // viewfinder brackets: whole pixels, so they stay crisp while they tighten
            g.SetSmoothingMode(gp::SmoothingModeNone);
            const int in = static_cast<int>(std::lround(pxf(kBracketRest + (kBracketLock - kBracketRest) * brk, dpi)));
            const int arm = P(kBracketArm), bw = std::max(1, P(kBracketW));
            gp::SolidBrush bb(gc(kCream, 0.6));
            const int l = ph.left + in, t = ph.top + in, r = ph.right - in, b = ph.bottom - in;
            const gp::Rect bars[8] = {
                {l, t, arm, bw}, {l, t, bw, arm},
                {r - arm, t, arm, bw}, {r - bw, t, bw, arm},
                {l, b - bw, arm, bw}, {l, b - arm, bw, arm},
                {r - arm, b - bw, arm, bw}, {r - bw, b - arm, bw, arm},
            };
            for (const gp::Rect& br : bars) g.FillRectangle(&bb, br);
            g.SetSmoothingMode(gp::SmoothingModeAntiAlias);

            if (L.osdOn) {
                gp::RectF pill = L.osdPill;
                pill.Offset(0.0f, static_cast<float>(dy));
                gp::GraphicsPath pp;
                addRoundRect(pp, pill, pxf(4, dpi));
                gp::SolidBrush pb(gc(kEspresso950, 0.62));
                g.FillPath(&pb, &pp);
                gp::RectF dot = L.osdDot;
                dot.Offset(0.0f, static_cast<float>(dy));
                gp::SolidBrush db(gc(kGold));
                g.FillEllipse(&db, dot);
            }
            g.ResetClip();
        }

        // plate border - brightens a touch with the lift
        {
            gp::GraphicsPath bp;
            gp::RectF br = plateF;
            br.Inflate(-0.5f, -0.5f);
            addRoundRect(bp, br, radius - 0.5f);
            gp::Pen pen(gc(kCream, 0.14 + 0.12 * lift), 1.0f);
            g.DrawPath(&pen, &bp);
        }

        // ---- the lockup (only if every path was allocated) ----
        const Art& A = st.A;
        if (A.ok) {
            g.TranslateTransform(L.markX, L.markY + dy);
            {
                gp::SolidBrush b(gc(kCream));
                g.FillPath(&b, A.head.get());
            }
            if (glow > 0.002) {   // .gl-seal-glow: a soft caramel bloom behind the stamp
                const float pad = std::max(2.0f, A.sealRc.Width * 0.6f);
                gp::RectF gr = A.sealRc;
                gr.Inflate(pad, pad);
                gp::GraphicsPath gpth;
                addRoundRect(gpth, gr, static_cast<float>(art::kSealRx) * A.s + pad);
                gp::PathGradientBrush pb(&gpth);
                pb.SetCenterPoint(gp::PointF(gr.X + gr.Width / 2, gr.Y + gr.Height / 2));
                pb.SetCenterColor(gc(kCaramel, kGlowPeak * glow));
                gp::Color edge = gc(kCaramel, 0.0);
                INT n = 1;
                pb.SetSurroundColors(&edge, &n);
                pb.SetFocusScales(A.sealRc.Width / gr.Width * 0.5f, A.sealRc.Height / gr.Height * 0.5f);
                g.FillPath(&pb, &gpth);
            }
            {
                gp::SolidBrush b(gc(mix(kCaramelDeep, kCaramel, face)));
                g.FillPath(&b, A.seal.get());
            }
            if (chrOp > 0.002) {
                gp::SolidBrush b(gc(kGlyph, chrOp));
                g.FillPath(&b, A.chr.get());
            }
            if (thumbOp > 0.002) {
                gp::SolidBrush b(gc(kGlyph, thumbOp));
                g.FillPath(&b, A.thumb.get());
            }
            if (m.sparkOn(now)) drawSpark(g, A, now - m.sparkT0);
            g.ResetTransform();
        }

        // QR tile (cream) and its faint caramel ring on hover
        RECT tile = L.tile;
        OffsetRect(&tile, 0, dy);
        const gp::RectF tileF = rectF(tile);
        if (accent > 0.002) {
            gp::GraphicsPath rp;
            gp::RectF rr = tileF;
            rr.Inflate(pxf(1.5, dpi), pxf(1.5, dpi));
            addRoundRect(rp, rr, pxf(7.5, dpi));
            gp::Pen pen(gc(kCaramel, 0.55 * accent), pxf(1.5, dpi));
            g.DrawPath(&pen, &rp);
        }
        {
            gp::GraphicsPath tp;
            addRoundRect(tp, tileF, pxf(6, dpi));
            gp::SolidBrush b(gc(kQrLight));
            g.FillPath(&b, &tp);
        }

        // keyboard focus ring, inside the plate
        if (st.focusVisible) {
            gp::GraphicsPath fp;
            gp::RectF fr = plateF;
            fr.Inflate(-pxf(3, dpi) + 0.5f, -pxf(3, dpi) + 0.5f);
            addRoundRect(fp, fr, radius - pxf(3, dpi));
            gp::Pen pen(gc(kCream, 0.72), std::max(1.0f, pxf(1, dpi)));
            g.DrawPath(&pen, &fp);
        }
    } else {   // no GDI+: a plain plate; the text and QR modules below are GDI either way
        RECT tile = L.tile;
        OffsetRect(&tile, 0, dy);
        plainPlate(dc, plate, L.radius, tile, P(6), lift, accent, st.focusVisible, dpi);
    }   // (GDI+ path) the Graphics flushes into the bits here

    // ---- 3. QR modules and the plate's text (GDI) ------------------------------------
    {
        HBRUSH dark = CreateSolidBrush(cr(kQrDark));
        const int mod = L.module, qx = L.qrOrg.x, qy = L.qrOrg.y + dy;
        for (int y = 0; y < kQrN; ++y) {
            for (int x = 0; x < kQrN;) {
                if (kQr[y][x] != '1') { ++x; continue; }
                int run = x;
                while (run < kQrN && kQr[y][run] == '1') ++run;   // one rect per horizontal run
                RECT r{qx + x * mod, qy + y * mod, qx + run * mod, qy + (y + 1) * mod};
                FillRect(dc, &r, dark);
                x = run;
            }
        }
        DeleteObject(dark);
    }
    if (L.osdOn && g_gdiplusOk)   // the readout belongs on the photo, which needs GDI+
        drawText(dc, st.F.osd, L.osdText.x, L.osdText.y + dy, kOsd, cr(kCream), TA_TOP | TA_LEFT);
    drawText(dc, st.F.name, L.nameBase.x, L.nameBase.y + dy, kName, cr(kCream), TA_BASELINE | TA_LEFT);
    drawText(dc, st.F.tag, L.tagPos.x, L.tagPos.y + dy, kTag, cr(mix(kCreamSoft, kCream, accent)),
             TA_TOP | TA_LEFT, L.tagTrack);
    drawText(dc, st.F.cap1, L.cap1.x, L.cap1.y + dy, kCap1, cr(kCreamSoft), TA_TOP | TA_CENTER);
    drawText(dc, st.F.cap2, L.cap2.x, L.cap2.y + dy, kCap2, cr(kCaptionSub), TA_TOP | TA_CENTER);
    GdiFlush();

    if (target != nullptr) BitBlt(target, 0, 0, w, h, dc, 0, 0, SRCCOPY);
    return true;
}

// ===========================================================================
// Window plumbing.
// ===========================================================================
constexpr UINT_PTR kTimerId = 1;
constexpr UINT     kFrameMs = 15;

State* stateOf(HWND h) { return reinterpret_cast<State*>(GetWindowLongPtrW(h, GWLP_USERDATA)); }

int dpiOf(HWND h) {
    if (HMODULE u = GetModuleHandleW(L"user32.dll")) {
        using Fn = UINT(WINAPI*)(HWND);
        if (auto f = reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(u, "GetDpiForWindow")))) {
            const int d = static_cast<int>(f(h));
            if (d > 0) return d;
        }
    }
    HDC dc = GetDC(h);
    const int d = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc) ReleaseDC(h, dc);
    return d > 0 ? d : 96;
}

void clientSize(HWND h, int& w, int& ht) {
    RECT rc{};
    GetClientRect(h, &rc);
    w = rc.right - rc.left;
    ht = rc.bottom - rc.top;
}

void ensureLayoutFor(State* s) {
    int w = 0, h = 0;
    clientSize(s->hwnd, w, h);
    s->ensureLayout(dpiOf(s->hwnd), w);
}

// Start the timer if anything is moving; KILL it the moment nothing is.
void syncTimer(State* s) {
    const bool busy = s->m.busy(nowMs());
    if (busy && !s->timer) {
        s->timer = SetTimer(s->hwnd, kTimerId, kFrameMs, nullptr) != 0;
    } else if (!busy && s->timer) {
        KillTimer(s->hwnd, kTimerId);
        s->timer = false;
    }
}

bool animationsOn() {
    BOOL v = TRUE;
    if (!SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &v, 0)) v = TRUE;
    return v != FALSE;
}

// Hover, keyboard focus (when focus cues are showing) and press all play the hover state;
// hovering the copy lines does not.
void refreshActive(State* s) {
    s->focusVisible = s->focused &&
                      (SendMessageW(s->hwnd, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS) == 0;
    s->m.setActive(s->hot || s->focusVisible || s->pressed, nowMs(), animationsOn());
    InvalidateRect(s->hwnd, nullptr, FALSE);
    syncTimer(s);
}

void updateTipRect(State* s) {
    if (s->tip == nullptr) return;
    TTTOOLINFOW ti{};
    ti.cbSize = sizeof(ti);
    ti.hwnd = s->hwnd;
    ti.uId = 1;
    ti.rect = s->L.hit;
    SendMessageW(s->tip, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&ti));
}

void openSite() { ShellExecuteW(nullptr, L"open", kUrl, nullptr, nullptr, SW_SHOWNORMAL); }

bool overPlate(State* s, LPARAM lp) {
    const POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
    return PtInRect(&s->L.hit, p) != FALSE;
}

LRESULT CALLBACK goatCamProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    State* s = stateOf(hwnd);

    switch (msg) {
        case WM_CREATE: {
            s = new State();
            s->hwnd = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
            // ponytail: the accessible name is the window text (MSAA's default), and there is
            // no "link" role - a WM_GETOBJECT provider would add one if a screen reader needs it.
            SetWindowTextW(hwnd, kLabel);
            s->tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                                     WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
                                     CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                     hwnd, nullptr, g_inst, nullptr);
            if (s->tip != nullptr) {
                TTTOOLINFOW ti{};
                ti.cbSize = sizeof(ti);
                ti.uFlags = TTF_SUBCLASS;
                ti.hwnd = hwnd;
                ti.uId = 1;
                ti.lpszText = const_cast<LPWSTR>(kLabel);
                SendMessageW(s->tip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
            }
            return 0;
        }

        case WM_DESTROY:
            if (s != nullptr) {
                if (s->timer) KillTimer(hwnd, kTimerId);
                if (s->tip != nullptr) DestroyWindow(s->tip);
                delete s;
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            }
            return 0;

        case WM_SIZE:
            if (s != nullptr) {
                ensureLayoutFor(s);
                updateTipRect(s);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;

        case WM_DPICHANGED_AFTERPARENT:
            if (s != nullptr) {
                ensureLayoutFor(s);
                updateTipRect(s);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;

        case WM_TIMER:
            if (s != nullptr && wp == kTimerId) {
                InvalidateRect(hwnd, nullptr, FALSE);   // this frame paints even if it is the last
                syncTimer(s);
            }
            return 0;

        case WM_SETCURSOR:
            if (s != nullptr && LOWORD(lp) == HTCLIENT) {
                ensureLayoutFor(s);
                POINT p;
                GetCursorPos(&p);
                ScreenToClient(hwnd, &p);
                SetCursor(LoadCursorW(nullptr, PtInRect(&s->L.hit, p) ? IDC_HAND : IDC_ARROW));
                return TRUE;
            }
            break;

        case WM_MOUSEMOVE:
            if (s != nullptr) {
                if (!s->tracking) {
                    TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
                    s->tracking = TrackMouseEvent(&tme) != FALSE;
                }
                ensureLayoutFor(s);
                const bool hot = overPlate(s, lp);
                if (hot != s->hot) {
                    s->hot = hot;
                    refreshActive(s);
                }
            }
            return 0;

        case WM_MOUSELEAVE:
            if (s != nullptr) {
                s->tracking = false;
                if (s->hot) {
                    s->hot = false;
                    refreshActive(s);
                }
            }
            return 0;

        case WM_LBUTTONDOWN:
            if (s != nullptr && overPlate(s, lp)) {
                s->pressed = true;
                SetCapture(hwnd);
                refreshActive(s);
            }
            return 0;

        case WM_LBUTTONUP:
            if (s != nullptr && s->pressed) {
                s->pressed = false;
                ReleaseCapture();
                const bool inside = overPlate(s, lp);
                s->hot = inside;
                refreshActive(s);
                if (inside) openSite();
            }
            return 0;

        case WM_CAPTURECHANGED:
            if (s != nullptr && s->pressed && reinterpret_cast<HWND>(lp) != hwnd) {
                s->pressed = false;
                refreshActive(s);
            }
            return 0;

        case WM_GETDLGCODE: {
            // Enter and Space must reach us rather than the host's default button; chars are
            // wanted so a Space press does not beep through the dialog manager.
            LRESULT code = DLGC_WANTCHARS;
            const MSG* m = reinterpret_cast<const MSG*>(lp);
            if (m != nullptr && m->message == WM_KEYDOWN && (m->wParam == VK_RETURN || m->wParam == VK_SPACE))
                code |= DLGC_WANTMESSAGE;
            return code;
        }

        case WM_KEYDOWN:
            if (wp == VK_RETURN || wp == VK_SPACE) {
                if ((lp & (1 << 30)) == 0) openSite();   // not on auto-repeat
                return 0;
            }
            break;

        case WM_CHAR:
            if (wp == L' ' || wp == L'\r') return 0;
            break;

        case WM_SETFOCUS:
        case WM_KILLFOCUS:
            if (s != nullptr) {
                s->focused = (msg == WM_SETFOCUS);
                refreshActive(s);
            }
            return 0;

        case WM_UPDATEUISTATE: {
            const LRESULT r = DefWindowProcW(hwnd, msg, wp, lp);
            if (s != nullptr) refreshActive(s);
            return r;
        }

        case WM_ERASEBKGND:
            return 1;   // the paint covers every pixel from its own back buffer

        case WM_PRINTCLIENT:
            if (s != nullptr) {
                int w = 0, h = 0;
                clientSize(hwnd, w, h);
                if (!render(*s, reinterpret_cast<HDC>(wp), dpiOf(hwnd), w, h, nowMs())) {
                    RECT rc{0, 0, w, h};
                    FillRect(reinterpret_cast<HDC>(wp), &rc, GetSysColorBrush(COLOR_BTNFACE));
                }
            }
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            if (hdc != nullptr && s != nullptr) {
                int w = 0, h = 0;
                clientSize(hwnd, w, h);
                // No back buffer (out of memory): show the host background rather than stale
                // pixels. The state is clean, so the next paint - any invalidation - retries.
                if (!render(*s, hdc, dpiOf(hwnd), w, h, nowMs()))
                    FillRect(hdc, &ps.rcPaint, GetSysColorBrush(COLOR_BTNFACE));
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

void registerGoatCamClass(HINSTANCE hInst) {
    g_inst = hInst;
    if (!g_gdiplusOk) {
        // ponytail: GDI+ is started once and never shut down - the process exit reclaims it,
        // and shutting down early would invalidate the cached photo. Add GdiplusShutdown
        // after the message loop if this control ever lives in a DLL.
        // If it fails, the banner paints its plain GDI plate instead (see render()).
        gp::GdiplusStartupInput in;
        const gp::Status st = GOATCAM_STARTUP_FAULT() ? gp::GenericError
                                                      : gp::GdiplusStartup(&g_gdiplusToken, &in, nullptr);
        g_gdiplusOk = (st == gp::Ok);
        if (!g_gdiplusOk) g_gdiplusToken = 0;
    }
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = goatCamProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kGoatCamClass;
    RegisterClassExW(&wc);
}

SIZE goatCamPreferredSize(int dpi) {
    return SIZE{px(kWidth, dpi), totalHeight(dpi)};
}

} // namespace ui
