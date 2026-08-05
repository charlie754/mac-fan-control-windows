// fans.h - fan discovery and control.
//
// Per-fan keys (N = fan index, decimal ASCII):
//   F<N>Ac  current speed        read-only   (fpe2 on MacBookPro14,x, flt on newer)
//   F<N>Mn  minimum speed        read/write
//   F<N>Mx  maximum speed        read/write
//   F<N>Tg  target speed         read/write  <- what we drive
//   F<N>ID  descriptor ({fds)    read-only   bytes 4..15 hold a 12-char name
//   F<N>Md  manual mode flag     read/write  (absent on MacBookPro14,3)
//   FNum    fan count (ui8)      read-only
//   FS!     manual bitmask ui16  read/write  bit N set = fan N under manual control
//
// Two mechanisms exist for taking a fan off automatic control. This model
// exposes FS! and not F<N>Md, so FS! is preferred with F<N>Md as a fallback.

#pragma once

#include "smc.h"
#include <string>
#include <vector>

namespace fans {

struct Fan {
    int          index = 0;
    std::wstring name;        // from F<N>ID, e.g. "Left side"
    double       minRpm = 0;  // F<N>Mn
    double       maxRpm = 0;  // F<N>Mx
    double       curRpm = 0;  // F<N>Ac  (refreshed by poll)
    double       tgtRpm = 0;  // F<N>Tg  (refreshed by poll)
    bool         hasMdKey = false;

    smc::Key keyAc, keyMn, keyMx, keyTg, keyMd;

    // False when the SMC did not give us usable Mn/Mx limits, in which case we
    // refuse to drive this fan rather than guess a safe range.
    bool controllable() const;
};

class FanSet {
public:
    // Discovers fans via FNum and reads their static limits/names.
    bool discover(const smc::Device& dev);

    // Refresh curRpm / tgtRpm for every fan.
    void poll(const smc::Device& dev);

    size_t count() const { return fans_.size(); }
    const std::vector<Fan>& all() const { return fans_; }
    const Fan& at(size_t i) const { return fans_[i]; }

    bool supportsControl() const { return hasFsBang_ || anyMdKey_; }

    // Snapshot of FS! as it was when we first attached, so we can hand the
    // machine back exactly as we found it.
    uint16_t originalMask() const { return origMask_; }

    // Put one fan under manual control at `rpm` (clamped to [Mn, Mx]).
    bool setManual(const smc::Device& dev, size_t i, double rpm);

    // Hand one fan back to the SMC's own thermal management.
    bool setAuto(const smc::Device& dev, size_t i);

    // Hand every fan back and restore FS! to its original value. Safe to call
    // repeatedly and from an exit path.
    bool restoreAll(const smc::Device& dev);

    bool isManual(const smc::Device& dev, size_t i) const;

private:
    bool readMask(const smc::Device& dev, uint16_t* out) const;
    bool writeMask(const smc::Device& dev, uint16_t v) const;

    std::vector<Fan> fans_;
    bool     hasFsBang_ = false;
    bool     anyMdKey_  = false;
    uint16_t origMask_  = 0;
};

// Clamp helper shared with the curve engine.
double clampRpm(const Fan& f, double rpm);

} // namespace fans
