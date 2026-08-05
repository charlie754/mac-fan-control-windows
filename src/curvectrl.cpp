#include "curvectrl.h"

#include <windowsx.h>
#include <cstdio>
#include <algorithm>
#include <vector>

#ifndef WM_DPICHANGED_AFTERPARENT   // absent from the MinGW headers
#define WM_DPICHANGED_AFTERPARENT 0x02E3
#endif

namespace ui {

const wchar_t* kCurveClass = L"MacFanCtlCurve";

namespace {

constexpr double kTempMin = 30.0;
constexpr double kTempMax = 100.0;
constexpr int    kPad     = 6;
constexpr int    kLeft    = 40;   // room for the RPM axis labels
constexpr int    kRight   = 18;   // room for the last temperature label
constexpr int    kBottom  = 20;   // room for the temperature axis labels
constexpr int    kHitR    = 9;

struct State {
    curve::Curve curve;
    double minRpm = 0, maxRpm = 6000;
    int    drag = -1;
    bool   opValid = false;
    double opTemp = 0, opRpm = 0;
    int    dpi = 96;
};

State* stateOf(HWND h) { return (State*)GetWindowLongPtrW(h, GWLP_USERDATA); }

// Scale a 96-DPI design pixel for this control.
int sc(const State* s, int v) { return MulDiv(v, s ? s->dpi : 96, 96); }

int detectDpi(HWND h) {
    if (HMODULE u = GetModuleHandleW(L"user32.dll")) {
        using Fn = UINT(WINAPI*)(HWND);
        if (auto f = (Fn)(void*)GetProcAddress(u, "GetDpiForWindow")) {
            const int d = (int)f(h);
            if (d > 0) return d;
        }
    }
    HDC dc = GetDC(h);
    const int d = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc) ReleaseDC(h, dc);
    return d ? d : 96;
}

RECT plotRect(HWND h) {
    const State* s = stateOf(h);
    RECT rc;
    GetClientRect(h, &rc);
    rc.left   += sc(s, kLeft);
    rc.top    += sc(s, kPad);
    rc.right  -= sc(s, kRight);   // room for the centred "100" label
    rc.bottom -= sc(s, kBottom);
    return rc;
}

POINT toScreen(const RECT& r, const State* s, double t, double rpm) {
    const double tw = kTempMax - kTempMin;
    const double rh = std::max(1.0, s->maxRpm - s->minRpm);
    double fx = (t - kTempMin) / tw;
    double fy = (rpm - s->minRpm) / rh;
    fx = std::clamp(fx, 0.0, 1.0);
    fy = std::clamp(fy, 0.0, 1.0);
    POINT p;
    p.x = r.left + (LONG)(fx * (r.right - r.left));
    p.y = r.bottom - (LONG)(fy * (r.bottom - r.top));
    return p;
}

void toData(const RECT& r, const State* s, POINT p, double* t, double* rpm) {
    const double w = std::max(1L, r.right - r.left);
    const double h = std::max(1L, r.bottom - r.top);
    double fx = (p.x - r.left) / w;
    double fy = (r.bottom - p.y) / h;
    fx = std::clamp(fx, 0.0, 1.0);
    fy = std::clamp(fy, 0.0, 1.0);
    *t   = kTempMin + fx * (kTempMax - kTempMin);
    *rpm = s->minRpm + fy * (s->maxRpm - s->minRpm);
}

int hitTest(HWND h, POINT p) {
    State* s = stateOf(h);
    const RECT r = plotRect(h);
    const int rad = sc(s, kHitR);
    for (size_t i = 0; i < s->curve.points.size(); ++i) {
        const POINT q = toScreen(r, s, s->curve.points[i].tempC, s->curve.points[i].rpm);
        if (std::abs(q.x - p.x) <= rad && std::abs(q.y - p.y) <= rad) return (int)i;
    }
    return -1;
}

void paint(HWND hwnd) {
    State* s = stateOf(hwnd);
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);

    RECT full;
    GetClientRect(hwnd, &full);

    // Double-buffer: this control repaints on every poll tick.
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, full.right, full.bottom);
    HBITMAP oldBmp = (HBITMAP)SelectObject(mem, bmp);

    HBRUSH bg = CreateSolidBrush(RGB(252, 252, 252));
    FillRect(mem, &full, bg);
    DeleteObject(bg);
    FrameRect(mem, &full, (HBRUSH)GetStockObject(GRAY_BRUSH));

    const RECT r = plotRect(hwnd);
    SetBkMode(mem, TRANSPARENT);

    HFONT font = CreateFontW(-sc(s, 11), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT oldFont = (HFONT)SelectObject(mem, font);

    // Grid + axis labels.
    HPEN grid = CreatePen(PS_SOLID, std::max(1, sc(s, 1)), RGB(226, 226, 226));
    HPEN oldPen = (HPEN)SelectObject(mem, grid);
    SetTextColor(mem, RGB(120, 120, 120));

    for (double t = 30.0; t <= 100.0; t += 10.0) {
        const POINT p = toScreen(r, s, t, s->minRpm);
        MoveToEx(mem, p.x, r.top, nullptr);
        LineTo(mem, p.x, r.bottom);
        wchar_t lbl[16];
        swprintf(lbl, 16, L"%d", (int)t);
        RECT tr{p.x - sc(s, 14), r.bottom + sc(s, 2), p.x + sc(s, 14), r.bottom + sc(s, 18)};
        DrawTextW(mem, lbl, -1, &tr, DT_CENTER | DT_SINGLELINE);
    }
    for (int i = 0; i <= 4; ++i) {
        const double rpm = s->minRpm + (s->maxRpm - s->minRpm) * i / 4.0;
        const POINT p = toScreen(r, s, kTempMin, rpm);
        MoveToEx(mem, r.left, p.y, nullptr);
        LineTo(mem, r.right, p.y);
        wchar_t lbl[16];
        swprintf(lbl, 16, L"%d", (int)(rpm + 0.5));
        RECT tr{0, p.y - sc(s, 8), r.left - sc(s, 4), p.y + sc(s, 8)};
        DrawTextW(mem, lbl, -1, &tr, DT_RIGHT | DT_SINGLELINE);
    }
    SelectObject(mem, oldPen);
    DeleteObject(grid);

    // The curve itself.
    if (!s->curve.points.empty()) {
        std::vector<POINT> pts;
        pts.push_back(toScreen(r, s, kTempMin, s->curve.points.front().rpm));
        for (const auto& p : s->curve.points) pts.push_back(toScreen(r, s, p.tempC, p.rpm));
        pts.push_back(toScreen(r, s, kTempMax, s->curve.points.back().rpm));

        HPEN line = CreatePen(PS_SOLID, sc(s, 2), RGB(0, 120, 215));
        oldPen = (HPEN)SelectObject(mem, line);
        Polyline(mem, pts.data(), (int)pts.size());
        SelectObject(mem, oldPen);
        DeleteObject(line);

        HBRUSH knob = CreateSolidBrush(RGB(0, 120, 215));
        HBRUSH oldBrush = (HBRUSH)SelectObject(mem, knob);
        const int kr = sc(s, 4);
        for (const auto& p : s->curve.points) {
            const POINT q = toScreen(r, s, p.tempC, p.rpm);
            Ellipse(mem, q.x - kr, q.y - kr, q.x + kr + 1, q.y + kr + 1);
        }
        SelectObject(mem, oldBrush);
        DeleteObject(knob);
    }

    // Live operating point.
    if (s->opValid) {
        const POINT q = toScreen(r, s, s->opTemp, s->opRpm);
        HBRUSH dot = CreateSolidBrush(RGB(220, 60, 40));
        HBRUSH oldBrush = (HBRUSH)SelectObject(mem, dot);
        HPEN pen = CreatePen(PS_SOLID, sc(s, 1), RGB(180, 40, 20));
        oldPen = (HPEN)SelectObject(mem, pen);
        const int dr = sc(s, 5);
        Ellipse(mem, q.x - dr, q.y - dr, q.x + dr + 1, q.y + dr + 1);
        SelectObject(mem, oldBrush);
        SelectObject(mem, oldPen);
        DeleteObject(dot);
        DeleteObject(pen);

        wchar_t lbl[64];
        swprintf(lbl, 64, L"%.0f °C / %.0f RPM", s->opTemp, s->opRpm);
        SetTextColor(mem, RGB(180, 40, 20));
        RECT tr{r.left + sc(s, 4), r.top + sc(s, 2), r.right - sc(s, 4), r.top + sc(s, 20)};
        DrawTextW(mem, lbl, -1, &tr, DT_RIGHT | DT_SINGLELINE);
    }

    SetTextColor(mem, RGB(140, 140, 140));
    RECT hint{r.left + sc(s, 4), r.top + sc(s, 2), r.right - sc(s, 4), r.top + sc(s, 20)};
    DrawTextW(mem, L"drag to edit · dbl-click adds · right-click removes", -1, &hint,
              DT_LEFT | DT_SINGLELINE);

    SelectObject(mem, oldFont);
    DeleteObject(font);

    BitBlt(dc, 0, 0, full.right, full.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

void notifyParent(HWND hwnd) {
    const HWND parent = GetParent(hwnd);
    if (parent)
        SendMessageW(parent, WM_COMMAND,
                     MAKEWPARAM(GetDlgCtrlID(hwnd), CURVE_CHANGED), (LPARAM)hwnd);
}

LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    State* s = stateOf(hwnd);

    switch (msg) {
        case WM_NCCREATE: {
            auto* st = new State();
            st->dpi = detectDpi(hwnd);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)st);
            return TRUE;
        }

        case WM_DPICHANGED_AFTERPARENT:
            if (s) { s->dpi = detectDpi(hwnd); InvalidateRect(hwnd, nullptr, FALSE); }
            return 0;

        case WM_NCDESTROY:
            delete s;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            return 0;

        case WM_ERASEBKGND:
            return 1;   // fully painted in WM_PAINT

        case WM_PAINT:
            paint(hwnd);
            return 0;

        case WM_LBUTTONDOWN: {
            POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            s->drag = hitTest(hwnd, p);
            if (s->drag >= 0) SetCapture(hwnd);
            return 0;
        }

        case WM_MOUSEMOVE: {
            POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            if (s->drag < 0) {
                SetCursor(LoadCursorW(nullptr, hitTest(hwnd, p) >= 0 ? IDC_SIZEALL : IDC_ARROW));
                return 0;
            }
            const RECT r = plotRect(hwnd);
            double t, rpm;
            toData(r, s, p, &t, &rpm);

            // Keep points ordered: a dragged point stays between its neighbours.
            const double lo = (s->drag > 0) ? s->curve.points[s->drag - 1].tempC + 1.0 : kTempMin;
            const double hi = (s->drag + 1 < (int)s->curve.points.size())
                                ? s->curve.points[s->drag + 1].tempC - 1.0 : kTempMax;
            s->curve.points[s->drag].tempC = std::clamp(t, lo, std::max(lo, hi));
            s->curve.points[s->drag].rpm   = std::clamp(rpm, s->minRpm, s->maxRpm);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_LBUTTONUP:
            if (s->drag >= 0) {
                s->drag = -1;
                ReleaseCapture();
                notifyParent(hwnd);
            }
            return 0;

        case WM_LBUTTONDBLCLK: {
            POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            if (hitTest(hwnd, p) < 0 && s->curve.points.size() < 12) {
                const RECT r = plotRect(hwnd);
                double t, rpm;
                toData(r, s, p, &t, &rpm);
                s->curve.points.push_back({t, std::clamp(rpm, s->minRpm, s->maxRpm)});
                s->curve.sort();
                InvalidateRect(hwnd, nullptr, FALSE);
                notifyParent(hwnd);
            }
            return 0;
        }

        case WM_RBUTTONDOWN: {
            POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            const int i = hitTest(hwnd, p);
            if (i >= 0 && s->curve.points.size() > 2) {
                s->curve.points.erase(s->curve.points.begin() + i);
                InvalidateRect(hwnd, nullptr, FALSE);
                notifyParent(hwnd);
            }
            return 0;
        }

        case WM_SETCURSOR:
            return TRUE;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

void registerCurveClass(HINSTANCE hInst) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = proc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kCurveClass;
    RegisterClassExW(&wc);
}

void curveSet(HWND hwnd, const curve::Curve& c, double minRpm, double maxRpm) {
    State* s = stateOf(hwnd);
    if (!s || s->drag >= 0) return;
    s->curve = c;
    s->minRpm = minRpm;
    s->maxRpm = std::max(minRpm + 1.0, maxRpm);
    InvalidateRect(hwnd, nullptr, FALSE);
}

void curveSetOperatingPoint(HWND hwnd, double tempC, double rpm, bool valid) {
    State* s = stateOf(hwnd);
    if (!s) return;
    s->opTemp = tempC;
    s->opRpm = rpm;
    s->opValid = valid;
    InvalidateRect(hwnd, nullptr, FALSE);
}

curve::Curve curveGet(HWND hwnd) {
    State* s = stateOf(hwnd);
    return s ? s->curve : curve::Curve{};
}

} // namespace ui
