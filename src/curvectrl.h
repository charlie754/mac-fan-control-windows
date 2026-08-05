// curvectrl.h - a small custom control that draws a fan curve and lets the
// user drag, add and remove its points.

#pragma once

#include <windows.h>
#include "curve.h"

namespace ui {

// Sent to the parent as WM_COMMAND with this notification code in HIWORD
// whenever the user finishes editing (mouse up / add / delete).
constexpr WORD CURVE_CHANGED = 0x7A01;

extern const wchar_t* kCurveClass;

void registerCurveClass(HINSTANCE hInst);

// Replaces the control's curve and axis limits. Ignored while the user is
// mid-drag so the poll timer cannot yank the point out from under the mouse.
void curveSet(HWND hwnd, const curve::Curve& c, double minRpm, double maxRpm);

// Marks where the fan is actually operating right now, for the live dot.
void curveSetOperatingPoint(HWND hwnd, double tempC, double rpm, bool valid);

curve::Curve curveGet(HWND hwnd);

} // namespace ui
