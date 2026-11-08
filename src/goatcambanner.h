// goatcambanner.h - the built-in DaGoat / online GOAT Cam banner control.
//
// The control draws one of two models: a signed GOAT Cam photo banner or the
// offline DaGoat project lockup.  Animation timers exist only while a visual
// transition is running; the resting control has no animation timer.

#pragma once

#include <windows.h>

namespace ob {
struct Config;
}

namespace ui {

extern const wchar_t *kGoatCamClass;

// Registers the window class and starts GDI+ (once per process).
void registerGoatCamClass(HINSTANCE hInst);

// The control's natural size at `dpi`, so the host's layout never has to guess.
SIZE goatCamPreferredSize(int dpi);

// The host owns visibility; keeping this explicit lets the online client stop
// all work while the main window is hidden or minimized.
void goatCamVisible(HWND hwnd, bool visible);
void goatCamResumed(HWND hwnd);

// Test seams. They are intentionally small and do not expose the drawing state.
enum class GoatCamTestModelKind { DaGoat = 0, Online = 1 };

struct GoatCamTestTextProbe {
  RECT box{};
  RECT extent{};
  int role = 0;
  int requestedSize = 0;
  int actualSize = 0;
  bool ellipsized = false;
};

struct GoatCamTestLayoutProbe {
  SIZE control{};
  RECT plate{};
  RECT bracket{};
  RECT textColumn{};
  RECT badge{};
  RECT iphone{};
  RECT qr{};
  RECT headline{};
  GoatCamTestTextProbe text[8]{};
  int textCount = 0;
  GoatCamTestModelKind kind = GoatCamTestModelKind::DaGoat;
  double scale = 1.0;
};

void goatCamTestConfig(const ob::Config *config);
void goatCamTestSetOpenUrl(void (*openUrl)(const wchar_t *));
bool goatCamTestIsOnline(HWND hwnd);
bool goatCamTestProbeLayout(HWND hwnd, int dpi, GoatCamTestModelKind kind,
                            bool active, GoatCamTestLayoutProbe *probe);
void goatCamTestSetActive(HWND hwnd, bool active, bool settle);
void goatCamTestSetDpi(HWND hwnd, int dpi);
void goatCamTestForceCheck(HWND hwnd);

} // namespace ui
