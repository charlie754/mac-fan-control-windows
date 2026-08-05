// smc.h - Apple SMC transport over the \\.\APPLESMC kernel device.
//
// Verified empirically on MacBookPro14,3.
//
//   Device      : \\.\APPLESMC   (service "AppleSMC", driver applesmc.sys)
//   READ_KEY    : 0x220000  in {u32 key; u8 size}          out <= 32 bytes
//   WRITE_KEY   : 0x220004  in {u32 key; u8 size; u8 d[]}  out 1 byte
//   KEY_BY_INDEX: 0x220008  in u32 index                   out 4 bytes (key)
//   KEY_INFO    : 0x22000C  in u32 key                     out 6 bytes
//   GET_PROTOCOL: 0x220020  in -                           out 1 byte (1=mmio)
//
// Key encoding: the four characters in memory order, reinterpreted as a
// little-endian u32.  'F0Ac' -> bytes 46 30 41 63 -> 0x63413046.
// Value encoding: BIG-endian for all integer and fixed-point types.
//                 'flt' is the exception: native LITTLE-endian IEEE-754.

#pragma once

#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>
#include <optional>

namespace smc {

// ---- key type -------------------------------------------------------------

// A 4-character SMC key, stored in the on-the-wire u32 form.
struct Key {
    uint32_t raw = 0;

    Key() = default;
    explicit Key(uint32_t r) : raw(r) {}
    // Accepts "F0Ac"; shorter names are space-padded, as the SMC itself pads.
    explicit Key(const char* s) {
        char b[4] = {' ', ' ', ' ', ' '};
        for (int i = 0; i < 4 && s[i]; ++i) b[i] = s[i];
        memcpy(&raw, b, 4);
    }

    std::string str() const {
        char b[5];
        memcpy(b, &raw, 4);
        b[4] = 0;
        for (int i = 0; i < 4; ++i)
            if ((unsigned char)b[i] < 32 || (unsigned char)b[i] > 126) b[i] = '.';
        // Trim trailing pad spaces for display ("ui8 " -> "ui8").
        std::string s(b);
        while (!s.empty() && s.back() == ' ') s.pop_back();
        return s;
    }

    bool operator==(const Key& o) const { return raw == o.raw; }
    bool operator<(const Key& o) const { return raw < o.raw; }
};

// ---- key metadata ---------------------------------------------------------

#pragma pack(push, 1)
struct KeyInfo {
    uint8_t  dataSize;        // bytes the key holds
    uint32_t dataType;        // 4CC, same encoding as Key::raw
    uint8_t  dataAttributes;  // 0x80 readable, 0x40 writable, 0x10 function
};
#pragma pack(pop)
static_assert(sizeof(KeyInfo) == 6, "SMC KeyInfo is a packed 6-byte struct");

constexpr uint8_t ATTR_READABLE = 0x80;
constexpr uint8_t ATTR_WRITABLE = 0x40;
constexpr uint8_t ATTR_FUNCTION = 0x10;

// A key's value: raw bytes plus the type needed to interpret them.
struct Value {
    Key      type;
    uint8_t  data[32] = {};
    uint32_t size = 0;

    // Decode to a double using the declared type. Returns nullopt for
    // non-numeric types (hex_, ch8*, struct types like {fds).
    std::optional<double> toDouble() const;
    // Best-effort printable form.
    std::string toString() const;
};

// Encode a double into `out` according to `type`. Returns bytes written, or 0
// if the type is not encodable.
uint32_t encode(Key type, double v, uint8_t* out, uint32_t outCap);

// ---- device ---------------------------------------------------------------

class Device {
public:
    Device() = default;
    ~Device() { close(); }
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    // Opens \\.\APPLESMC, starting the AppleSMC service first if it is
    // installed but stopped. On failure `error()` explains why.
    bool open();
    void close();
    bool isOpen() const { return h_ != INVALID_HANDLE_VALUE; }

    const std::wstring& error() const { return err_; }
    uint8_t protocol() const { return proto_; }        // 1 = mmio, else pmio
    const wchar_t* protocolName() const { return proto_ == 1 ? L"mmio" : L"pmio"; }

    bool getKeyInfo(Key k, KeyInfo* out) const;
    bool readKey(Key k, uint8_t size, Value* out) const;   // size from getKeyInfo
    bool readKey(Key k, Value* out) const;                 // looks up size itself
    bool writeKey(Key k, const uint8_t* data, uint8_t size) const;
    bool keyByIndex(uint32_t index, Key* out) const;
    uint32_t keyCount() const;                             // reads "#KEY"

    // Convenience wrappers.
    std::optional<double> readNumber(Key k) const;
    bool writeNumber(Key k, double v) const;               // uses declared type

    // Enumerate every key the SMC exposes.
    std::vector<Key> enumerateKeys() const;

private:
    // Attempts to start the "AppleSMC" service. Best-effort; needs admin.
    bool ensureServiceRunning();

    HANDLE       h_ = INVALID_HANDLE_VALUE;
    uint8_t      proto_ = 0;
    std::wstring err_;
};

// Byte-order helpers, exposed for tests.
uint64_t beRead(const uint8_t* p, int n);
void     beWrite(uint8_t* p, int n, uint64_t v);

} // namespace smc
