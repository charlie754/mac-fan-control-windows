// goatcambanner.cpp - GOAT Cam promotional banner. Implementation of goatcambanner.h.
//
// WHAT THIS IS
//   The GOAT Cam banner, v2 (brand\goatcam-banner\SPEC.md section 7 in the GOAT Cam repo):
//   one headline line, then a plate that is entirely the site's own hero photo (the cup on
//   the left), with "GOAT Cam" and "IPHONE APP · ON THE APP STORE" over the laptop's dark
//   screen, viewfinder brackets, a "1080p · 60 fps" OSD and the App Store QR on the right.
//   Hovering - or keyboard-focusing - the QR enlarges it to >= 4 px per module so a phone can
//   scan it. The plate is the only link (goatcam.dagoat.io).
//   - The photo: resources\goatcam-coffee.jpg = the site's hero-coffee-800.jpg, unmodified
//     (CC0, Alex Knight), embedded as RCDATA. 800 px wide covers the 600 px plate at 144 DPI.
//   - Durations and curves: the site's --ease-out and the lockup stylesheet's --gl-out.
//
// DRAWING
//   Each control owns one 32bpp DIB as its back buffer, so nothing flickers. GDI+ draws the
//   shapes, the photo and everything with alpha straight into the DIB's bits; GDI then draws
//   the text (ClearType and letter-spacing, which GDI+ lacks) and the QR modules, which must
//   land on exact integer pixels to stay scannable - at rest, enlarged and in between.
//
// HOVER LIFT
//   The web plate scales to 1.05. Here it rises S(2) px and casts a soft shadow instead:
//   scaling would resample the QR off its integer module grid and make the text shimmer on
//   every frame. Both are kept honest by moving only in whole pixels.
//
// THE QR ENLARGEMENT
//   It grows inside this control - the control is made tall enough for a 37 x 4 px code - and
//   is drawn last, above everything, so it is never clipped and needs no popup window.
//
// THE CPU RULE, WHICH OUTRANKS EVERYTHING ABOVE
//   The 15 ms animation timer exists only while a transition is still running.
//   Motion::busy() is the single gate and syncTimer() kills the timer the moment it goes
//   false, so the banner at rest - hovered or not - costs nothing. The looping parts of the
//   web banner (steam, the pulsing OSD dot) are deliberately NOT ported: they would hold the
//   timer open for as long as the pointer stays, in an app that exists to keep a fan quiet.

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
// paints a plain GDI plate (no photo) and no GDI+ object is ever constructed.
bool g_gdiplusOk = false;

// Seams for the offscreen test harness, which #includes this file and defines them first.
// In the product all three are constants and compile away.
#ifndef GOATCAM_STARTUP_FAULT
#define GOATCAM_STARTUP_FAULT() 0   // nonzero: treat GdiplusStartup as failed
#endif
#ifndef GOATCAM_WRAP_FAULT
#define GOATCAM_WRAP_FAULT() 0      // 1: the back-buffer wrapper allocation returns null; 2: its status is not Ok
#endif
#ifndef GOATCAM_PLATE_TEXT
#define GOATCAM_PLATE_TEXT() 1      // 0: skip the wordmark and tagline (to measure the pixels under them)
#endif

// ===========================================================================
// Content. GOAT Cam v1 is camera-only: never a microphone, never a price.
// ===========================================================================
const wchar_t* const kUrl   = L"https://goatcam.dagoat.io";
const wchar_t* const kLabel = L"GOAT Cam — open goatcam.dagoat.io";   // tooltip + accessible name

const wchar_t* const kLine1a = L"GOAT Cam — your iPhone, ";
const wchar_t* const kLine1b = L"NOW";
const wchar_t* const kLine1c = L" your PC’s webcam.";
const wchar_t* const kName   = L"GOAT Cam";
const wchar_t* const kTag    = L"IPHONE APP · ON THE APP STORE";
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
constexpr Rgb kQrLight     {0xfb, 0xf5, 0xea};
constexpr Rgb kQrDark      {0x1c, 0x13, 0x0c};
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
// ===========================================================================
constexpr int kWidth       = 400;   // the sensor list's width, which the banner sits under
constexpr int kLine1H      = 18;    // the headline
constexpr int kCopyGap     = 6;
constexpr int kRise        = 2;     // hover lift
constexpr int kShadow      = 5;     // room under the plate for the lift's shadow
constexpr int kPlateH      = 124;   // tall enough that the enlarged QR fits inside the control
constexpr int kPlateRadius = 10;
constexpr int kTile        = 74;    // QR at rest: 37 modules x 2 px at 96 DPI (the v1 floor)
constexpr int kTileBig     = 148;   // QR enlarged: 37 modules x 4 px at 96 DPI
constexpr int kTileGap     = 4;     // QR tile -> the locked right-hand bracket
constexpr int kBracketArm  = 11;
constexpr int kBracketW    = 2;
constexpr double kBracketRest = 6.0;   // inset from the plate edge
constexpr double kBracketLock = 9.0;   // focus lock: tightens INWARD by 3 px on hover, as the master CSS
constexpr int kTagGap      = 7;        // wordmark baseline -> tagline top

int totalHeight(int dpi) {
    return px(kLine1H, dpi) + px(kCopyGap, dpi) + px(kRise, dpi) + px(kPlateH, dpi) + px(kShadow, dpi);
}

// ===========================================================================
// The photo's own geometry, as fractions of hero-coffee-800.jpg (800 x 498), so a re-encode
// at another size keeps every anchor. [M] read off the 800 px file.
// ===========================================================================
constexpr double kImgW = 800.0, kImgH = 498.0;   // nominal size, for layout without the photo
constexpr double kPhotoTop = 160.0 / 498.0;      // source row at the plate's top: cup + saucer fully in
constexpr double kCupX = 105.0 / 800.0, kCupY = 335.0 / 498.0;          // push-in focus
constexpr double kScreenL = 262.0 / 800.0, kScreenR = 572.0 / 800.0;     // the laptop's dark screen
constexpr double kScreenTop = 125.0 / 498.0, kScreenBottom = 328.0 / 498.0;

// ===========================================================================
// Motion constants. Leaving is always faster than arriving.
// ===========================================================================
constexpr double kOutMs        = 360.0;   // --gl-t-out
constexpr double kPushInMs     = 1200.0;  // photo push-in
constexpr double kPushScale    = 0.06;    // scale(1) -> scale(1.06)
constexpr double kLiftInMs     = 420.0;
constexpr double kBracketInMs  = 300.0;   // "focus lock"
constexpr double kBracketOutMs = 240.0;
constexpr double kAccentInMs   = 630.0;   // the QR tile's caramel ring
constexpr double kQrInMs       = 200.0;   // QR enlarge
constexpr double kQrOutMs      = 120.0;

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
double easeOut(double t) { return cubicBezier(t, 0.16, 1.0, 0.3, 1.0); }   // --ease-out
double glOut(double t)   { return cubicBezier(t, 0.4, 0.0, 0.2, 1.0); }    // --gl-out
using Ease = double (*)(double);

// One CSS-style transition of a 0..1 value. Retargeting starts from wherever the value is
// NOW, and a part-finished transition reverses in the matching part of its duration (CSS
// "reversing shortening"), so a quick in-and-out never jumps.
struct Tween {
    double from = 0.0, to = 0.0, t0 = 0.0, dur = 0.0;
    Ease ease = glOut;

    double at(double now) const {
        if (dur <= 0.0) return to;
        const double t = (now - t0) / dur;
        if (t <= 0.0) return from;
        if (t >= 1.0) return to;
        return from + (to - from) * ease(t);
    }
    bool busy(double now) const { return dur > 0.0 && now < t0 + dur; }
    void go(double now, double target, double fullMs, Ease e) {
        const double cur = at(now);
        from = cur; to = target; t0 = now; ease = e;
        dur = fullMs * std::fabs(target - cur);
        if (dur < 1.0) { from = to; dur = 0.0; }
    }
};

struct Motion {
    Tween push, lift, bracket;   // travelling parts: off under reduced motion
    Tween accent;                // the QR tile's caramel ring: a fade, always
    Tween qr;                    // QR enlargement: functional, so instant under reduced motion
    bool active = false, enlarged = false;

    // `travel` is SPI_GETCLIENTAREAANIMATION. Off = fades only, enlargement instant.
    void setActive(bool on, double now, bool travel) {
        if (on == active) return;
        active = on;
        if (on) {
            if (travel) {
                push.go(now, 1, kPushInMs, easeOut);
                lift.go(now, 1, kLiftInMs, easeOut);
                bracket.go(now, 1, kBracketInMs, easeOut);
            }
            accent.go(now, 1, kAccentInMs, glOut);
        } else {
            push.go(now, 0, kOutMs, glOut);
            lift.go(now, 0, kOutMs, glOut);
            bracket.go(now, 0, kBracketOutMs, glOut);
            accent.go(now, 0, kOutMs, glOut);
        }
    }
    void setEnlarged(bool on, double now, bool travel) {
        if (on == enlarged) return;
        enlarged = on;
        qr.go(now, on ? 1.0 : 0.0, travel ? (on ? kQrInMs : kQrOutMs) : 0.0, on ? easeOut : glOut);
    }

    // THE single gate on the animation timer.
    bool busy(double now) const {
        return push.busy(now) || lift.busy(now) || bracket.busy(now) || accent.busy(now) || qr.busy(now);
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
    HFONT line1 = nullptr, now = nullptr, osd = nullptr, name = nullptr, tag = nullptr;

    Fonts() = default;
    Fonts(const Fonts&) = delete;
    Fonts& operator=(const Fonts&) = delete;
    ~Fonts() { destroy(); }

    void destroy() {
        for (HFONT* f : {&line1, &now, &osd, &name, &tag}) {
            if (*f != nullptr) DeleteObject(*f);
            *f = nullptr;
        }
        dpi = 0;
    }
    void build(int d) {
        destroy();
        dpi   = d;
        line1 = makeFont({L"Segoe UI"}, px(12.5, d), FW_SEMIBOLD, false);
        now   = makeFont({GC_SERIF}, px(13.5, d), FW_BOLD, true);      // the site h1's italic NOW
        osd   = makeFont({GC_MONO}, px(8, d), FW_NORMAL, false);
        name  = makeFont({GC_SERIF}, px(24, d), FW_NORMAL, false);     // --serif, not uppercase
        tag   = makeFont({L"Segoe UI"}, px(8, d), FW_SEMIBOLD, false); // tracked micro-caps, sans
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

// Where the photo lands on a plate: cover, the plate's top at kPhotoTop (so the cup and the
// saucer are fully in), never a gap. iw/ih = the image's own size.
struct Cover { double ox, oy, sc; };
Cover coverOf(const RECT& plate, double iw, double ih) {
    const double pw = plate.right - plate.left, ph = plate.bottom - plate.top;
    const double sc = std::max(pw / iw, ph / ih);
    double ox = plate.left;   // the cup is on the LEFT: align left
    double oy = plate.top - kPhotoTop * ih * sc;
    oy = std::min<double>(plate.top, std::max<double>(plate.bottom - ih * sc, oy));
    ox = std::min<double>(plate.left, std::max<double>(plate.right - iw * sc, ox));
    return Cover{ox, oy, sc};
}

// ===========================================================================
// Layout for one (dpi, width). Every rect is at REST; the paint offsets the plate by the
// lift. Rects are in control client coordinates.
// ===========================================================================
struct Layout {
    int dpi = 0, w = 0, h = 0;
    int line1Base = 0;
    RECT plate{}, hit{};
    int rise = 0, radius = 0;
    bool osdOn = false;
    gp::RectF osdPill, osdDot;
    POINT osdText{};
    POINT nameBase{}, tagPos{};
    int tagTrack = 0;
    gp::RectF scrim;       // the local espresso gradient behind the two plate lines
    RECT tile{};           // QR at rest (plate-relative motion applies)
    int module = 0;
    RECT bigTile{};        // QR enlarged - fixed in the control, never moved by the lift
    int bigModule = 0;

    void build(HDC dc, const Fonts& F, int d, int width) {
        dpi = d;
        w = width;
        h = totalHeight(d);
        auto P = [d](double v) { return px(v, d); };

        const TEXTMETRICW tm1 = metricsOf(dc, F.line1);
        line1Base = (P(kLine1H) - tm1.tmHeight) / 2 + tm1.tmAscent;

        rise = P(kRise);
        radius = P(kPlateRadius);
        const int top = P(kLine1H) + P(kCopyGap) + rise;
        plate = RECT{0, top, w, top + P(kPlateH)};
        hit = plate;
        hit.top -= rise;

        // brackets: the locked inset and bar, with the paint's own rounding
        const int lockIn = static_cast<int>(std::lround(pxf(kBracketLock, d)));
        const int bar = std::max(1, P(kBracketW));

        // OSD readout, top-left just inside the LOCKED bracket - only when it fits.
        const TEXTMETRICW tmo = metricsOf(dc, F.osd);
        const float pillH = static_cast<float>(tmo.tmHeight + P(4));
        const float dot = pxf(5, d);
        const float textW = static_cast<float>(textWidth(dc, F.osd, kOsd));
        const float pillW = pxf(4, d) + dot + pxf(3, d) + textW + pxf(5, d);
        osdPill = gp::RectF(static_cast<float>(plate.left + P(13)), static_cast<float>(plate.top + P(12)), pillW, pillH);
        osdDot  = gp::RectF(osdPill.X + pxf(4, d), osdPill.Y + (pillH - dot) / 2.0f, dot, dot);
        osdText = POINT{static_cast<LONG>(std::lround(osdDot.X + dot + pxf(3, d))),
                        static_cast<LONG>(std::lround(osdPill.Y + (pillH - tmo.tmHeight) / 2.0f))};

        // QR at rest: right-hand side, inside the locked brackets, an INTEGER module size.
        const int side = P(kTile);
        module = std::max(2, side / kQrN);
        const int qr = module * kQrN;
        const int tside = std::max(side, qr);
        const int tRight = w - lockIn - bar - P(kTileGap);
        const int tTop = plate.top + (P(kPlateH) - tside) / 2;
        tile = RECT{tRight - tside, tTop, tRight, tTop + tside};
        osdOn = osdPill.X + osdPill.Width <= tile.left - P(8);

        // QR enlarged: >= 4 px per module, right edge on the resting tile's, vertically as
        // close to the resting tile as the control allows - fully inside it, shadow included.
        bigModule = std::max(4, P(kTileBig) / kQrN);
        const int big = bigModule * kQrN;
        const int shadowPad = P(4);
        int bTop = (tile.top + tile.bottom) / 2 - big / 2;
        bTop = std::max(0, std::min(bTop, h - big - shadowPad));
        bigTile = RECT{tRight - big, bTop, tRight, bTop + big};

        // The two plate lines sit on the laptop's dark screen: centred on the part of it that
        // stays under them through the whole push-in (the photo scales about the cup, so the
        // screen slides right), and in its visible height vertically. Computed from the photo's
        // nominal geometry, so the layout is the same whether or not the photo loaded.
        const Cover cv = coverOf(plate, kImgW, kImgH);
        const double fx = cv.ox + kCupX * kImgW * cv.sc, k = 1.0 + kPushScale;
        const double sl = cv.ox + kScreenL * kImgW * cv.sc, sr = cv.ox + kScreenR * kImgW * cv.sc;
        const double cx = (std::max(sl, fx + (sl - fx) * k) + std::min(sr, fx + (sr - fx) * k)) / 2.0;
        const double sTop = std::max<double>(plate.top, cv.oy + kScreenTop * kImgH * cv.sc);
        const double sBot = std::min<double>(plate.bottom, cv.oy + kScreenBottom * kImgH * cv.sc);
        tagTrack = std::max(1, P(1));   // 1/1/2 px at 96/120/144: scales with the type
        const int capName = capHeightOf(dc, F.name);
        const TEXTMETRICW tmt = metricsOf(dc, F.tag);
        const int capTag = capHeightOf(dc, F.tag);
        const int nameW = textWidth(dc, F.name, kName);
        const int tagW = textWidth(dc, F.tag, kTag, tagTrack) - tagTrack;   // no trailing tracking
        const double block = capName + P(kTagGap) + capTag;
        const double bTopY = (sTop + sBot) / 2.0 - block / 2.0;
        nameBase = POINT{static_cast<LONG>(std::lround(cx - nameW / 2.0)), static_cast<LONG>(std::lround(bTopY + capName))};
        const int tagCapTop = static_cast<int>(std::lround(bTopY + capName + P(kTagGap)));
        tagPos = POINT{static_cast<LONG>(std::lround(cx - tagW / 2.0)),
                       tagCapTop - (tmt.tmAscent - capTag)};   // TA_TOP origin from the cap top
        const double bw = std::max(nameW, tagW);
        // the scrim: an ellipse whose solid core covers the whole text block, fading outside it
        scrim = gp::RectF(static_cast<float>(cx - bw / 2.0 - P(34)), static_cast<float>(bTopY - P(14)),
                          static_cast<float>(bw + P(68)), static_cast<float>(block + P(28)));
    }
};

// ===========================================================================
// The photo: decoded once per process from the RCDATA resource into a PARGB bitmap, so
// nothing keeps the source stream alive.
// ===========================================================================
HINSTANCE g_inst = nullptr;
ULONG_PTR g_gdiplusToken = 0;
gp::Bitmap* g_photo = nullptr;   // ponytail: never freed - one ~1.6 MB decode for the process lifetime
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
                bool drawn = false;
                {
                    gp::Graphics g(copy.get());
                    drawn = g.GetLastStatus() == gp::Ok && g.DrawImage(&src, 0, 0, w, h) == gp::Ok;
                }   // the Graphics is released before the bitmap is published
                if (drawn) {   // a failed draw leaves g_photo null: the plate draws without a photo
                    g_photo = copy.release();
                    ok = true;
                }
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

// ===========================================================================
// Per-control state.
// ===========================================================================
struct State {
    HWND hwnd = nullptr, tip = nullptr;
    bool hot = false, qrHot = false, pressed = false, focused = false, focusVisible = false;
    bool tracking = false, timer = false;
    Motion m;
    Fonts F;
    Layout L;

    // back buffer: a top-down 32bpp DIB, also wrapped as a GDI+ bitmap over the same bits
    HDC memDC = nullptr;
    HBITMAP dib = nullptr;
    HGDIOBJ oldBmp = nullptr;
    void* bits = nullptr;
    int bw = 0, bh = 0;
    std::unique_ptr<gp::Bitmap> wrap;

    // The photo pre-scaled once to the full push-in, for the frames while it is MOVING: a
    // HighQualityBilinear draw from this costs a fraction of a bicubic one from the 800 px original.
    // Still frames (rest, settled hover) always draw from the original at full quality.
    std::unique_ptr<gp::Bitmap> zoom;
    double zoomScale = 0.0;

    gp::Bitmap* zoomFor(gp::Bitmap* img, double scale) {
        if (zoom && zoomScale == scale) return zoom.get();
        zoom.reset();
        zoomScale = 0.0;
        const double iw = img->GetWidth(), ih = img->GetHeight();
        const INT zw = static_cast<INT>(std::ceil(iw * scale)), zh = static_cast<INT>(std::ceil(ih * scale));
        std::unique_ptr<gp::Bitmap> z(new gp::Bitmap(zw, zh, PixelFormat32bppPARGB));
        if (!z || z->GetLastStatus() != gp::Ok) return nullptr;   // GdipAlloc can return null
        bool drawn = false;
        {
            gp::Graphics g(z.get());
            if (g.GetLastStatus() == gp::Ok) {
                g.SetInterpolationMode(gp::InterpolationModeHighQualityBicubic);
                g.SetPixelOffsetMode(gp::PixelOffsetModeHalf);
                gp::ImageAttributes ia;
                ia.SetWrapMode(gp::WrapModeTileFlipXY);
                drawn = g.DrawImage(img, gp::RectF(0.0f, 0.0f, static_cast<float>(iw * scale), static_cast<float>(ih * scale)),
                                    0.0f, 0.0f, static_cast<float>(iw), static_cast<float>(ih), gp::UnitPixel, &ia) == gp::Ok;
            }
        }
        if (!drawn) return nullptr;   // the caller falls back to the full-quality draw
        zoom = std::move(z);
        zoomScale = scale;
        return zoom.get();
    }

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

    // Fonts and layout depend on the DPI and the width only.
    void ensureLayout(int dpi, int w) {
        if (F.dpi != dpi) F.build(dpi);
        if (L.dpi != dpi || L.w != w) {
            HDC screen = GetDC(nullptr);
            L.build(screen, F, dpi, w);
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

// The QR modules: one rect per horizontal run of dark modules, exact integer pixels.
void drawQrModules(HDC dc, int qx, int qy, int mod) {
    HBRUSH dark = CreateSolidBrush(cr(kQrDark));
    for (int y = 0; y < kQrN; ++y) {
        for (int x = 0; x < kQrN;) {
            if (kQr[y][x] != '1') { ++x; continue; }
            int run = x;
            while (run < kQrN && kQr[y][run] == '1') ++run;
            RECT r{qx + x * mod, qy + y * mod, qx + run * mod, qy + (y + 1) * mod};
            FillRect(dc, &r, dark);
            x = run;
        }
    }
    DeleteObject(dark);
}

// A filled GDI round rect - the no-GDI+ fallback's only shape.
void gdiRoundRect(HDC dc, const RECT& r, int rad, const Rgb* fill, Rgb line, int width) {
    HBRUSH b = fill != nullptr ? CreateSolidBrush(cr(*fill)) : nullptr;
    HPEN p = CreatePen(PS_SOLID, width, cr(line));
    HGDIOBJ ob = SelectObject(dc, b != nullptr ? static_cast<HGDIOBJ>(b) : GetStockObject(NULL_BRUSH));
    HGDIOBJ op = SelectObject(dc, p);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, rad * 2, rad * 2);
    SelectObject(dc, op);
    SelectObject(dc, ob);
    DeleteObject(p);
    if (b != nullptr) DeleteObject(b);
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
    const double accent = m.accent.at(now), q = m.qr.at(now);
    const int dy = st.pressed ? P(1) : -static_cast<int>(std::lround(L.rise * lift));
    const bool big = q > 0.002;
    HDC dc = st.memDC;

    // ---- 1. host background and the headline (GDI, ClearType) ------------------------
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
    {
        int x = 0;
        drawText(dc, st.F.line1, x, L.line1Base, kLine1a, ink1, TA_BASELINE | TA_LEFT);
        x += textWidth(dc, st.F.line1, kLine1a);
        drawText(dc, st.F.now, x, L.line1Base, kLine1b, inkNow, TA_BASELINE | TA_LEFT);
        x += textWidth(dc, st.F.now, kLine1b);
        drawText(dc, st.F.line1, x, L.line1Base, kLine1c, ink1, TA_BASELINE | TA_LEFT);
    }
    GdiFlush();

    RECT plate = L.plate;
    OffsetRect(&plate, 0, dy);
    const gp::RectF plateF = rectF(plate);
    const float radius = static_cast<float>(L.radius);
    RECT tile = L.tile;
    OffsetRect(&tile, 0, dy);

    // ---- 2. the plate: photo, scrim, brackets, OSD, QR tile (GDI+) --------------------
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
        g.SetClip(&platePath);

        // the photo, covering the WHOLE plate, pushed in about the cup
        if (gp::Bitmap* img = photo()) {
            const double iw = img->GetWidth(), ih = img->GetHeight();
            const Cover cv = coverOf(plate, iw, ih);
            const double fx = cv.ox + kCupX * iw * cv.sc, fy = cv.oy + kCupY * ih * cv.sc;
            const double sc = cv.sc * (1.0 + kPushScale * push);
            const gp::RectF dst(static_cast<float>(fx - kCupX * iw * sc), static_cast<float>(fy - kCupY * ih * sc),
                                static_cast<float>(iw * sc), static_cast<float>(ih * sc));
            gp::ImageAttributes ia;
            ia.SetWrapMode(gp::WrapModeTileFlipXY);   // no soft seam where the image edge meets the clip
            const double zs = cv.sc * (1.0 + kPushScale);
            gp::Bitmap* z = m.push.busy(now) ? st.zoomFor(img, zs) : nullptr;
            if (z != nullptr) {   // moving: cheap
                g.SetInterpolationMode(gp::InterpolationModeHighQualityBilinear);   // [M] ~3x cheaper than plain Bilinear here
                g.DrawImage(z, dst, 0.0f, 0.0f, static_cast<float>(iw * zs), static_cast<float>(ih * zs), gp::UnitPixel, &ia);
                g.SetInterpolationMode(gp::InterpolationModeHighQualityBicubic);
            } else {              // still (or no cache): full quality
                g.DrawImage(img, dst, 0.0f, 0.0f, static_cast<float>(iw), static_cast<float>(ih), gp::UnitPixel, &ia);
            }
        }

        // a local espresso gradient under the two plate lines - a scrim, not a box
        {
            gp::RectF sr = L.scrim;
            sr.Offset(0.0f, static_cast<float>(dy));
            gp::GraphicsPath ep;
            ep.AddEllipse(sr);
            gp::PathGradientBrush pb(&ep);
            pb.SetCenterColor(gc(kEspresso950, 0.82));
            gp::Color edge = gc(kEspresso950, 0.0);
            INT n = 1;
            pb.SetSurroundColors(&edge, &n);
            pb.SetFocusScales(0.78f, 0.55f);
            g.FillPath(&pb, &ep);
        }

        // viewfinder brackets: whole pixels, so they stay crisp while they tighten
        {
            g.SetSmoothingMode(gp::SmoothingModeNone);
            const int in = static_cast<int>(std::lround(pxf(kBracketRest + (kBracketLock - kBracketRest) * brk, dpi)));
            const int arm = P(kBracketArm), bw = std::max(1, P(kBracketW));
            gp::SolidBrush bb(gc(kCream, 0.6));
            const int l = plate.left + in, t = plate.top + in, r = plate.right - in, b = plate.bottom - in;
            const gp::Rect bars[8] = {
                {l, t, arm, bw}, {l, t, bw, arm},
                {r - arm, t, arm, bw}, {r - bw, t, bw, arm},
                {l, b - bw, arm, bw}, {l, b - arm, bw, arm},
                {r - arm, b - bw, arm, bw}, {r - bw, b - arm, bw, arm},
            };
            for (const gp::Rect& br : bars) g.FillRectangle(&bb, br);
            g.SetSmoothingMode(gp::SmoothingModeAntiAlias);
        }

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

        // plate border - brightens a touch with the lift
        {
            gp::GraphicsPath bp;
            gp::RectF br = plateF;
            br.Inflate(-0.5f, -0.5f);
            addRoundRect(bp, br, radius - 0.5f);
            gp::Pen pen(gc(kCream, 0.14 + 0.12 * lift), 1.0f);
            g.DrawPath(&pen, &bp);
        }

        // QR tile at rest (cream) and its faint caramel ring on hover; the enlarged QR replaces it
        if (!big) {
            const gp::RectF tileF = rectF(tile);
            if (accent > 0.002) {
                gp::GraphicsPath rp;
                gp::RectF rr = tileF;
                rr.Inflate(pxf(1.5, dpi), pxf(1.5, dpi));
                addRoundRect(rp, rr, pxf(7.5, dpi));
                gp::Pen pen(gc(kCaramel, 0.55 * accent), pxf(1.5, dpi));
                g.DrawPath(&pen, &rp);
            }
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
    } else {   // no GDI+: a plain plate and tile; the text and QR modules below are GDI either way
        gdiRoundRect(dc, plate, L.radius, &kEspresso950, mix(kEspresso950, kCream, 0.14 + 0.12 * lift), 1);
        if (!big) {
            if (accent > 0.002) {
                RECT rr = tile;
                InflateRect(&rr, P(2), P(2));
                gdiRoundRect(dc, rr, P(8), nullptr, mix(kEspresso950, kCaramel, 0.55 * accent), std::max(1, P(1.5)));
            }
            gdiRoundRect(dc, tile, P(6), &kQrLight, kQrLight, 1);
        }
        if (st.focusVisible) {
            RECT fr = plate;
            InflateRect(&fr, -P(3), -P(3));
            gdiRoundRect(dc, fr, std::max(1, L.radius - P(3)), nullptr, mix(kEspresso950, kCream, 0.72), std::max(1, P(1)));
        }
    }   // (GDI+ path) the Graphics flushes into the bits here

    // ---- 3. the resting QR's modules and the plate's text (GDI) ----------------------
    if (!big) drawQrModules(dc, L.tile.left + (L.tile.right - L.tile.left - L.module * kQrN) / 2,
                            L.tile.top + dy + (L.tile.bottom - L.tile.top - L.module * kQrN) / 2, L.module);
    if (L.osdOn && g_gdiplusOk)   // the readout belongs on the photo, which needs GDI+
        drawText(dc, st.F.osd, L.osdText.x, L.osdText.y + dy, kOsd, cr(kCream), TA_TOP | TA_LEFT);
    if (GOATCAM_PLATE_TEXT()) {
        drawText(dc, st.F.name, L.nameBase.x, L.nameBase.y + dy, kName, cr(kCream), TA_BASELINE | TA_LEFT);
        drawText(dc, st.F.tag, L.tagPos.x, L.tagPos.y + dy, kTag, cr(kCream), TA_TOP | TA_LEFT, L.tagTrack);
    }
    GdiFlush();

    // ---- 4. the enlarged QR, above everything ------------------------------------------
    if (big) {
        // The tile grows from the resting rect to the big one; the code inside is always the
        // largest INTEGER module that fits, so every in-between frame stays scannable.
        auto lerp = [q](LONG a, LONG b) { return static_cast<LONG>(std::lround(a + (b - a) * q)); };
        const RECT r{lerp(tile.left, L.bigTile.left), lerp(tile.top, L.bigTile.top),
                     lerp(tile.right, L.bigTile.right), lerp(tile.bottom, L.bigTile.bottom)};
        const int side = std::min(r.right - r.left, r.bottom - r.top);
        const int mod = std::max(L.module, side / kQrN);
        if (g_gdiplusOk) {
            gp::Graphics g(st.wrap.get());
            g.SetPageUnit(gp::UnitPixel);
            g.SetSmoothingMode(gp::SmoothingModeAntiAlias);
            g.SetPixelOffsetMode(gp::PixelOffsetModeHalf);
            const gp::RectF rf = rectF(r);
            for (int i = 1; i <= 3; ++i) {   // soft shadow
                gp::GraphicsPath sp;
                gp::RectF s = rf;
                s.Inflate(static_cast<float>(i), static_cast<float>(i));
                s.Offset(0.0f, pxf(1.5, dpi));
                addRoundRect(sp, s, pxf(8, dpi) + i);
                gp::SolidBrush b(gc(kBlack, 0.10 * q));
                g.FillPath(&b, &sp);
            }
            gp::GraphicsPath tp;
            addRoundRect(tp, rf, pxf(8, dpi));
            gp::SolidBrush b(gc(kQrLight));
            g.FillPath(&b, &tp);
        } else {
            gdiRoundRect(dc, r, P(8), &kQrLight, mix(kQrLight, kEspresso950, 0.25), 1);
        }
        drawQrModules(dc, r.left + (r.right - r.left - mod * kQrN) / 2, r.top + (r.bottom - r.top - mod * kQrN) / 2, mod);
        GdiFlush();
    }

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

// Plate hover, keyboard focus (when focus cues are showing) and press play the plate's hover;
// the QR enlarges while the pointer is on it or the banner has visible keyboard focus.
// Hovering the headline does neither. The tooltip is silenced while the QR is enlarged so it
// can never sit on top of the code being scanned.
void refreshActive(State* s) {
    s->focusVisible = s->focused &&
                      (SendMessageW(s->hwnd, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS) == 0;
    const double now = nowMs();
    const bool travel = animationsOn();
    s->m.setActive(s->hot || s->qrHot || s->focusVisible || s->pressed, now, travel);
    const bool enlarge = s->qrHot || s->focusVisible;
    if (enlarge != s->m.enlarged && s->tip != nullptr) SendMessageW(s->tip, TTM_ACTIVATE, enlarge ? FALSE : TRUE, 0);
    s->m.setEnlarged(enlarge, now, travel);
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

// The QR's hover target: the resting tile, or - once it has grown - the whole big tile, so the
// pointer can move across the enlarged code without it collapsing.
bool overQr(const State* s, POINT p) {
    RECT t = s->L.tile;
    if (s->m.enlarged) UnionRect(&t, &t, &s->L.bigTile);
    return PtInRect(&t, p) != FALSE;
}

// The link: the plate, plus the enlarged QR while it is showing.
bool overLink(const State* s, POINT p) {
    return PtInRect(&s->L.hit, p) != FALSE || (s->m.enlarged && PtInRect(&s->L.bigTile, p) != FALSE);
}

POINT ptOf(LPARAM lp) { return POINT{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}; }

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
                SetCursor(LoadCursorW(nullptr, overLink(s, p) ? IDC_HAND : IDC_ARROW));
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
                const POINT p = ptOf(lp);
                const bool hot = PtInRect(&s->L.hit, p) != FALSE, qrHot = overQr(s, p);
                if (hot != s->hot || qrHot != s->qrHot) {
                    s->hot = hot;
                    s->qrHot = qrHot;
                    refreshActive(s);
                }
            }
            return 0;

        case WM_MOUSELEAVE:
            if (s != nullptr) {
                s->tracking = false;
                if (s->hot || s->qrHot) {
                    s->hot = false;
                    s->qrHot = false;
                    refreshActive(s);
                }
            }
            return 0;

        case WM_LBUTTONDOWN:
            if (s != nullptr && overLink(s, ptOf(lp))) {
                s->pressed = true;
                SetCapture(hwnd);
                refreshActive(s);
            }
            return 0;

        case WM_LBUTTONUP:
            if (s != nullptr && s->pressed) {
                s->pressed = false;
                ReleaseCapture();
                const POINT p = ptOf(lp);
                const bool inside = overLink(s, p);
                s->hot = PtInRect(&s->L.hit, p) != FALSE;
                s->qrHot = overQr(s, p);
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
