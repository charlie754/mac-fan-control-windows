#include "smc.h"

#include <cstdio>
#include <cmath>
#include <algorithm>

#pragma comment(lib, "advapi32.lib")

namespace smc {

// ---- IOCTL codes ----------------------------------------------------------

#define IOCTL_SMC_READ_KEY        0x220000u
#define IOCTL_SMC_WRITE_KEY       0x220004u
#define IOCTL_SMC_GET_KEY_BY_IDX  0x220008u
#define IOCTL_SMC_GET_KEY_INFO    0x22000Cu
#define IOCTL_SMC_GET_PROTOCOL    0x220020u

#pragma pack(push, 1)
struct ReadReq  { uint32_t key; uint8_t size; };
struct WriteReq { uint32_t key; uint8_t size; uint8_t data[32]; };
#pragma pack(pop)

// ---- endianness -----------------------------------------------------------

uint64_t beRead(const uint8_t* p, int n) {
    uint64_t v = 0;
    for (int i = 0; i < n; ++i) v = (v << 8) | p[i];
    return v;
}

void beWrite(uint8_t* p, int n, uint64_t v) {
    for (int i = n - 1; i >= 0; --i) { p[i] = (uint8_t)(v & 0xFF); v >>= 8; }
}

static int64_t beReadSigned(const uint8_t* p, int n) {
    int64_t v = (int64_t)beRead(p, n);
    const int64_t sign = (int64_t)1 << (n * 8 - 1);
    if (v & sign) v -= (sign << 1);
    return v;
}

static int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// ---- value decoding -------------------------------------------------------
//
// Fixed-point types are named <s|f>p<int-bits><frac-bits> in hex, always 16
// bits wide:  sp78 = sign + 7 int + 8 frac -> /256 signed
//             fpe2 = 14 int + 2 frac       -> /4   unsigned
// So the fraction shift is simply the hex value of the 4th character.

std::optional<double> Value::toDouble() const {
    const std::string t = type.str();
    const int n = (int)size;
    if (n == 0) return std::nullopt;

    if (t == "ui8" || t == "ui16" || t == "ui32" || t == "ui64")
        return (double)beRead(data, std::min(n, 8));
    if (t == "si8" || t == "si16" || t == "si32")
        return (double)beReadSigned(data, std::min(n, 8));
    if (t == "flag")
        return data[0] ? 1.0 : 0.0;
    if (t == "flt" && n == 4) {
        // Verified on hardware: 'flt' is native little-endian, unlike every
        // other type. (B0AP raw 259F9840 -> 4.77 W as LE, ~0 as BE.)
        float f;
        memcpy(&f, data, 4);
        return (double)f;
    }
    if (n == 2 && (t.size() == 4) && (t[0] == 's' || t[0] == 'f') && t[1] == 'p') {
        const int frac = hexDigit(t[3]);
        if (frac < 0 || frac > 16) return std::nullopt;
        const double scale = (double)(1u << frac);
        return (t[0] == 's') ? (double)beReadSigned(data, 2) / scale
                             : (double)beRead(data, 2) / scale;
    }
    return std::nullopt;
}

std::string Value::toString() const {
    if (auto d = toDouble()) {
        char b[64];
        snprintf(b, sizeof b, "%.4g", *d);
        return b;
    }
    const std::string t = type.str();
    if (t.rfind("ch8", 0) == 0) {
        std::string s;
        for (uint32_t i = 0; i < size; ++i)
            s += (data[i] >= 32 && data[i] < 127) ? (char)data[i] : '.';
        return s;
    }
    std::string hex;
    char b[8];
    for (uint32_t i = 0; i < size; ++i) { snprintf(b, sizeof b, "%02X", data[i]); hex += b; }
    return hex;
}

uint32_t encode(Key type, double v, uint8_t* out, uint32_t outCap) {
    const std::string t = type.str();

    auto putU = [&](int n, double val) -> uint32_t {
        if ((uint32_t)n > outCap) return 0;
        if (val < 0) val = 0;
        const double maxv = std::pow(256.0, n) - 1.0;
        if (val > maxv) val = maxv;
        beWrite(out, n, (uint64_t)llround(val));
        return n;
    };
    auto putS = [&](int n, double val) -> uint32_t {
        if ((uint32_t)n > outCap) return 0;
        const double lim = std::pow(2.0, n * 8 - 1);
        val = std::max(-lim, std::min(lim - 1.0, val));
        int64_t iv = llround(val);
        beWrite(out, n, (uint64_t)iv);
        return n;
    };

    if (t == "ui8")  return putU(1, v);
    if (t == "ui16") return putU(2, v);
    if (t == "ui32") return putU(4, v);
    if (t == "si8")  return putS(1, v);
    if (t == "si16") return putS(2, v);
    if (t == "si32") return putS(4, v);
    if (t == "flag") { if (outCap < 1) return 0; out[0] = v != 0.0 ? 1 : 0; return 1; }
    if (t == "flt") {
        if (outCap < 4) return 0;
        float f = (float)v;
        memcpy(out, &f, 4);   // little-endian, matching the decode path
        return 4;
    }
    if (t.size() == 4 && (t[0] == 's' || t[0] == 'f') && t[1] == 'p') {
        const int frac = hexDigit(t[3]);
        if (frac < 0 || frac > 16) return 0;
        const double scaled = v * (double)(1u << frac);
        return (t[0] == 's') ? putS(2, scaled) : putU(2, scaled);
    }
    return 0;
}

// ---- device ---------------------------------------------------------------

static std::wstring lastErrorText(const wchar_t* what, DWORD e) {
    wchar_t buf[512];
    swprintf(buf, 512, L"%ls failed (Windows error %lu)", what, e);
    return buf;
}

ServiceState queryAppleSmcService() {
    ServiceState s;
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return s;
    if (SC_HANDLE svc = OpenServiceW(scm, L"AppleSMC", SERVICE_QUERY_STATUS)) {
        s.registered = true;
        SERVICE_STATUS st{};
        if (QueryServiceStatus(svc, &st)) s.running = (st.dwCurrentState == SERVICE_RUNNING);
        CloseServiceHandle(svc);
    }
    CloseServiceHandle(scm);
    return s;
}

bool Device::ensureServiceRunning() {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return false;
    bool ok = false;
    if (SC_HANDLE svc = OpenServiceW(scm, L"AppleSMC", SERVICE_QUERY_STATUS | SERVICE_START)) {
        SERVICE_STATUS st{};
        if (QueryServiceStatus(svc, &st)) {
            if (st.dwCurrentState == SERVICE_RUNNING) {
                ok = true;
            } else if (StartServiceW(svc, 0, nullptr) || GetLastError() == ERROR_SERVICE_ALREADY_RUNNING) {
                // Poll until running, mirroring the 500 ms cadence the stock app uses.
                for (int i = 0; i < 20 && !ok; ++i) {
                    Sleep(100);
                    if (QueryServiceStatus(svc, &st) && st.dwCurrentState == SERVICE_RUNNING) ok = true;
                }
            }
        }
        CloseServiceHandle(svc);
    }
    CloseServiceHandle(scm);
    return ok;
}

bool Device::open() {
    close();
    err_.clear();

    for (int attempt = 0; attempt < 2; ++attempt) {
        h_ = CreateFileW(L"\\\\.\\APPLESMC",
                         GENERIC_READ | GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                         nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h_ != INVALID_HANDLE_VALUE) break;

        const DWORD e = GetLastError();
        if (attempt == 0 && (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND)) {
            // Driver present but service stopped: try to start it, then retry.
            if (ensureServiceRunning()) continue;
        }
        if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) {
            // Separate "nothing is registered" from "registered but not running":
            // only the latter is something the app can resolve on its own.
            const ServiceState svc = queryAppleSmcService();
            status_ = svc.registered ? Status::ServiceStopped : Status::NoDriver;
            err_ = svc.registered
                 ? L"The SMC service is installed but not running, and MacFanCtl could "
                   L"not start it. Starting it needs administrator rights."
                 : L"No SMC driver is set up on this PC, so MacFanCtl has nothing to talk to.";
        }
        else if (e == ERROR_ACCESS_DENIED || e == ERROR_SHARING_VIOLATION)
            // The driver hands out a single handle at a time, so this almost
            // always means another SMC tool already has the device open.
        {
            status_ = Status::DeviceBusy;
            err_ = L"Another program is already using the SMC. Only one program can "
                   L"use it at a time.";
        }
        else {
            status_ = Status::OtherError;
            err_ = lastErrorText(L"CreateFile(\\\\.\\APPLESMC)", e);
        }
        return false;
    }

    DWORD ret = 0;
    if (!DeviceIoControl(h_, IOCTL_SMC_GET_PROTOCOL, nullptr, 0, &proto_, 1, &ret, nullptr))
        proto_ = 0;
    status_ = Status::Connected;
    return true;
}

void Device::close() {
    if (h_ != INVALID_HANDLE_VALUE) { CloseHandle(h_); h_ = INVALID_HANDLE_VALUE; }
}

bool Device::getKeyInfo(Key k, KeyInfo* out) const {
    if (!isOpen()) return false;
    uint8_t buf[8] = {};
    DWORD ret = 0;
    uint32_t key = k.raw;
    if (!DeviceIoControl(h_, IOCTL_SMC_GET_KEY_INFO, &key, 4, buf, 6, &ret, nullptr)) return false;
    if (ret != 6) return false;   // unknown key: driver returns 0 bytes
    memcpy(out, buf, 6);
    return true;
}

bool Device::readKey(Key k, uint8_t size, Value* out) const {
    if (!isOpen() || size == 0 || size > 32) return false;
    ReadReq req{k.raw, size};
    uint8_t buf[32] = {};
    DWORD ret = 0;
    if (!DeviceIoControl(h_, IOCTL_SMC_READ_KEY, &req, sizeof(ReadReq), buf, 32, &ret, nullptr))
        return false;
    if (ret == 0 || ret > 32) return false;
    memcpy(out->data, buf, ret);
    out->size = ret;
    return true;
}

bool Device::readKey(Key k, Value* out) const {
    KeyInfo ki{};
    if (!getKeyInfo(k, &ki)) return false;
    out->type = Key(ki.dataType);
    return readKey(k, ki.dataSize, out);
}

bool Device::writeKey(Key k, const uint8_t* data, uint8_t size) const {
    if (!isOpen() || size == 0 || size > 32) return false;
    WriteReq req{};
    req.key = k.raw;
    req.size = size;
    memcpy(req.data, data, size);
    uint8_t resp = 0;
    DWORD ret = 0;
    const DWORD inLen = 5u + size;   // {u32 key; u8 size; u8 data[size]}
    if (!DeviceIoControl(h_, IOCTL_SMC_WRITE_KEY, &req, inLen, &resp, 1, &ret, nullptr))
        return false;
    return ret == 1;
}

bool Device::keyByIndex(uint32_t index, Key* out) const {
    if (!isOpen()) return false;
    uint32_t raw = 0;
    DWORD ret = 0;
    if (!DeviceIoControl(h_, IOCTL_SMC_GET_KEY_BY_IDX, &index, 4, &raw, 4, &ret, nullptr))
        return false;
    if (ret != 4) return false;
    out->raw = raw;
    return true;
}

uint32_t Device::keyCount() const {
    Value v;
    if (!readKey(Key("#KEY"), &v)) return 0;
    if (v.size != 4) return 0;
    return (uint32_t)beRead(v.data, 4);
}

std::optional<double> Device::readNumber(Key k) const {
    Value v;
    if (!readKey(k, &v)) return std::nullopt;
    return v.toDouble();
}

bool Device::writeNumber(Key k, double val) const {
    KeyInfo ki{};
    if (!getKeyInfo(k, &ki)) return false;
    if (!(ki.dataAttributes & ATTR_WRITABLE)) return false;
    uint8_t buf[32] = {};
    const uint32_t n = encode(Key(ki.dataType), val, buf, sizeof buf);
    if (n == 0 || n != ki.dataSize) return false;
    return writeKey(k, buf, (uint8_t)n);
}

std::vector<Key> Device::enumerateKeys() const {
    std::vector<Key> keys;
    const uint32_t n = keyCount();
    if (n == 0 || n > 4096) return keys;
    keys.reserve(n);
    for (uint32_t i = 0; i < n; ++i) {
        Key k;
        if (keyByIndex(i, &k)) keys.push_back(k);
    }
    return keys;
}

} // namespace smc
