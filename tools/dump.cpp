// dump.cpp - enumerate every SMC key on this machine with type + decoded value.
// Read-only. Build: g++ -std=c++17 -O2 -o dump.exe dump.cpp

#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

#define IOCTL_SMC_READ_KEY       0x220000u
#define IOCTL_SMC_GET_KEY_BY_IDX 0x220008u
#define IOCTL_SMC_GET_KEY_INFO   0x22000Cu
#define IOCTL_SMC_GET_PROTOCOL   0x220020u

#pragma pack(push, 1)
struct ReadReq { uint32_t key; uint8_t size; };
struct KeyInfo { uint8_t dataSize; uint32_t dataType; uint8_t dataAttributes; };
#pragma pack(pop)

static HANDLE g_h = INVALID_HANDLE_VALUE;

static uint32_t mk(const char* s) { uint32_t v; memcpy(&v, s, 4); return v; }
static void un(uint32_t v, char o[5]) {
    memcpy(o, &v, 4); o[4] = 0;
    for (int i = 0; i < 4; i++) if ((unsigned char)o[i] < 32 || (unsigned char)o[i] > 126) o[i] = '.';
}

static bool keyByIndex(uint32_t i, uint32_t* key) {
    DWORD n = 0;
    return DeviceIoControl(g_h, IOCTL_SMC_GET_KEY_BY_IDX, &i, 4, key, 4, &n, nullptr) && n == 4;
}
static bool keyInfo(uint32_t key, KeyInfo* ki) {
    uint8_t b[8] = {0}; DWORD n = 0;
    if (!DeviceIoControl(g_h, IOCTL_SMC_GET_KEY_INFO, &key, 4, b, 6, &n, nullptr) || n != 6) return false;
    memcpy(ki, b, 6); return true;
}
static bool readKey(uint32_t key, uint8_t size, uint8_t* out, DWORD* n) {
    ReadReq r{key, size}; uint8_t b[32] = {0}; DWORD ret = 0;
    if (!DeviceIoControl(g_h, IOCTL_SMC_READ_KEY, &r, 5, b, 32, &ret, nullptr)) return false;
    memcpy(out, b, ret > 32 ? 32 : ret); *n = ret; return true;
}

static uint64_t beUInt(const uint8_t* p, int n) {
    uint64_t v = 0; for (int i = 0; i < n; i++) v = (v << 8) | p[i]; return v;
}
static int64_t beSInt(const uint8_t* p, int n) {
    int64_t v = beUInt(p, n);
    int64_t sign = (int64_t)1 << (n * 8 - 1);
    if (v & sign) v -= (sign << 1);
    return v;
}
static int hexv(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Decode a value given its 4CC type string. Returns "" if not numeric.
static std::string decode(const char* t, const uint8_t* d, int n) {
    char buf[128];
    std::string ty(t);
    if (ty == "ui8 " || ty == "ui16" || ty == "ui32" || ty == "ui64") {
        snprintf(buf, sizeof buf, "%llu", (unsigned long long)beUInt(d, n)); return buf;
    }
    if (ty == "si8 " || ty == "si16" || ty == "si32") {
        snprintf(buf, sizeof buf, "%lld", (long long)beSInt(d, n)); return buf;
    }
    if (ty == "flag") { snprintf(buf, sizeof buf, "%s", d[0] ? "true" : "false"); return buf; }
    if (ty == "flt " && n == 4) {
        // little-endian IEEE754 (x86 native) - verified against big-endian below
        float le, be; uint8_t r[4] = {d[3], d[2], d[1], d[0]};
        memcpy(&le, d, 4); memcpy(&be, r, 4);
        snprintf(buf, sizeof buf, "LE=%.4f BE=%.4f", le, be); return buf;
    }
    if ((ty[0] == 's' || ty[0] == 'f') && ty[1] == 'p' && n == 2) {
        int frac = hexv(ty[3]); if (frac < 0) return "";
        double scale = (double)(1u << frac);
        double v = (ty[0] == 's') ? (double)beSInt(d, 2) / scale : (double)beUInt(d, 2) / scale;
        snprintf(buf, sizeof buf, "%.4f", v); return buf;
    }
    if (ty.rfind("ch8", 0) == 0) {
        std::string s = "\"";
        for (int i = 0; i < n; i++) s += (d[i] >= 32 && d[i] < 127) ? (char)d[i] : '.';
        return s + "\"";
    }
    return "";
}

int main(int argc, char** argv) {
    const char* outPath = argc > 1 ? argv[1] : "smc_dump.txt";

    g_h = CreateFileW(L"\\\\.\\APPLESMC", GENERIC_READ | GENERIC_WRITE,
                      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (g_h == INVALID_HANDLE_VALUE) { printf("open failed %lu\n", GetLastError()); return 1; }

    uint8_t v[32]; DWORD n = 0;
    uint32_t total = 0;
    if (readKey(mk("#KEY"), 4, v, &n) && n == 4) total = (uint32_t)beUInt(v, 4);
    printf("#KEY = %u\n", total);

    FILE* f = fopen(outPath, "w");
    fprintf(f, "idx\tkey\ttype\tsize\tattr\traw\tdecoded\n");

    int ok = 0, fail = 0;
    for (uint32_t i = 0; i < total; i++) {
        uint32_t key;
        if (!keyByIndex(i, &key)) { fail++; continue; }
        char ks[5]; un(key, ks);
        KeyInfo ki{};
        if (!keyInfo(key, &ki)) { fprintf(f, "%u\t%s\t?\t?\t?\t\t\n", i, ks); fail++; continue; }
        char ts[5]; un(ki.dataType, ts);

        std::string raw, dec;
        if (ki.dataSize > 0 && ki.dataSize <= 32 && readKey(key, ki.dataSize, v, &n)) {
            char hb[8];
            for (DWORD j = 0; j < n; j++) { snprintf(hb, sizeof hb, "%02X", v[j]); raw += hb; }
            dec = decode(ts, v, (int)n);
        }
        fprintf(f, "%u\t%s\t%s\t%u\t0x%02X\t%s\t%s\n",
                i, ks, ts, ki.dataSize, ki.dataAttributes, raw.c_str(), dec.c_str());
        ok++;
    }
    fclose(f);
    printf("dumped %d keys (%d failed) -> %s\n", ok, fail, outPath);
    CloseHandle(g_h);
    return 0;
}
