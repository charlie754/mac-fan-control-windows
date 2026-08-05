#include "controller.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace app {

const wchar_t* fanModeName(FanMode m) {
    switch (m) {
        case FanMode::Manual: return L"Manual";
        case FanMode::Curve:  return L"Auto (curve)";
        default:              return L"System";
    }
}

// ---- global handle for the crash / shutdown safety net --------------------

static std::atomic<Controller*> g_active{nullptr};
Controller* activeController() { return g_active.load(); }
void setActiveController(Controller* c) { g_active.store(c); }

// A last-resort restore that opens its own handle. Called from the unhandled
// exception filter and console control handler, where touching the worker
// thread's state would be unsafe.
void Controller::emergencyRestore() {
    HANDLE h = CreateFileW(L"\\\\.\\APPLESMC", GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;

#pragma pack(push, 1)
    struct { uint32_t key; uint8_t size; uint8_t data[32]; } req{};
#pragma pack(pop)
    req.key = smc::Key("FS! ").raw;
    req.size = 2;
    req.data[0] = 0;
    req.data[1] = 0;
    uint8_t resp = 0;
    DWORD ret = 0;
    DeviceIoControl(h, 0x220004u, &req, 7, &resp, 1, &ret, nullptr);
    CloseHandle(h);
}

// ---- lifecycle ------------------------------------------------------------

Controller::~Controller() { stop(); }

bool Controller::start() {
    std::lock_guard<std::mutex> lk(mtx_);

    if (!dev_.open()) {
        snap_ = Snapshot{};
        snap_.connected = false;
        snap_.status = dev_.error();
        return false;
    }

    if (!fanset_.discover(dev_)) {
        snap_.connected = true;
        snap_.status = L"Connected to the SMC, but no fans were reported (FNum unreadable).";
    }

    // Probe every temperature key once and keep only those that read
    // plausibly. This is what separates the ~20 real sensors from the long
    // tail of keys that return -127, -128 or 0 on hardware that lacks them.
    tempKeys_.clear();
    for (const smc::Key k : dev_.enumerateKeys()) {
        if (sensors::classify(k) != sensors::Category::Temperature) continue;
        auto v = dev_.readNumber(k);
        if (!v) continue;
        if (!sensors::plausible(sensors::Category::Temperature, *v)) continue;
        tempKeys_.push_back(k);
    }
    std::sort(tempKeys_.begin(), tempKeys_.end(),
              [](smc::Key a, smc::Key b) { return a.str() < b.str(); });

    const size_t n = fanset_.count();
    cfg_.assign(n, FanConfig{});
    smooth_.assign(n, curve::Smoother{});
    curveOut_.assign(n, 0.0);
    for (size_t i = 0; i < n; ++i) {
        const auto& f = fanset_.at(i);
        cfg_[i].manualRpm = f.minRpm;
        cfg_[i].curve = curve::Curve::defaultCurve(f.minRpm, f.maxRpm);
    }

    snap_.connected = true;
    snap_.controllable = fanset_.supportsControl();
    snap_.protocolName = dev_.protocolName();
    if (snap_.status.empty()) {
        snap_.status = snap_.controllable
            ? L"Connected."
            : L"Connected, but this Mac exposes no writable fan-control key; monitoring only.";
    }

    // Take one full reading before the UI is built so the first frame already
    // shows real values instead of an empty list.
    pollOnce();
    rebuildSnapshot();

    running_ = true;
    setActiveController(this);
    worker_ = std::thread(&Controller::workerLoop, this);
    return true;
}

void Controller::stop() {
    if (running_.exchange(false)) {
        if (worker_.joinable()) worker_.join();
    }
    std::lock_guard<std::mutex> lk(mtx_);
    if (dev_.isOpen()) {
        fanset_.restoreAll(dev_);   // hand the fans back before letting go
        dev_.close();
    }
    if (g_active.load() == this) setActiveController(nullptr);
}

// ---- worker ---------------------------------------------------------------

void Controller::workerLoop() {
    using clock = std::chrono::steady_clock;
    auto last = clock::now();

    while (running_.load()) {
        const auto now = clock::now();
        const double dt = std::chrono::duration<double>(now - last).count();
        last = now;

        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (reapply_.exchange(false)) {
                for (auto& s : smooth_) s.reset();
            }
            pollOnce();
            applyControl(dt);
            rebuildSnapshot();
        }

        // 1.5 s keeps the UI lively without hammering the SMC, which is a slow
        // microcontroller shared with the firmware's own thermal loop.
        for (int i = 0; i < 15 && running_.load(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void Controller::pollOnce() {
    ++polls_;
    fanset_.poll(dev_);

    // Single sweep of every known-good temperature key.
    tempCache_.assign(tempKeys_.size(), std::numeric_limits<double>::quiet_NaN());

    // Two candidates for "hottest component": the hottest sensor from the
    // curated catalogue, and the hottest of anything plausible. The catalogue
    // is preferred because it excludes misleading keys, but it is model
    // specific — on a Mac whose keys we don't recognise it would be empty, and
    // a curve tracking "hottest" would then see 0 C and silently hand the fan
    // back to the firmware. The unfiltered maximum is the safety net.
    double curatedMax = 0, anyMax = 0;
    std::wstring curatedName, anyName;

    for (size_t i = 0; i < tempKeys_.size(); ++i) {
        auto v = dev_.readNumber(tempKeys_[i]);
        if (!v || !sensors::plausible(sensors::Category::Temperature, *v)) continue;
        tempCache_[i] = *v;

        if (*v > anyMax) { anyMax = *v; anyName = sensors::describe(tempKeys_[i]); }
        if (sensors::isNoteworthy(tempKeys_[i]) && *v > curatedMax) {
            curatedMax = *v;
            curatedName = sensors::describe(tempKeys_[i]);
        }
    }

    if (curatedMax > 0) { hottest_ = curatedMax; hottestName_ = curatedName; }
    else                { hottest_ = anyMax;     hottestName_ = anyName; }
}

// Temperature a curve should follow: a specific key, or the hottest component.
double Controller::curveInput(const std::string& sensorKey) const {
    if (sensorKey.empty()) return hottest_;
    const smc::Key want(sensorKey.c_str());
    for (size_t i = 0; i < tempKeys_.size(); ++i)
        if (tempKeys_[i] == want && !std::isnan(tempCache_[i])) return tempCache_[i];
    return 0.0;   // configured sensor vanished: caller falls back to system control
}

void Controller::applyControl(double dtSec) {
    if (!fanset_.supportsControl()) return;

    for (size_t i = 0; i < fanset_.count(); ++i) {
        const FanConfig& c = cfg_[i];
        const fans::Fan& f = fanset_.at(i);

        switch (c.mode) {
            case FanMode::Auto:
                curveOut_[i] = 0.0;
                fanset_.setAuto(dev_, i);
                break;

            case FanMode::Manual: {
                curveOut_[i] = 0.0;
                const double want = fans::clampRpm(f, c.manualRpm);
                // Re-assert every tick: the SMC drops manual mode across sleep
                // and after some firmware events.
                if (std::abs(f.tgtRpm - want) > 25.0 || !fanset_.isManual(dev_, i))
                    fanset_.setManual(dev_, i, want);
                break;
            }

            case FanMode::Curve: {
                const double temp = curveInput(c.sensorKey);
                if (temp <= 0.0) {
                    // No usable temperature: fail safe to the firmware's control.
                    fanset_.setAuto(dev_, i);
                    break;
                }
                const double desired = fans::clampRpm(f, c.curve.eval(temp));
                const double want = fans::clampRpm(f, smooth_[i].update(desired, dtSec));
                curveOut_[i] = want;
                if (std::abs(f.tgtRpm - want) > 25.0 || !fanset_.isManual(dev_, i))
                    fanset_.setManual(dev_, i, want);
                break;
            }
        }
    }
}

void Controller::rebuildSnapshot() {
    snap_.sensors.clear();
    snap_.hottest = hottest_;
    snap_.hottestName = hottestName_;

    for (size_t i = 0; i < tempKeys_.size() && i < tempCache_.size(); ++i) {
        if (std::isnan(tempCache_[i])) continue;
        SensorReading r;
        r.key = tempKeys_[i];
        r.name = sensors::describe(tempKeys_[i]);
        r.cat = sensors::Category::Temperature;
        r.value = tempCache_[i];
        snap_.sensors.push_back(r);
    }
    std::sort(snap_.sensors.begin(), snap_.sensors.end(),
              [](const SensorReading& a, const SensorReading& b) { return a.value > b.value; });

    snap_.fans.clear();
    for (size_t i = 0; i < fanset_.count(); ++i) {
        const fans::Fan& f = fanset_.at(i);
        FanState s;
        s.name = f.name;
        s.curRpm = f.curRpm;
        s.tgtRpm = f.tgtRpm;
        s.minRpm = f.minRpm;
        s.maxRpm = f.maxRpm;
        s.mode = cfg_[i].mode;
        s.manualRpm = cfg_[i].manualRpm;
        s.sensorKey = cfg_[i].sensorKey;
        s.curve = cfg_[i].curve;
        s.curveOutput = curveOut_[i];
        snap_.fans.push_back(s);
    }
    snap_.pollCount = polls_;
}

// ---- intents --------------------------------------------------------------

Snapshot Controller::snapshot() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return snap_;
}

void Controller::setFanMode(size_t i, FanMode m) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (i >= cfg_.size()) return;
    if (cfg_[i].mode != m) {
        cfg_[i].mode = m;
        smooth_[i].reset();
        if (m == FanMode::Auto && dev_.isOpen()) fanset_.setAuto(dev_, i);
    }
}

void Controller::setManualRpm(size_t i, double rpm) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (i >= cfg_.size()) return;
    cfg_[i].manualRpm = rpm;
}

void Controller::setCurve(size_t i, const curve::Curve& c) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (i >= cfg_.size()) return;
    cfg_[i].curve = c;
    cfg_[i].curve.sort();
}

void Controller::setCurveSensor(size_t i, const std::string& key) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (i >= cfg_.size()) return;
    cfg_[i].sensorKey = key;
}

void Controller::reapplyAfterResume() { reapply_ = true; }

std::vector<std::pair<std::string, std::wstring>> Controller::temperatureChoices() const {
    std::lock_guard<std::mutex> lk(mtx_);
    std::vector<std::pair<std::string, std::wstring>> out;
    out.emplace_back("", L"Hottest component (recommended)");
    for (const smc::Key k : tempKeys_)
        out.emplace_back(k.str(), sensors::describe(k) + L"  [" +
                                  std::wstring(k.str().begin(), k.str().end()) + L"]");
    return out;
}

void Controller::applyConfigs(const std::vector<FanConfig>& c) {
    std::lock_guard<std::mutex> lk(mtx_);
    for (size_t i = 0; i < cfg_.size() && i < c.size(); ++i) {
        cfg_[i] = c[i];
        cfg_[i].curve.sort();
        if (cfg_[i].curve.points.empty())
            cfg_[i].curve = curve::Curve::defaultCurve(fanset_.at(i).minRpm, fanset_.at(i).maxRpm);
        smooth_[i].reset();
    }
    // Publish immediately: the GUI builds its controls from the snapshot right
    // after loading settings, before the worker has ticked.
    rebuildSnapshot();
}

} // namespace app
