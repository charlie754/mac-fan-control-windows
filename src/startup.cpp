#include "startup.h"

#include <windows.h>
#include <cwchar>
#include <string>

namespace app::startup {

namespace {

constexpr const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

// Task Manager and Settings > Apps > Startup don't delete a Run value when
// the user switches it off; they record the choice here instead. REG_BINARY,
// first byte odd (03) when disabled, even (02) when enabled; no value at all
// also means enabled.
constexpr const wchar_t* kApprovedKey =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";

constexpr const wchar_t* kValue = L"MacFanCtl";

std::wstring exePath() {
    std::wstring p(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, p.data(), (DWORD)p.size());
        if (n == 0) return {};
        if (n < p.size()) { p.resize(n); return p; }
        p.resize(p.size() * 2);   // truncated: a long path
    }
}

// What Windows runs at sign-in. Quoted, since the exe often lives in a folder
// whose name has spaces.
std::wstring command() {
    const std::wstring exe = exePath();
    return exe.empty() ? std::wstring() : L"\"" + exe + L"\" " + kSwitch;
}

bool readCommand(std::wstring* out) {
    DWORD bytes = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, kRunKey, kValue, RRF_RT_REG_SZ,
                     nullptr, nullptr, &bytes) != ERROR_SUCCESS)
        return false;
    std::wstring s(bytes / sizeof(wchar_t) + 1, L'\0');
    bytes = (DWORD)(s.size() * sizeof(wchar_t));
    if (RegGetValueW(HKEY_CURRENT_USER, kRunKey, kValue, RRF_RT_REG_SZ,
                     nullptr, s.data(), &bytes) != ERROR_SUCCESS)
        return false;
    s.resize(wcslen(s.c_str()));
    *out = s;
    return true;
}

bool writeCommand(const std::wstring& cmd) {
    return RegSetKeyValueW(HKEY_CURRENT_USER, kRunKey, kValue, REG_SZ, cmd.c_str(),
                           (DWORD)((cmd.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}

bool disabledInTaskManager() {
    BYTE b[64] = {};
    DWORD bytes = sizeof b;
    if (RegGetValueW(HKEY_CURRENT_USER, kApprovedKey, kValue, RRF_RT_REG_BINARY,
                     nullptr, b, &bytes) != ERROR_SUCCESS)
        return false;
    return bytes > 0 && (b[0] & 1);
}

void clearTaskManagerMark() {
    RegDeleteKeyValueW(HKEY_CURRENT_USER, kApprovedKey, kValue);
}

} // namespace

bool isEnabled() {
    std::wstring cmd;
    return readCommand(&cmd) && !cmd.empty() && !disabledInTaskManager();
}

bool setEnabled(bool on) {
    if (on) {
        const std::wstring cmd = command();
        if (cmd.empty() || !writeCommand(cmd)) return false;
        clearTaskManagerMark();
        return true;
    }
    const LSTATUS r = RegDeleteKeyValueW(HKEY_CURRENT_USER, kRunKey, kValue);
    clearTaskManagerMark();   // don't leave an orphaned record behind
    return r == ERROR_SUCCESS || r == ERROR_FILE_NOT_FOUND;
}

void pointAtThisCopy() {
    std::wstring cur;
    if (!readCommand(&cur)) return;   // not registered: leave it that way
    const std::wstring want = command();
    // Rewriting keeps any Task Manager "disabled" mark: that is keyed by the
    // value name, and switching the entry off was the user's call.
    if (!want.empty() && _wcsicmp(cur.c_str(), want.c_str()) != 0) writeCommand(want);
}

} // namespace app::startup
