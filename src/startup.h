// startup.h - "Run on startup": a per-user entry under
// HKCU\Software\Microsoft\Windows\CurrentVersion\Run, the same list Task
// Manager's Startup tab shows and can switch off. No elevation needed, which
// works because the app itself runs asInvoker.

#pragma once

namespace app::startup {

// Appended to the command the entry runs, so a launch at sign-in comes up in
// the tray instead of opening the window.
constexpr const wchar_t* kSwitch = L"--startup";

// True when the entry exists and has not been switched off in Task Manager.
bool isEnabled();

// Adds the entry for this copy of the exe, or removes it. Enabling also clears
// a "disabled" mark left by Task Manager, which would otherwise stop Windows
// running an entry the checkbox shows as on.
bool setEnabled(bool on);

// MacFanCtl is a portable exe that people move, and update by unzipping a new
// version somewhere else. If the entry exists but runs a different path, point
// it at this copy. Never creates an entry.
void pointAtThisCopy();

} // namespace app::startup
