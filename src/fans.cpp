#include "fans.h"

#include <cstdio>
#include <algorithm>

namespace fans {

static smc::Key fanKey(int idx, const char* suffix) {
    char b[5];
    snprintf(b, sizeof b, "F%d%s", idx, suffix);
    return smc::Key(b);
}

double clampRpm(const Fan& f, double rpm) {
    // If the fan never reported usable limits we must not invent any: pin the
    // request to whatever minimum we do have rather than risk commanding a
    // speed the hardware never advertised.
    if (f.maxRpm <= f.minRpm) return f.minRpm;
    return std::max(f.minRpm, std::min(f.maxRpm, rpm));
}

bool Fan::controllable() const { return maxRpm > minRpm && minRpm >= 0; }

// F<N>ID is a 16-byte {fds descriptor; bytes 4..15 are a NUL-padded name.
static std::wstring parseFanName(const smc::Value& v, int idx) {
    std::wstring name;
    if (v.size >= 16) {
        for (uint32_t i = 4; i < 16; ++i) {
            const uint8_t c = v.data[i];
            if (c == 0) break;
            if (c >= 32 && c < 127) name += (wchar_t)c;
        }
        while (!name.empty() && name.back() == L' ') name.pop_back();
    }
    if (name.empty()) {
        wchar_t b[32];
        swprintf(b, 32, L"Fan %d", idx + 1);
        name = b;
    }
    return name;
}

bool FanSet::discover(const smc::Device& dev) {
    fans_.clear();
    hasFsBang_ = false;
    anyMdKey_  = false;

    auto n = dev.readNumber(smc::Key("FNum"));
    if (!n) return false;
    // Key names are "F<decimal digit><suffix>", so a two-digit index would not
    // fit the 4-character key format. No Mac ships more than a handful anyway.
    const int count = (int)*n;
    if (count <= 0 || count > 10) return false;

    for (int i = 0; i < count; ++i) {
        Fan f;
        f.index = i;
        f.keyAc = fanKey(i, "Ac");
        f.keyMn = fanKey(i, "Mn");
        f.keyMx = fanKey(i, "Mx");
        f.keyTg = fanKey(i, "Tg");
        f.keyMd = fanKey(i, "Md");

        if (auto v = dev.readNumber(f.keyMn)) f.minRpm = *v;
        if (auto v = dev.readNumber(f.keyMx)) f.maxRpm = *v;

        smc::Value id;
        if (dev.readKey(fanKey(i, "ID"), &id)) f.name = parseFanName(id, i);
        else                                   f.name = parseFanName(smc::Value{}, i);

        smc::KeyInfo ki{};
        f.hasMdKey = dev.getKeyInfo(f.keyMd, &ki) && (ki.dataAttributes & smc::ATTR_WRITABLE);
        if (f.hasMdKey) anyMdKey_ = true;

        fans_.push_back(f);
    }

    smc::KeyInfo ki{};
    hasFsBang_ = dev.getKeyInfo(smc::Key("FS! "), &ki) && (ki.dataAttributes & smc::ATTR_WRITABLE);
    if (hasFsBang_) readMask(dev, &origMask_);

    poll(dev);
    return true;
}

void FanSet::poll(const smc::Device& dev) {
    for (auto& f : fans_) {
        if (auto v = dev.readNumber(f.keyAc)) f.curRpm = *v;
        if (auto v = dev.readNumber(f.keyTg)) f.tgtRpm = *v;
    }
}

bool FanSet::readMask(const smc::Device& dev, uint16_t* out) const {
    auto v = dev.readNumber(smc::Key("FS! "));
    if (!v) return false;
    *out = (uint16_t)*v;
    return true;
}

bool FanSet::writeMask(const smc::Device& dev, uint16_t v) const {
    return dev.writeNumber(smc::Key("FS! "), (double)v);
}

bool FanSet::isManual(const smc::Device& dev, size_t i) const {
    if (i >= fans_.size()) return false;
    if (fans_[i].hasMdKey) {
        if (auto v = dev.readNumber(fans_[i].keyMd)) return *v != 0.0;
    }
    if (hasFsBang_) {
        uint16_t m = 0;
        if (readMask(dev, &m)) return (m & (1u << i)) != 0;
    }
    return false;
}

bool FanSet::setManual(const smc::Device& dev, size_t i, double rpm) {
    if (i >= fans_.size()) return false;
    Fan& f = fans_[i];
    if (!f.controllable()) return false;
    const double target = clampRpm(f, rpm);

    // 1. Take the fan off automatic control.
    bool engaged = false;
    if (hasFsBang_) {
        uint16_t m = 0;
        if (readMask(dev, &m)) {
            const uint16_t want = (uint16_t)(m | (1u << i));
            engaged = (want == m) || writeMask(dev, want);
        }
    }
    if (!engaged && f.hasMdKey) engaged = dev.writeNumber(f.keyMd, 1.0);
    if (!engaged) return false;

    // 2. Drive the target. Do this after engaging manual mode so the SMC does
    //    not briefly apply the old target under automatic control.
    if (!dev.writeNumber(f.keyTg, target)) return false;
    f.tgtRpm = target;
    return true;
}

bool FanSet::setAuto(const smc::Device& dev, size_t i) {
    if (i >= fans_.size()) return false;
    Fan& f = fans_[i];
    bool ok = false;

    if (hasFsBang_) {
        uint16_t m = 0;
        if (readMask(dev, &m)) {
            const uint16_t want = (uint16_t)(m & ~(1u << i));
            ok = (want == m) || writeMask(dev, want);
        }
    }
    if (f.hasMdKey) {
        // Clear it regardless of the FS! result; leaving it set would pin the fan.
        ok = dev.writeNumber(f.keyMd, 0.0) || ok;
    }
    return ok;
}

bool FanSet::restoreAll(const smc::Device& dev) {
    if (!dev.isOpen()) return false;
    bool ok = true;
    for (auto& f : fans_) {
        if (f.hasMdKey && !dev.writeNumber(f.keyMd, 0.0)) ok = false;
    }
    if (hasFsBang_) {
        // Restore the exact bitmask the machine had before we attached, rather
        // than assuming zero: another tool may legitimately own a fan.
        if (!writeMask(dev, origMask_)) ok = false;
    }
    return ok;
}

} // namespace fans
