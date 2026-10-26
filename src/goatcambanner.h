// goatcambanner.h - GOAT Cam promotional banner, a self-contained custom control.
//
// Two plain copy lines above a dark plate. The plate is the only live part: hover,
// keyboard focus, Enter/Space or a click on it opens https://goatcam.dagoat.io.
// The QR code on the plate is the App Store route and is scanned, not clicked.

#pragma once

#include <windows.h>

namespace ui {

extern const wchar_t* kGoatCamClass;

// Registers the window class and starts GDI+ (once per process).
void registerGoatCamClass(HINSTANCE hInst);

// The control's natural size at `dpi`, so the host's layout never has to guess.
SIZE goatCamPreferredSize(int dpi);

} // namespace ui
