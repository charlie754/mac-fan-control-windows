// sensordb.h - maps raw SMC keys to human-readable sensor descriptions and
// defines what counts as a plausible reading for each kind of sensor.
//
// The catalogue below is anchored on a full 911-key dump taken from a real
// MacBookPro14,3 (15-inch 2017, Touch Bar); keys marked as such were observed
// live on that machine. Unknown keys still get a sensible generated name so
// the app degrades gracefully on other models.

#pragma once

#include "smc.h"
#include <string>

namespace sensors {

enum class Category {
    Temperature,   // deg C
    Fan,           // RPM
    Voltage,       // V
    Current,       // A
    Power,         // W
    Other,
};

const wchar_t* categoryName(Category c);
const wchar_t* categoryUnit(Category c);

// Classify a key by its prefix / known catalogue entry.
Category classify(smc::Key k);

// Human-readable label, e.g. "CPU proximity". Falls back to a generated
// description for keys not in the catalogue.
std::wstring describe(smc::Key k);

// True if `v` is a physically plausible reading for this category. Used to
// drop the many dead/sentinel keys the SMC exposes (e.g. -127, -128, 0 on a
// machine that has no such component).
bool plausible(Category c, double v);

// True if the key is one of the well-known, genuinely useful sensors worth
// showing by default (as opposed to the long tail of diagnostic keys).
bool isNoteworthy(smc::Key k);

} // namespace sensors
