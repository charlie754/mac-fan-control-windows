// probe.cpp - empirical validation of the \\.\APPLESMC IOCTL protocol.
// Build: g++ -std=c++17 -O2 -o probe.exe probe.cpp
// Run elevated.

#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

static HANDLE g_h = INVALID_HANDLE_VALUE;

// SMC device IOCTLs.
#define IOCTL_SMC_READ_KEY      0x220000u
#define IOCTL_SMC_WRITE_KEY     0x220004u
#define IOCTL_SMC_GET_KEY_INFO  0x22000Cu
#define IOCTL_SMC_GET_PROTOCOL  0x220020u

#pragma pack(push, 1)
struct ReadReq  { uint32_t key; uint8_t size; };
struct WriteReq { uint32_t key; uint8_t size; uint8_t data[32]; };
struct KeyInfo  { uint8_t dataSize; uint32_t dataType; uint8_t dataAttributes; };
#pragma pack(pop)

static_assert(sizeof(KeyInfo) == 6, "KeyInfo must be 6 bytes");

// Two candidate encodings of a 4-char key into the uint32 field.
static uint32_t keyAsWritten(const char* s) { // memory order 'F','0','A','c'
    uint32_t v; memcpy(&v, s, 4); return v;
}
static uint32_t keyBigEndian(const char* s) { // 0x46304163
    return ((uint32_t)(uint8_t)s[0] << 24) | ((uint32_t)(uint8_t)s[1] << 16) |
           ((uint32_t)(uint8_t)s[2] << 8)  |  (uint32_t)(uint8_t)s[3];
}

static void fourccToStr(uint32_t v, char out[5], bool asWritten) {
    if (asWritten) { memcpy(out, &v, 4); }
    else { out[0]=(char)(v>>24); out[1]=(char)(v>>16); out[2]=(char)(v>>8); out[3]=(char)v; }
    out[4] = 0;
    for (int i = 0; i < 4; i++) if ((unsigned char)out[i] < 32 || (unsigned char)out[i] > 126) out[i] = '.';
}

static bool getKeyInfo(uint32_t key, KeyInfo* out) {
    uint8_t buf[16] = {0};
    DWORD ret = 0;
    if (!DeviceIoControl(g_h, IOCTL_SMC_GET_KEY_INFO, &key, 4, buf, 6, &ret, nullptr)) return false;
    if (ret != 6) { printf("    [getKeyInfo returned %lu bytes, expected 6]\n", ret); return false; }
    memcpy(out, buf, 6);
    return true;
}

static bool readKey(uint32_t key, uint8_t size, uint8_t* out, DWORD* outLen) {
    ReadReq req{key, size};
    uint8_t buf[32] = {0};
    DWORD ret = 0;
    if (!DeviceIoControl(g_h, IOCTL_SMC_READ_KEY, &req, 5, buf, 32, &ret, nullptr)) return false;
    memcpy(out, buf, ret > 32 ? 32 : ret);
    *outLen = ret;
    return true;
}

static void hexdump(const uint8_t* p, DWORD n) {
    for (DWORD i = 0; i < n; i++) printf("%02X ", p[i]);
}

// ---- probing the two key encodings -----------------------------------------

static bool tryEncoding(bool asWritten, const char* label) {
    const char* probes[] = {"#KEY", "FNum", "TC0P"};
    printf("\n== Encoding: %s ==\n", label);
    int hits = 0;
    for (const char* k : probes) {
        uint32_t key = asWritten ? keyAsWritten(k) : keyBigEndian(k);
        KeyInfo ki{};
        printf("  %s (0x%08X): ", k, key);
        if (!getKeyInfo(key, &ki)) { printf("getKeyInfo FAILED (err %lu)\n", GetLastError()); continue; }
        char t[5]; fourccToStr(ki.dataType, t, asWritten);
        char t2[5]; fourccToStr(ki.dataType, t2, !asWritten);
        printf("size=%u type=0x%08X ('%s' | rev '%s') attr=0x%02X",
               ki.dataSize, ki.dataType, t, t2, ki.dataAttributes);
        uint8_t val[32]; DWORD n = 0;
        if (ki.dataSize && ki.dataSize <= 32 && readKey(key, ki.dataSize, val, &n)) {
            printf("  data[%lu]= ", n); hexdump(val, n);
        } else {
            printf("  (read failed err %lu)", GetLastError());
        }
        printf("\n");
        hits++;
    }
    return hits > 0;
}

int main() {
    printf("=== APPLESMC protocol probe ===\n");

    g_h = CreateFileW(L"\\\\.\\APPLESMC", GENERIC_READ | GENERIC_WRITE,
                      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (g_h == INVALID_HANDLE_VALUE) {
        printf("CreateFileW(\\\\.\\APPLESMC) failed, error %lu\n", GetLastError());
        printf("(error 5 = access denied -> run elevated; 2 = driver not present)\n");
        return 1;
    }
    printf("Device opened OK.\n");

    // GetProtocol
    uint8_t proto = 0xFF; DWORD ret = 0;
    if (DeviceIoControl(g_h, IOCTL_SMC_GET_PROTOCOL, nullptr, 0, &proto, 1, &ret, nullptr))
        printf("GetProtocol: 0x%02X (%s), %lu bytes\n", proto, proto == 1 ? "mmio" : "pmio", ret);
    else
        printf("GetProtocol FAILED, error %lu\n", GetLastError());

    bool a = tryEncoding(true,  "key bytes as written in memory (LE uint32)");
    bool b = tryEncoding(false, "key big-endian uint32");
    (void)a; (void)b;

    // Probe candidate getKeyByIndex IOCTLs with index 0.
    printf("\n== getKeyByIndex IOCTL candidates (index 0) ==\n");
    for (uint32_t code : {0x220008u, 0x220010u, 0x220014u, 0x220018u, 0x22001Cu, 0x220024u}) {
        uint32_t idx = 0; uint8_t out[32] = {0}; DWORD n = 0;
        if (DeviceIoControl(g_h, code, &idx, 4, out, 32, &n, nullptr)) {
            printf("  0x%06X: OK, %lu bytes: ", code, n); hexdump(out, n);
            char s[5]; memcpy(s, out, 4); s[4]=0;
            for (int i=0;i<4;i++) if ((unsigned char)s[i]<32||(unsigned char)s[i]>126) s[i]='.';
            printf(" ascii='%s'\n", s);
        } else {
            printf("  0x%06X: fail (err %lu)\n", code, GetLastError());
        }
    }

    CloseHandle(g_h);
    return 0;
}
