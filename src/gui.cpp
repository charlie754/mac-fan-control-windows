// gui.cpp - Win32 front end: live sensor list, per-fan control panels with a
// draggable curve editor, and a tray icon.

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include <cstdio>
#include <algorithm>
#include <string>
#include <vector>

#include "controller.h"
#include "config.h"
#include "curvectrl.h"
#include "kofibtn.h"
#include "startup.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

using namespace app;

// ---- ids ------------------------------------------------------------------

constexpr int IDC_LIST      = 1000;
constexpr int IDC_STATUS    = 1001;
constexpr int IDC_KOFI      = 1002;
constexpr int IDC_STARTUP   = 1003;   // "Run on startup" checkbox

// "Not connected" notice, shown instead of the sensor list and fan panels.
constexpr int IDC_NC_TITLE  = 1010;
constexpr int IDC_NC_BODY   = 1011;
constexpr int IDC_NC_FIX    = 1012;   // primary action; label depends on the cause
constexpr int IDC_NC_RETRY  = 1013;

constexpr int FAN_STRIDE    = 20;
constexpr int IDC_FAN_BASE  = 2000;
enum FanCtl {
    F_GROUP = 0, F_NAME, F_RPM, F_RADIO_SYS, F_RADIO_MAN, F_RADIO_CRV,
    F_SLIDER, F_SLIDER_LBL, F_SRC_LBL, F_SRC_COMBO, F_CURVE
};
static int fanId(size_t fan, FanCtl c) { return IDC_FAN_BASE + (int)fan * FAN_STRIDE + (int)c; }
static bool fanFromId(int id, size_t* fan, FanCtl* c) {
    if (id < IDC_FAN_BASE) return false;
    const int off = id - IDC_FAN_BASE;
    *fan = off / FAN_STRIDE;
    *c = (FanCtl)(off % FAN_STRIDE);
    return true;
}

constexpr UINT WM_TRAY      = WM_APP + 1;
constexpr int  IDM_SHOW     = 40001;
constexpr int  IDM_ALLAUTO  = 40002;
constexpr int  IDM_EXIT     = 40003;
constexpr int  IDM_STARTUP  = 40004;
constexpr int  IDT_POLL     = 1;
constexpr int  IDT_RETRY_LABEL = 2;

// How long a launch at sign-in may stay hidden in the tray without reaching
// the fans before the window opens to say why.
constexpr ULONGLONG kQuietStartGraceMs = 30000;

// ---- app state ------------------------------------------------------------

namespace {

struct Ui {
    HINSTANCE inst = nullptr;
    HWND      main = nullptr;
    HWND      list = nullptr;
    HWND      status = nullptr;
    HWND      kofi = nullptr;
    HWND      startup = nullptr;
    HFONT     font = nullptr;
    HFONT     fontBig = nullptr;
    HFONT     fontBold = nullptr;
    int       dpi = 96;   // Retina Macs run at 192 (200%), so nothing may be
                          // laid out in raw pixels.
    Controller* ctl = nullptr;
    Snapshot  snap;
    size_t    fanCount = 0;
    bool      inTray = false;
    bool      quitting = false;
    bool      panelsBuilt = false;   // fan panels are built on first connect
    bool      showingNotice = false;
    bool      quietStart = false;    // launched at sign-in: stay in the tray
    ULONGLONG startedAt = 0;         // GetTickCount64() when the window was set up
    UINT      taskbarCreated = 0;    // broadcast when Explorer (re)starts
    HANDLE    instanceMutex = nullptr;
    std::vector<std::pair<std::string, std::wstring>> tempChoices;
};

Ui g;

std::wstring widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }

void setFont(HWND h, HFONT f) { SendMessageW(h, WM_SETFONT, (WPARAM)f, TRUE); }

// Scale a 96-DPI design pixel to the current display.
int S(int v) { return MulDiv(v, g.dpi, 96); }

// GetDpiForWindow / GetDpiForSystem only exist on Win10 1607+; resolve them
// dynamically so the binary still starts on older builds.
int systemDpi() {
    if (HMODULE u = GetModuleHandleW(L"user32.dll")) {
        using Fn = UINT(WINAPI*)();
        if (auto f = (Fn)(void*)GetProcAddress(u, "GetDpiForSystem")) return (int)f();
    }
    HDC dc = GetDC(nullptr);
    const int d = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc) ReleaseDC(nullptr, dc);
    return d ? d : 96;
}

int windowDpi(HWND hwnd) {
    if (HMODULE u = GetModuleHandleW(L"user32.dll")) {
        using Fn = UINT(WINAPI*)(HWND);
        if (auto f = (Fn)(void*)GetProcAddress(u, "GetDpiForWindow")) {
            const int d = (int)f(hwnd);
            if (d > 0) return d;
        }
    }
    return systemDpi();
}

HFONT makeFont(int pt96, int weight) {
    return CreateFontW(-S(pt96), 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

void rebuildFonts() {
    HFONT oldF = g.font, oldB = g.fontBold, oldG = g.fontBig;
    g.font     = makeFont(12, FW_NORMAL);
    g.fontBold = makeFont(13, FW_SEMIBOLD);
    g.fontBig  = makeFont(21, FW_SEMIBOLD);
    if (oldF) DeleteObject(oldF);
    if (oldB) DeleteObject(oldB);
    if (oldG) DeleteObject(oldG);
}

// ---- persistence ----------------------------------------------------------

void persist() {
    if (g.ctl) saveConfig(g.ctl->configs());
}

// ---- tray -----------------------------------------------------------------

NOTIFYICONDATAW trayData() {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof nid;
    nid.hWnd = g.main;
    nid.uID = 1;
    return nid;
}

void trayAdd() {
    NOTIFYICONDATAW nid = trayData();
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAY;
    nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(nid.szTip, L"MacFanCtl");
    // Right after sign-in the shell may still be starting: NIM_ADD can fail,
    // or time out even though the icon went in, which NIM_MODIFY confirms.
    // The poll timer retries until the icon is there, because after a quiet
    // start it is the only way into the app.
    g.inTray = Shell_NotifyIconW(NIM_ADD, &nid) || Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void trayRemove() {
    if (!g.inTray) return;
    NOTIFYICONDATAW nid = trayData();
    Shell_NotifyIconW(NIM_DELETE, &nid);
    g.inTray = false;
}

void trayUpdateTip() {
    if (!g.inTray) return;
    NOTIFYICONDATAW nid = trayData();
    nid.uFlags = NIF_TIP;
    std::wstring tip = L"MacFanCtl";
    if (g.snap.connected) {
        wchar_t b[128];
        swprintf(b, 128, L"\n%.0f °C", g.snap.hottest);
        tip += b;
        for (const auto& f : g.snap.fans) {
            swprintf(b, 128, L"\n%ls: %.0f RPM", f.name.c_str(), f.curRpm);
            tip += b;
        }
    }
    if (tip.size() > 127) tip.resize(127);
    wcscpy_s(nid.szTip, tip.c_str());
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

// ---- run on startup -------------------------------------------------------

// The registry is the source of truth: Task Manager's Startup tab can switch
// the entry off while the app is running.
void syncStartupCheck() {
    if (g.startup)
        Button_SetCheck(g.startup, startup::isEnabled() ? BST_CHECKED : BST_UNCHECKED);
}

void setRunOnStartup(bool on) {
    if (!startup::setEnabled(on)) {
        MessageBoxW(g.main,
                    on ? L"MacFanCtl couldn't add itself to your startup apps."
                       : L"MacFanCtl couldn't remove itself from your startup apps.",
                    L"Run on startup", MB_ICONWARNING | MB_OK);
    }
    syncStartupCheck();
}

bool launchedWith(const wchar_t* sw) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return false;
    bool found = false;
    for (int i = 1; i < argc && !found; ++i) found = _wcsicmp(argv[i], sw) == 0;
    LocalFree(argv);
    return found;
}

// ---- "not connected" notice ----------------------------------------------
//
// Written for someone who has never heard of a kernel driver. Each cause gets
// a concrete next step rather than a dead end, and the app keeps retrying in
// the background so a fix applied outside the app is picked up on its own.

struct Notice {
    const wchar_t* title;
    const wchar_t* body;
    const wchar_t* fixLabel;   // nullptr = no primary action for this cause
};

Notice noticeFor(smc::Status s) {
    switch (s) {
        case smc::Status::ServiceStopped:
            return {L"MacFanCtl just needs permission",
                    L"The part of Windows that reads your Mac's fans is installed but "
                    L"switched off, and starting it needs administrator permission.\n\n"
                    L"Click the button below and choose \"Yes\" when Windows asks. "
                    L"MacFanCtl will restart and connect by itself.",
                    L"Fix this for me"};

        case smc::Status::DeviceBusy:
            return {L"Another program is using the fans",
                    L"Only one program can control your Mac's fans at a time, and "
                    L"something else has them right now.\n\n"
                    L"Close the other fan or temperature app - including its icon near "
                    L"the clock, bottom-right - and MacFanCtl will connect on its own "
                    L"within a few seconds.",
                    L"Show me what's using it"};

        case smc::Status::NoDriver:
            return {L"MacFanCtl can't reach your Mac's fans",
                    L"Reading fans and temperatures on a Mac needs a small Windows "
                    L"driver, and this PC doesn't have one set up yet.\n\n"
                    L"MacFanCtl doesn't include that driver, so it can't install it for "
                    L"you. The setup steps are on the project page.",
                    L"Open setup instructions"};

        default:
            return {L"MacFanCtl can't reach your Mac's fans",
                    L"Something went wrong talking to your Mac's fan controller.\n\n"
                    L"The exact error is shown along the bottom of this window.",
                    nullptr};
    }
}

// ---- layout ---------------------------------------------------------------

constexpr int kListW  = 400;
constexpr int kMargin = 10;
constexpr int kStatusH = 54;
constexpr int kKofiW  = 270;
constexpr int kKofiH  = 48;
constexpr int kStartupW = 130;

void layout() {
    RECT rc;
    GetClientRect(g.main, &rc);
    const int W = rc.right, H = rc.bottom;
    const int bodyH = H - S(kStatusH);
    const int margin = S(kMargin);
    const int listW = S(kListW);
    const int kofiW = S(kKofiW);
    const int kofiH = S(kKofiH);
    const int chkW = S(kStartupW);
    const int rowY = bodyH + (S(kStatusH) - S(24)) / 2;

    // Bottom bar: status text | "Run on startup" | Ko-fi. It stays visible
    // while the notice is up, so startup can be set before the first connect.
    MoveWindow(g.list, margin, margin, listW, bodyH - 2 * margin, TRUE);
    MoveWindow(g.status, margin, rowY, W - kofiW - chkW - 5 * margin, S(24), TRUE);
    if (g.startup) MoveWindow(g.startup, W - kofiW - chkW - 3 * margin, rowY, chkW, S(24), TRUE);
    if (g.kofi) MoveWindow(g.kofi, W - kofiW - margin, bodyH + (S(kStatusH) - kofiH) / 2, kofiW, kofiH, TRUE);

    if (g.showingNotice) {
        // Centred column across the whole client area.
        const int w  = std::min(W - 4 * margin, S(620));
        const int x  = (W - w) / 2;
        const int bh = S(34);
        int y = std::max(margin, (bodyH - S(300)) / 2);
        auto mv = [&](int id, int h, int cx = -1, int cw = -1) {
            if (HWND c = GetDlgItem(g.main, id))
                MoveWindow(c, cx < 0 ? x : cx, y, cw < 0 ? w : cw, h, TRUE);
            y += h;
        };
        mv(IDC_NC_TITLE, S(40));
        y += S(8);
        mv(IDC_NC_BODY, S(150));
        y += S(14);
        const int bw = S(190), gap = S(12);
        if (HWND fix = GetDlgItem(g.main, IDC_NC_FIX))
            MoveWindow(fix, x, y, bw, bh, TRUE);
        if (HWND rty = GetDlgItem(g.main, IDC_NC_RETRY))
            MoveWindow(rty, x + bw + gap, y, S(130), bh, TRUE);
        return;
    }

    if (g.fanCount == 0) return;
    const int fx = margin * 2 + listW;
    const int fw = W - fx - margin;
    const int totalH = bodyH - 2 * margin;
    const int fh = totalH / (int)g.fanCount;

    for (size_t i = 0; i < g.fanCount; ++i) {
        const int py = margin + (int)i * fh;
        const int ih = fh - S(8);
        auto mv = [&](FanCtl c, int x, int y, int w, int h) {
            if (HWND h2 = GetDlgItem(g.main, fanId(i, c)))
                MoveWindow(h2, x, y, w, h, TRUE);
        };
        mv(F_GROUP,      fx,              py,          fw,             ih);
        mv(F_NAME,       fx + S(14),      py + S(20),  fw - S(200),    S(20));
        mv(F_RPM,        fx + fw - S(190),py + S(14),  S(176),         S(30));
        mv(F_RADIO_SYS,  fx + S(14),      py + S(48),  S(110),         S(22));
        mv(F_RADIO_MAN,  fx + S(128),     py + S(48),  S(90),          S(22));
        mv(F_RADIO_CRV,  fx + S(222),     py + S(48),  S(130),         S(22));
        mv(F_SLIDER,     fx + S(12),      py + S(74),  fw - S(120),    S(28));
        mv(F_SLIDER_LBL, fx + fw - S(104),py + S(78),  S(92),          S(20));
        mv(F_SRC_LBL,    fx + S(14),      py + S(110), S(50),          S(20));
        mv(F_SRC_COMBO,  fx + S(66),      py + S(106), fw - S(82),     S(260));
        mv(F_CURVE,      fx + S(14),      py + S(138), fw - S(28),     ih - S(148));
    }
}

// ---- per-fan control creation --------------------------------------------

void createFanPanel(size_t i, const FanState& f) {
    auto mk = [&](FanCtl c, const wchar_t* cls, const wchar_t* text, DWORD style,
                  DWORD ex = 0) -> HWND {
        HWND h = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style,
                                 0, 0, 10, 10, g.main, (HMENU)(INT_PTR)fanId(i, c),
                                 g.inst, nullptr);
        setFont(h, g.font);
        return h;
    };

    mk(F_GROUP, L"BUTTON", L"", BS_GROUPBOX);
    HWND name = mk(F_NAME, L"STATIC", f.name.c_str(), SS_LEFT);
    setFont(name, g.fontBold);
    HWND rpm = mk(F_RPM, L"STATIC", L"—", SS_RIGHT);
    setFont(rpm, g.fontBig);

    mk(F_RADIO_SYS, L"BUTTON", L"System", BS_AUTORADIOBUTTON | WS_GROUP);
    mk(F_RADIO_MAN, L"BUTTON", L"Manual", BS_AUTORADIOBUTTON);
    mk(F_RADIO_CRV, L"BUTTON", L"Auto (curve)", BS_AUTORADIOBUTTON);

    HWND sl = mk(F_SLIDER, TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS);
    SendMessageW(sl, TBM_SETRANGE, TRUE,
                 MAKELPARAM((int)f.minRpm, (int)std::max(f.minRpm + 1, f.maxRpm)));
    SendMessageW(sl, TBM_SETPOS, TRUE, (LPARAM)(int)f.manualRpm);

    mk(F_SLIDER_LBL, L"STATIC", L"", SS_RIGHT);
    mk(F_SRC_LBL, L"STATIC", L"Source:", SS_LEFT);

    HWND cb = mk(F_SRC_COMBO, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL);
    for (const auto& t : g.tempChoices)
        SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)t.second.c_str());
    SendMessageW(cb, CB_SETCURSEL, 0, 0);

    HWND cv = CreateWindowExW(0, ui::kCurveClass, L"", WS_CHILD | WS_VISIBLE,
                              0, 0, 10, 10, g.main,
                              (HMENU)(INT_PTR)fanId(i, F_CURVE), g.inst, nullptr);
    ui::curveSet(cv, f.curve, f.minRpm, f.maxRpm);

    CheckRadioButton(g.main, fanId(i, F_RADIO_SYS), fanId(i, F_RADIO_CRV),
                     fanId(i, f.mode == FanMode::Manual ? F_RADIO_MAN
                            : f.mode == FanMode::Curve  ? F_RADIO_CRV : F_RADIO_SYS));
}

void syncFanEnabled(size_t i, FanMode m) {
    EnableWindow(GetDlgItem(g.main, fanId(i, F_SLIDER)),    m == FanMode::Manual);
    EnableWindow(GetDlgItem(g.main, fanId(i, F_SRC_COMBO)), m == FanMode::Curve);
    EnableWindow(GetDlgItem(g.main, fanId(i, F_CURVE)),     m == FanMode::Curve);
}

// ---- connect / disconnect transitions -------------------------------------

// Builds the fan panels the first time a connection succeeds. The app can now
// open before the SMC is reachable, so this cannot happen at startup.
void buildFanPanels() {
    if (g.panelsBuilt || !g.ctl) return;
    g.snap = g.ctl->snapshot();
    g.fanCount = g.snap.fans.size();
    if (g.fanCount == 0) return;

    g.tempChoices = g.ctl->temperatureChoices();
    for (size_t i = 0; i < g.fanCount; ++i) {
        const FanState& f = g.snap.fans[i];
        createFanPanel(i, f);
        syncFanEnabled(i, f.mode);
        if (!f.sensorKey.empty()) {
            for (size_t j = 0; j < g.tempChoices.size(); ++j)
                if (g.tempChoices[j].first == f.sensorKey)
                    SendDlgItemMessageW(g.main, fanId(i, F_SRC_COMBO), CB_SETCURSEL, j, 0);
        }
    }
    if (!g.snap.controllable) {
        for (size_t i = 0; i < g.fanCount; ++i) {
            EnableWindow(GetDlgItem(g.main, fanId(i, F_RADIO_MAN)), FALSE);
            EnableWindow(GetDlgItem(g.main, fanId(i, F_RADIO_CRV)), FALSE);
        }
    }
    g.panelsBuilt = true;
    // The panels were created at a placeholder size. When the very first poll
    // is already connected there is no notice transition to trigger a layout.
    layout();
}

void setNoticeVisible(bool show, smc::Status st) {
    static smc::Status shownFor = smc::Status::Connected;
    const bool changed = (show != g.showingNotice) || (show && st != shownFor);
    if (!changed) return;
    g.showingNotice = show;
    shownFor = st;

    const int sw = show ? SW_SHOW : SW_HIDE;
    for (int id : {IDC_NC_TITLE, IDC_NC_BODY, IDC_NC_FIX, IDC_NC_RETRY})
        if (HWND c = GetDlgItem(g.main, id)) ShowWindow(c, sw);

    // Everything that only makes sense while connected.
    ShowWindow(g.list, show ? SW_HIDE : SW_SHOW);
    for (size_t i = 0; i < g.fanCount; ++i)
        for (int c = F_GROUP; c <= F_CURVE; ++c)
            if (HWND h = GetDlgItem(g.main, fanId(i, (FanCtl)c)))
                ShowWindow(h, show ? SW_HIDE : SW_SHOW);

    if (show) {
        const Notice n = noticeFor(st);
        SetDlgItemTextW(g.main, IDC_NC_TITLE, n.title);
        SetDlgItemTextW(g.main, IDC_NC_BODY, n.body);
        SetDlgItemTextW(g.main, IDC_NC_RETRY, L"Try again now");
        if (HWND fix = GetDlgItem(g.main, IDC_NC_FIX)) {
            if (n.fixLabel) {
                SetWindowTextW(fix, n.fixLabel);
                ShowWindow(fix, SW_SHOW);
            } else {
                ShowWindow(fix, SW_HIDE);
            }
        }
    }
    layout();
    InvalidateRect(g.main, nullptr, TRUE);
}

// ---- refresh --------------------------------------------------------------

void refresh() {
    if (!g.ctl) return;
    g.snap = g.ctl->snapshot();

    // A launch at sign-in stays in the tray only while things work. If the fans
    // still can't be reached after a grace period, open the window so the
    // reason and its fix are on screen rather than failing silently.
    if (g.quietStart) {
        if (g.snap.connected) {
            g.quietStart = false;
        } else if (GetTickCount64() - g.startedAt > kQuietStartGraceMs) {
            g.quietStart = false;
            ShowWindow(g.main, SW_SHOW);
        }
    }

    if (!g.snap.connected) {
        // The controller keeps retrying on its own; just reflect the state.
        setNoticeVisible(true, g.snap.statusCode);
        SetWindowTextW(g.status, (L"  " + g.snap.status).c_str());
        trayUpdateTip();
        return;
    }

    // Connected: build the panels on the first successful attach, then reveal.
    buildFanPanels();
    setNoticeVisible(false, smc::Status::Connected);

    ListView_SetItemCountEx(g.list, (int)g.snap.sensors.size(), LVSICF_NOSCROLL);
    InvalidateRect(g.list, nullptr, FALSE);

    for (size_t i = 0; i < g.snap.fans.size() && i < g.fanCount; ++i) {
        const FanState& f = g.snap.fans[i];
        wchar_t b[128];

        swprintf(b, 128, L"%.0f RPM", f.curRpm);
        SetDlgItemTextW(g.main, fanId(i, F_RPM), b);

        swprintf(b, 128, L"%ls   (%.0f–%.0f RPM)", f.name.c_str(), f.minRpm, f.maxRpm);
        SetDlgItemTextW(g.main, fanId(i, F_NAME), b);

        if (f.mode == FanMode::Manual)
            swprintf(b, 128, L"%d RPM",
                     (int)SendDlgItemMessageW(g.main, fanId(i, F_SLIDER), TBM_GETPOS, 0, 0));
        else
            swprintf(b, 128, L"target %.0f", f.tgtRpm);
        SetDlgItemTextW(g.main, fanId(i, F_SLIDER_LBL), b);

        if (HWND cv = GetDlgItem(g.main, fanId(i, F_CURVE))) {
            // Only the source temperature drives the dot's X; the Y is the
            // actual measured speed, so a lagging fan is visible.
            double t = g.snap.hottest;
            if (!f.sensorKey.empty()) {
                for (const auto& s : g.snap.sensors)
                    if (s.key.str() == f.sensorKey) { t = s.value; break; }
            }
            ui::curveSetOperatingPoint(cv, t, f.curRpm,
                                       f.mode == FanMode::Curve && t > 0);
        }
    }

    std::wstring st = g.snap.status;
    if (g.snap.connected) {
        wchar_t b[256];
        swprintf(b, 256, L"  %ls  ·  protocol %ls  ·  %zu sensors  ·  hottest: %ls %.1f °C",
                 g.snap.status.c_str(), g.snap.protocolName.c_str(),
                 g.snap.sensors.size(),
                 g.snap.hottestName.empty() ? L"—" : g.snap.hottestName.c_str(),
                 g.snap.hottest);
        st = b;
    }
    SetWindowTextW(g.status, st.c_str());
    trayUpdateTip();
}

// ---- commands -------------------------------------------------------------

void onFanCommand(size_t i, FanCtl c, WORD notify, HWND ctrl) {
    if (!g.ctl) return;

    switch (c) {
        case F_RADIO_SYS:
        case F_RADIO_MAN:
        case F_RADIO_CRV: {
            if (notify != BN_CLICKED) return;
            const FanMode m = (c == F_RADIO_MAN) ? FanMode::Manual
                            : (c == F_RADIO_CRV) ? FanMode::Curve : FanMode::Auto;
            g.ctl->setFanMode(i, m);
            syncFanEnabled(i, m);
            persist();
            return;
        }
        case F_SRC_COMBO: {
            if (notify != CBN_SELCHANGE) return;
            const int sel = (int)SendMessageW(ctrl, CB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < (int)g.tempChoices.size()) {
                g.ctl->setCurveSensor(i, g.tempChoices[sel].first);
                persist();
            }
            return;
        }
        case F_CURVE: {
            if (notify != ui::CURVE_CHANGED) return;
            g.ctl->setCurve(i, ui::curveGet(ctrl));
            persist();
            return;
        }
        default: return;
    }
}

void onSlider(HWND bar, WORD code) {
    size_t i; FanCtl c;
    if (!fanFromId(GetDlgCtrlID(bar), &i, &c) || c != F_SLIDER) return;
    const int pos = (int)SendMessageW(bar, TBM_GETPOS, 0, 0);
    if (g.ctl) g.ctl->setManualRpm(i, pos);
    wchar_t b[64];
    swprintf(b, 64, L"%d RPM", pos);
    SetDlgItemTextW(g.main, fanId(i, F_SLIDER_LBL), b);

    // Track continuously for live feedback, but only write settings once the
    // user lets go, so dragging does not hammer the config file.
    if (code != TB_THUMBTRACK) persist();
}

void allSystem() {
    if (!g.ctl) return;
    for (size_t i = 0; i < g.fanCount; ++i) {
        g.ctl->setFanMode(i, FanMode::Auto);
        CheckRadioButton(g.main, fanId(i, F_RADIO_SYS), fanId(i, F_RADIO_CRV),
                         fanId(i, F_RADIO_SYS));
        syncFanEnabled(i, FanMode::Auto);
    }
    persist();
}

// ---- "not connected" actions ----------------------------------------------

// Restarts MacFanCtl through the standard Windows permission prompt so it can
// start the already-installed service. The single-instance guard and the SMC
// handle are released first, or the new copy would bounce off our own guard.
void relaunchElevated() {
    wchar_t path[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) return;

    if (g.ctl) g.ctl->stop();
    if (g.instanceMutex) { CloseHandle(g.instanceMutex); g.instanceMutex = nullptr; }

    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof sei;
    sei.lpVerb = L"runas";
    sei.lpFile = path;
    sei.nShow  = SW_SHOWNORMAL;

    if (ShellExecuteExW(&sei)) {
        if (sei.hProcess) CloseHandle(sei.hProcess);
        g.quitting = true;
        DestroyWindow(g.main);
        return;
    }
    // Prompt declined or failed: stay running unelevated rather than dying.
    g.instanceMutex = CreateMutexW(nullptr, TRUE, L"Local\\MacFanCtlSingleInstance");
    if (g.ctl) g.ctl->start();
}

// Windows won't tell us who holds a device handle without a driver of our own,
// so name the fan/monitoring tools that are actually running right now.
void showBusyHolders() {
    static const wchar_t* kKnown[] = {
        L"MacsFanControl", L"smcFanControl", L"TGPro", L"TG Pro", L"HWMonitor",
        L"OpenHardwareMonitor", L"LibreHardwareMonitor", L"SpeedFan", L"MacFanCtl",
    };
    std::wstring found;
    const DWORD self = GetCurrentProcessId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe{};
        pe.dwSize = sizeof pe;
        for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
            if (pe.th32ProcessID == self) continue;
            std::wstring name = pe.szExeFile;
            for (const wchar_t* k : kKnown) {
                if (_wcsnicmp(name.c_str(), k, wcslen(k)) == 0) {
                    if (found.find(name) == std::wstring::npos)
                        found += L"    \x2022  " + name + L"\n";
                    break;
                }
            }
        }
        CloseHandle(snap);
    }

    std::wstring msg;
    if (found.empty()) {
        msg = L"MacFanCtl couldn't spot a program it recognises.\n\n"
              L"Look for a fan or temperature app's icon near the clock, "
              L"bottom-right of your screen - you may need to click the small "
              L"arrow to see hidden icons. Right-click it and choose Quit or Exit.\n\n"
              L"MacFanCtl will connect on its own once it lets go.";
    } else {
        msg = L"These programs are running and may be using your Mac's fans:\n\n" +
              found +
              L"\nClose them - including any icon near the clock, bottom-right - "
              L"and MacFanCtl will connect on its own within a few seconds.";
    }
    MessageBoxW(g.main, msg.c_str(), L"What's using the fans", MB_ICONINFORMATION | MB_OK);
}

void openSetupInstructions() {
    ShellExecuteW(g.main, L"open",
                  L"https://github.com/charlie754/mac-fan-control-windows#requirements",
                  nullptr, nullptr, SW_SHOWNORMAL);
}

void onFixClicked() {
    switch (g.snap.statusCode) {
        case smc::Status::ServiceStopped: relaunchElevated();       break;
        case smc::Status::DeviceBusy:     showBusyHolders();        break;
        case smc::Status::NoDriver:       openSetupInstructions();  break;
        default: break;
    }
}

void showTrayMenu() {
    POINT p;
    GetCursorPos(&p);
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, IDM_SHOW, IsWindowVisible(g.main) ? L"Hide window" : L"Show window");
    AppendMenuW(m, MF_STRING, IDM_ALLAUTO, L"Return all fans to system control");
    AppendMenuW(m, MF_STRING | (startup::isEnabled() ? MF_CHECKED : MF_UNCHECKED),
                IDM_STARTUP, L"Run on startup");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, IDM_EXIT, L"Exit");
    SetForegroundWindow(g.main);
    TrackPopupMenu(m, TPM_RIGHTBUTTON, p.x, p.y, 0, g.main, nullptr);
    DestroyMenu(m);
}

// ---- window proc ----------------------------------------------------------

LRESULT CALLBACK mainProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (g.taskbarCreated && msg == g.taskbarCreated) {
        // Explorer restarted and every tray icon went with it.
        g.inTray = false;
        trayAdd();
        return 0;
    }

    switch (msg) {
        case WM_CREATE: {
            g.main = hwnd;
            g.dpi = windowDpi(hwnd);
            rebuildFonts();

            g.list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_OWNERDATA |
                LVS_NOSORTHEADER,
                0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)IDC_LIST, g.inst, nullptr);
            ListView_SetExtendedListViewStyle(g.list,
                LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_GRIDLINES);
            setFont(g.list, g.font);

            LVCOLUMNW col{};
            col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
            col.pszText = (LPWSTR)L"Sensor";      col.cx = S(210); col.iSubItem = 0;
            ListView_InsertColumn(g.list, 0, &col);
            col.pszText = (LPWSTR)L"Key";         col.cx = S(60);  col.iSubItem = 1;
            ListView_InsertColumn(g.list, 1, &col);
            col.pszText = (LPWSTR)L"Value";       col.cx = S(90);  col.iSubItem = 2;
            ListView_InsertColumn(g.list, 2, &col);

            g.status = CreateWindowExW(0, L"STATIC", L"Starting…",
                WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP | SS_CENTERIMAGE,
                0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)IDC_STATUS, g.inst, nullptr);
            setFont(g.status, g.font);

            g.startup = CreateWindowExW(0, L"BUTTON", L"Run on startup",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)IDC_STARTUP, g.inst, nullptr);
            setFont(g.startup, g.font);
            syncStartupCheck();

            g.kofi = CreateWindowExW(0, ui::kKofiClass, L"",
                WS_CHILD | WS_VISIBLE,
                0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)IDC_KOFI, g.inst, nullptr);

            // "Not connected" notice: created hidden, shown only when needed.
            auto nc = [&](int id, const wchar_t* cls, DWORD style, HFONT f) {
                HWND c = CreateWindowExW(0, cls, L"", WS_CHILD | style,
                                         0, 0, 10, 10, hwnd,
                                         (HMENU)(INT_PTR)id, g.inst, nullptr);
                setFont(c, f);
            };
            nc(IDC_NC_TITLE, L"STATIC", SS_LEFT,                  g.fontBig);
            nc(IDC_NC_BODY,  L"STATIC", SS_LEFT | SS_EDITCONTROL, g.font);
            nc(IDC_NC_FIX,   L"BUTTON", BS_DEFPUSHBUTTON,         g.font);
            nc(IDC_NC_RETRY, L"BUTTON", BS_PUSHBUTTON,            g.font);
            return 0;
        }

        case WM_SIZE:
            layout();
            return 0;

        case WM_ACTIVATE:
            // Picks up a change made in Task Manager's Startup tab meanwhile.
            if (LOWORD(wp) != WA_INACTIVE) syncStartupCheck();
            break;

        case WM_GETMINMAXINFO: {
            auto* mmi = (MINMAXINFO*)lp;
            mmi->ptMinTrackSize.x = S(900);
            mmi->ptMinTrackSize.y = S(560);
            return 0;
        }

        case WM_DPICHANGED: {
            // Dragging between the Retina panel and an external display.
            g.dpi = HIWORD(wp);
            rebuildFonts();
            setFont(g.list, g.font);
            setFont(g.status, g.font);
            setFont(g.startup, g.font);
            for (size_t i = 0; i < g.fanCount; ++i) {
                for (int c = F_GROUP; c <= F_SRC_COMBO; ++c)
                    if (HWND h = GetDlgItem(hwnd, fanId(i, (FanCtl)c)))
                        setFont(h, c == F_RPM ? g.fontBig
                                  : c == F_NAME ? g.fontBold : g.font);
            }
            // The notice controls exist from startup, connected or not, and
            // would otherwise keep the fonts rebuildFonts() just deleted.
            setFont(GetDlgItem(hwnd, IDC_NC_TITLE), g.fontBig);
            for (int id : {IDC_NC_BODY, IDC_NC_FIX, IDC_NC_RETRY})
                setFont(GetDlgItem(hwnd, id), g.font);
            ListView_SetColumnWidth(g.list, 0, S(210));
            ListView_SetColumnWidth(g.list, 1, S(60));
            ListView_SetColumnWidth(g.list, 2, S(90));
            const RECT* sug = (const RECT*)lp;
            SetWindowPos(hwnd, nullptr, sug->left, sug->top,
                         sug->right - sug->left, sug->bottom - sug->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            layout();
            return 0;
        }

        case WM_TIMER:
            if (wp == IDT_POLL) {
                if (!g.inTray) trayAdd();
                refresh();
            }
            if (wp == IDT_RETRY_LABEL) {
                KillTimer(hwnd, IDT_RETRY_LABEL);
                SetDlgItemTextW(hwnd, IDC_NC_RETRY, L"Try again now");
            }
            return 0;

        case WM_NOTIFY: {
            auto* nh = (NMHDR*)lp;
            if (nh->idFrom == IDC_LIST && nh->code == LVN_GETDISPINFOW) {
                auto* di = (NMLVDISPINFOW*)lp;
                static wchar_t buf[256];
                const int row = di->item.iItem;
                if (row < 0 || row >= (int)g.snap.sensors.size()) return 0;
                const SensorReading& s = g.snap.sensors[row];
                if (di->item.mask & LVIF_TEXT) {
                    switch (di->item.iSubItem) {
                        case 0: wcsncpy_s(buf, s.name.c_str(), _TRUNCATE); break;
                        case 1: wcsncpy_s(buf, widen(s.key.str()).c_str(), _TRUNCATE); break;
                        default:
                            swprintf(buf, 256, L"%.1f %ls", s.value,
                                     sensors::categoryUnit(s.cat));
                            break;
                    }
                    di->item.pszText = buf;
                }
                return 0;
            }
            return 0;
        }

        case WM_HSCROLL:
            if (lp) onSlider((HWND)lp, LOWORD(wp));
            return 0;

        case WM_COMMAND: {
            const int id = LOWORD(wp);
            const WORD notify = HIWORD(wp);
            if (id == IDM_SHOW) {
                ShowWindow(hwnd, IsWindowVisible(hwnd) ? SW_HIDE : SW_SHOW);
                if (IsWindowVisible(hwnd)) SetForegroundWindow(hwnd);
                return 0;
            }
            if (id == IDM_ALLAUTO) { allSystem(); return 0; }
            if (id == IDM_STARTUP) { setRunOnStartup(!startup::isEnabled()); return 0; }
            if (id == IDC_STARTUP) {
                if (notify == BN_CLICKED)
                    setRunOnStartup(Button_GetCheck(g.startup) == BST_CHECKED);
                return 0;
            }
            if (id == IDC_NC_FIX)   { onFixClicked(); return 0; }
            if (id == IDC_NC_RETRY) {
                SetDlgItemTextW(hwnd, IDC_NC_RETRY, L"Checking…");
                if (g.ctl) g.ctl->retryNow();
                // If the attempt fails for the same reason the notice doesn't
                // change, so nothing else would put the label back.
                SetTimer(hwnd, IDT_RETRY_LABEL, 2000, nullptr);
                return 0;
            }
            if (id == IDM_EXIT)    { g.quitting = true; DestroyWindow(hwnd); return 0; }

            size_t fan; FanCtl c;
            if (fanFromId(id, &fan, &c) && fan < g.fanCount)
                onFanCommand(fan, c, notify, (HWND)lp);
            return 0;
        }

        case WM_TRAY:
            if (LOWORD(lp) == WM_LBUTTONDBLCLK) {
                ShowWindow(hwnd, SW_SHOW);
                SetForegroundWindow(hwnd);
            } else if (LOWORD(lp) == WM_RBUTTONUP) {
                showTrayMenu();
            }
            return 0;

        case WM_POWERBROADCAST:
            // The SMC drops manual fan control across a sleep transition, so
            // re-assert everything once the system is back.
            if (wp == PBT_APMRESUMEAUTOMATIC || wp == PBT_APMRESUMESUSPEND) {
                if (g.ctl) g.ctl->reapplyAfterResume();
            }
            return TRUE;

        case WM_QUERYENDSESSION:
            return TRUE;

        case WM_ENDSESSION:
            // Log off / shutdown: give the fans back before the process dies.
            if (wp && g.ctl) g.ctl->stop();
            return 0;

        case WM_CLOSE:
            if (!g.quitting) { ShowWindow(hwnd, SW_HIDE); return 0; }  // minimise to tray
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            KillTimer(hwnd, IDT_POLL);
            trayRemove();
            if (g.ctl) g.ctl->stop();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LONG WINAPI crashFilter(EXCEPTION_POINTERS*) {
    // Never leave the fans pinned because we crashed.
    Controller::emergencyRestore();
    return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace

// ---- entry point ----------------------------------------------------------

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int) {
    g.inst = inst;
    g.quietStart = launchedWith(startup::kSwitch);   // run by the startup entry

    // The applesmc driver hands out one device handle at a time, so a second
    // copy of the app could never connect. Surface that clearly rather than
    // letting it fail at CreateFile.
    HANDLE once = CreateMutexW(nullptr, TRUE, L"Local\\MacFanCtlSingleInstance");
    const DWORD onceErr = GetLastError();
    g.instanceMutex = once;   // released early if we relaunch with admin rights
    // ERROR_ACCESS_DENIED: a copy restarted as administrator by "Fix this for
    // me" owns the mutex and we may not open it. That copy is still running.
    if ((once && onceErr == ERROR_ALREADY_EXISTS) || (!once && onceErr == ERROR_ACCESS_DENIED)) {
        // Someone opening the app wants to see it; the startup entry doesn't.
        if (!g.quietStart) {
            if (HWND prev = FindWindowW(L"MacFanCtlMain", nullptr)) {
                ShowWindow(prev, SW_SHOW);
                SetForegroundWindow(prev);
            }
        }
        return 0;
    }

    // If Run on startup is on but was set up from a copy that has since been
    // moved or replaced by a newer version, sign-in should run this one.
    startup::pointAtThisCopy();

    INITCOMMONCONTROLSEX icc{sizeof icc, ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES |
                                         ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    SetUnhandledExceptionFilter(crashFilter);

    g.dpi = systemDpi();
    rebuildFonts();

    ui::registerCurveClass(inst);
    ui::registerKofiClass(inst);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = mainProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.lpszClassName = L"MacFanCtlMain";
    RegisterClassExW(&wc);

    g.taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"MacFanCtl — Mac fan & temperature control",
                                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                S(1080), S(780), nullptr, nullptr, inst, nullptr);
    if (!hwnd) return 1;

    // Explorer runs unelevated; without this, a copy restarted as administrator
    // by "Fix this for me" would never hear that the taskbar came back.
    if (g.taskbarCreated) ChangeWindowMessageFilterEx(hwnd, g.taskbarCreated, MSGFLT_ALLOW, nullptr);

    Controller ctl;
    g.ctl = &ctl;

    // Never a dead end: if the SMC is unreachable the window still opens,
    // explains why, and the controller keeps retrying in the background.
    ctl.start();

    // Hand over saved settings now; if we are not connected yet the controller
    // holds them and applies them once the fans are known.
    if (auto saved = loadConfig(); !saved.empty()) ctl.applyConfigs(saved);

    layout();
    trayAdd();
    // Launched at sign-in: stay in the tray. refresh() opens the window after
    // kQuietStartGraceMs if the fans still can't be reached.
    g.startedAt = GetTickCount64();
    if (!g.quietStart) ShowWindow(hwnd, SW_SHOW);
    SetTimer(hwnd, IDT_POLL, 1000, nullptr);
    refresh();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    g.ctl = nullptr;
    ctl.stop();
    return 0;
}
