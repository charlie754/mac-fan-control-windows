// curve.h - temperature -> RPM mapping with hysteresis and rate limiting.

#pragma once

#include <algorithm>
#include <string>
#include <vector>

namespace curve {

struct Point {
    double tempC = 0;
    double rpm   = 0;
};

// A piecewise-linear fan curve. Below the first point the first RPM is held;
// above the last point the last RPM is held.
struct Curve {
    std::vector<Point> points;

    static Curve defaultCurve(double minRpm, double maxRpm) {
        // Quiet until the machine is genuinely warm, then ramp hard. The knee
        // at 80 C sits just below the 2017 15-inch Tj-max throttling band.
        Curve c;
        c.points = {
            {45.0, minRpm},
            {60.0, minRpm + (maxRpm - minRpm) * 0.20},
            {70.0, minRpm + (maxRpm - minRpm) * 0.45},
            {80.0, minRpm + (maxRpm - minRpm) * 0.75},
            {90.0, maxRpm},
        };
        return c;
    }

    void sort() {
        std::sort(points.begin(), points.end(),
                  [](const Point& a, const Point& b) { return a.tempC < b.tempC; });
    }

    double eval(double tempC) const {
        if (points.empty()) return 0.0;
        if (tempC <= points.front().tempC) return points.front().rpm;
        if (tempC >= points.back().tempC)  return points.back().rpm;
        for (size_t i = 1; i < points.size(); ++i) {
            const Point& a = points[i - 1];
            const Point& b = points[i];
            if (tempC <= b.tempC) {
                const double span = b.tempC - a.tempC;
                if (span <= 0.0) return b.rpm;
                const double t = (tempC - a.tempC) / span;
                return a.rpm + t * (b.rpm - a.rpm);
            }
        }
        return points.back().rpm;
    }
};

// Smooths curve output so the fan does not hunt: the target only moves when
// it differs meaningfully, and it falls more slowly than it rises.
class Smoother {
public:
    void reset() { has_ = false; current_ = 0.0; }

    // `dtSec` is the interval since the last update.
    double update(double desired, double dtSec) {
        if (!has_) { current_ = desired; has_ = true; return current_; }

        const double delta = desired - current_;
        if (std::abs(delta) < kDeadbandRpm) return current_;

        // Rise quickly (thermal safety), fall gently (avoid audible pumping).
        const double maxStep = (delta > 0 ? kRiseRpmPerSec : kFallRpmPerSec) * std::max(0.1, dtSec);
        current_ += std::clamp(delta, -maxStep, maxStep);
        return current_;
    }

    double current() const { return current_; }

private:
    static constexpr double kDeadbandRpm    = 40.0;   // ignore trivial changes
    static constexpr double kRiseRpmPerSec  = 900.0;
    static constexpr double kFallRpmPerSec  = 220.0;

    bool   has_ = false;
    double current_ = 0.0;
};

} // namespace curve
