// kofibtn.cpp - Custom Ko-fi support button control for Win32.

#include "kofibtn.h"
#include <windowsx.h>
#include <shellapi.h>
#include <algorithm>

namespace ui {

const wchar_t* kKofiClass = L"MacFanCtlKofiBtn";

namespace {

struct State {
    bool hovered = false;
    bool pressed = false;
    bool tracking = false;
};

static State* stateOf(HWND h) {
    return (State*)GetWindowLongPtrW(h, GWLP_USERDATA);
}

static int detectDpi(HWND h) {
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

static LRESULT CALLBACK kofiProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    State* s = stateOf(hwnd);

    switch (msg) {
        case WM_CREATE: {
            s = new State();
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)s);
            return 0;
        }

        case WM_DESTROY: {
            if (s) {
                delete s;
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            }
            return 0;
        }

        case WM_MOUSEMOVE: {
            if (s && !s->tracking) {
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
                TrackMouseEvent(&tme);
                s->tracking = true;
            }
            if (s && !s->hovered) {
                s->hovered = true;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }

        case WM_MOUSELEAVE: {
            if (s) {
                s->tracking = false;
                s->hovered = false;
                s->pressed = false;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }

        case WM_LBUTTONDOWN: {
            if (s) {
                s->pressed = true;
                SetCapture(hwnd);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }

        case WM_LBUTTONUP: {
            if (s && s->pressed) {
                s->pressed = false;
                ReleaseCapture();
                InvalidateRect(hwnd, nullptr, FALSE);

                RECT rc;
                GetClientRect(hwnd, &rc);
                POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
                if (PtInRect(&rc, pt)) {
                    ShellExecuteW(nullptr, L"open", L"https://ko-fi.com/IRP_HongKong",
                                  nullptr, nullptr, SW_SHOWNORMAL);
                }
            }
            return 0;
        }

        case WM_ERASEBKGND:
            return 1; // Handled in WM_PAINT via double buffering

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            if (!hdc) return 0;

            RECT rc;
            GetClientRect(hwnd, &rc);
            const int W = rc.right - rc.left;
            const int H = rc.bottom - rc.top;

            const int dpi = detectDpi(hwnd);
            auto S = [dpi](int v) { return MulDiv(v, dpi, 96); };

            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBmp = CreateCompatibleBitmap(hdc, W, H);
            HGDIOBJ oldBmp = SelectObject(memDC, memBmp);

            // Fill parent background color
            HBRUSH hParentBg = GetSysColorBrush(COLOR_BTNFACE);
            FillRect(memDC, &rc, hParentBg);

            // Button background color: Ko-fi Coral Red (#FF5E5B)
            COLORREF bgColor = RGB(255, 94, 91);
            if (s && s->pressed) {
                bgColor = RGB(220, 70, 68);
            } else if (s && s->hovered) {
                bgColor = RGB(255, 115, 112);
            }

            // Draw rounded rectangle for Ko-fi button (50% scaled: radius = 15)
            HBRUSH hBtnBrush = CreateSolidBrush(bgColor);
            HPEN hNullPen = CreatePen(PS_NULL, 0, 0);
            HGDIOBJ oldBrush = SelectObject(memDC, hBtnBrush);
            HGDIOBJ oldPen = SelectObject(memDC, hNullPen);
            const int radius = S(15);
            RoundRect(memDC, 0, 0, W, H, radius, radius);
            SelectObject(memDC, oldBrush);
            SelectObject(memDC, oldPen);
            DeleteObject(hBtnBrush);
            DeleteObject(hNullPen);

            // Prepare font (50% bigger: font height -S(15))
            HFONT hFont = CreateFontW(-S(15), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
                                      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                      DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            HGDIOBJ oldFont = SelectObject(memDC, hFont);

            const wchar_t* line1 = L"Support me on Ko-fi";
            const wchar_t* line2 = L"@IRP_HongKong";
            SIZE sz1{}, sz2{};
            GetTextExtentPoint32W(memDC, line1, (int)wcslen(line1), &sz1);
            GetTextExtentPoint32W(memDC, line2, (int)wcslen(line2), &sz2);

            const int maxTextW = std::max((int)sz1.cx, (int)sz2.cx);
            const int iconW = S(27);
            const int iconH = S(24);
            const int gap = S(12);
            const int totalContentW = iconW + gap + maxTextW;
            const int startX = (W - totalContentW) / 2;

            // Draw Coffee Cup Icon (White, 50% larger)
            const int iconX = startX;
            const int iconY = (H - iconH) / 2;

            HPEN hWhitePen2 = CreatePen(PS_SOLID, S(3), RGB(255, 255, 255));
            HGDIOBJ oldPen2 = SelectObject(memDC, hWhitePen2);

            // Saucer line
            MoveToEx(memDC, iconX, iconY + S(21), nullptr);
            LineTo(memDC, iconX + S(24), iconY + S(21));

            // Cup Body
            POINT cupPts[4];
            cupPts[0] = { iconX + S(3),  iconY + S(6) };
            cupPts[1] = { iconX + S(21), iconY + S(6) };
            cupPts[2] = { iconX + S(18), iconY + S(18) };
            cupPts[3] = { iconX + S(6),  iconY + S(18) };
            HBRUSH hWhiteBrush = CreateSolidBrush(RGB(255, 255, 255));
            HGDIOBJ oldBrush2 = SelectObject(memDC, hWhiteBrush);
            Polygon(memDC, cupPts, 4);
            SelectObject(memDC, oldBrush2);
            DeleteObject(hWhiteBrush);

            // Handle loop
            Arc(memDC, iconX + S(15), iconY + S(6), iconX + S(26), iconY + S(18),
                iconX + S(21), iconY + S(6), iconX + S(21), iconY + S(18));

            SelectObject(memDC, oldPen2);
            DeleteObject(hWhitePen2);

            // Draw Text Lines (White)
            SetBkMode(memDC, TRANSPARENT);
            SetTextColor(memDC, RGB(255, 255, 255));

            const int textX = startX + iconW + gap;
            const int lineH = sz1.cy + S(2);
            const int totalTextH = lineH + sz2.cy;
            const int textY = (H - totalTextH) / 2;

            RECT rc1 = { textX, textY, textX + maxTextW, textY + lineH };
            RECT rc2 = { textX, textY + lineH, textX + maxTextW, textY + totalTextH };

            DrawTextW(memDC, line1, -1, &rc1, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);
            DrawTextW(memDC, line2, -1, &rc2, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);

            SelectObject(memDC, oldFont);
            DeleteObject(hFont);

            // Blit to screen
            BitBlt(hdc, 0, 0, W, H, memDC, 0, 0, SRCCOPY);

            SelectObject(memDC, oldBmp);
            DeleteBitmap(memBmp);
            DeleteDC(memDC);

            EndPaint(hwnd, &ps);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

void registerKofiClass(HINSTANCE hInst) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = kofiProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_HAND);
    wc.lpszClassName = kKofiClass;
    RegisterClassExW(&wc);
}

} // namespace ui
