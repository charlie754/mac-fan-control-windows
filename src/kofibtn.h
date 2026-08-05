// kofibtn.h - Custom Ko-fi support button control for Win32.

#pragma once

#include <windows.h>

namespace ui {

extern const wchar_t* kKofiClass;

void registerKofiClass(HINSTANCE hInst);

} // namespace ui
