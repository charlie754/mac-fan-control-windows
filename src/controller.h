// controller.h - owns the SMC connection, polls sensors and fans on a worker
// thread, and applies the configured control policy.
//
// The GUI never touches the SMC directly; it reads immutable snapshots and
// posts intents. That keeps all device I/O on one thread and makes the
// restore-on-exit path a single, well-defined place.

#pragma once

#include "smc.h"
#include "sensordb.h"
#include "fans.h"
#include "curve.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace app {

enum class FanMode { Auto, Manual, Curve };

const wchar_t* fanModeName(FanMode m);

struct FanConfig {
    FanMode       mode = FanMode::Auto;
    double        manualRpm = 0;
    std::string   sensorKey;     // "" = track the hottest valid temperature
    curve::Curve  curve;
};

struct SensorReading {
    smc::Key           key;
    std::wstring       name;
    sensors::Category  cat = sensors::Category::Other;
    double             value = 0;
};

struct FanState {
    std::wstring name;
    double  curRpm = 0;
    double  tgtRpm = 0;
    double  minRpm = 0;
    double  maxRpm = 0;
    FanMode mode = FanMode::Auto;
    double  manualRpm = 0;
    double  curveOutput = 0;     // what the curve last asked for
    std::string sensorKey;
    curve::Curve curve;
};

struct Snapshot {
    bool          connected = false;
    bool          controllable = false;
    std::wstring  status;
    std::wstring  protocolName;
    std::vector<SensorReading> sensors;   // temperatures first, then the rest
    std::vector<FanState>      fans;
    double        hottest = 0;
    std::wstring  hottestName;
    unsigned      pollCount = 0;
};

class Controller {
public:
    ~Controller();

    // Opens the device, enumerates sensors and fans, starts the worker thread.
    bool start();

    // Restores automatic fan control and closes the device. Idempotent.
    void stop();

    Snapshot snapshot() const;

    void setFanMode(size_t i, FanMode m);
    void setManualRpm(size_t i, double rpm);
    void setCurve(size_t i, const curve::Curve& c);
    void setCurveSensor(size_t i, const std::string& key);

    // Re-assert manual targets after the machine wakes: the SMC resets fan
    // control across a sleep transition.
    void reapplyAfterResume();

    // Every temperature key that produced a plausible reading, for the
    // curve-source dropdown.
    std::vector<std::pair<std::string, std::wstring>> temperatureChoices() const;

    const std::vector<FanConfig>& configs() const { return cfg_; }
    void applyConfigs(const std::vector<FanConfig>& c);

    // Emergency restore usable from a signal / exception handler. Does not
    // take the main lock and does not allocate.
    static void emergencyRestore();

private:
    void workerLoop();
    void pollOnce();
    double curveInput(const std::string& sensorKey) const;
    void applyControl(double dtSec);
    void rebuildSnapshot();

    mutable std::mutex mtx_;
    smc::Device        dev_;
    fans::FanSet       fanset_;
    std::vector<smc::Key> tempKeys_;      // keys that read plausibly at startup

    // One SMC sweep per tick feeds both the control loop and the UI. The SMC
    // is a slow microcontroller shared with the firmware's own thermal loop,
    // so re-reading a sensor per fan per tick is worth avoiding.
    std::vector<double> tempCache_;       // parallel to tempKeys_; NaN = unreadable
    double              hottest_ = 0;
    std::wstring        hottestName_;

    std::vector<FanConfig> cfg_;
    std::vector<curve::Smoother> smooth_;
    std::vector<double> curveOut_;
    Snapshot           snap_;
    std::atomic<bool>  running_{false};
    std::atomic<bool>  reapply_{false};
    std::thread        worker_;
    unsigned           polls_ = 0;
};

// The single live controller, used by the crash/exit safety net.
Controller* activeController();
void setActiveController(Controller* c);

} // namespace app
