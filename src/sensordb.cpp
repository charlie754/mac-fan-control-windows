#include "sensordb.h"

#include <unordered_map>
#include <string>

namespace sensors {

const wchar_t* categoryName(Category c) {
    switch (c) {
        case Category::Temperature: return L"Temperature";
        case Category::Fan:         return L"Fan";
        case Category::Voltage:     return L"Voltage";
        case Category::Current:     return L"Current";
        case Category::Power:       return L"Power";
        default:                    return L"Other";
    }
}

const wchar_t* categoryUnit(Category c) {
    switch (c) {
        case Category::Temperature: return L"°C";
        case Category::Fan:         return L"RPM";
        case Category::Voltage:     return L"V";
        case Category::Current:     return L"A";
        case Category::Power:       return L"W";
        default:                    return L"";
    }
}

// ---- catalogue ------------------------------------------------------------
//
// Entries flagged "[observed]" were present in the live MacBookPro14,3 dump.
// Entries flagged "[MBP15,1]" come from a single user report on a T2 MacBookPro15,1
// (issue #1); their names are inferred from Apple's naming convention, not confirmed.

struct Entry { const char* key; const wchar_t* name; Category cat; bool noteworthy; };

static const Entry kCatalogue[] = {
    // --- CPU temperatures --------------------------------------------------
    {"TC0P", L"CPU proximity",                 Category::Temperature, true },  // [observed]
    {"TC0E", L"CPU package alt. E",            Category::Temperature, true },  // [observed]
    {"TC0F", L"CPU package alt. F",            Category::Temperature, true },  // [observed]
    {"TC0D", L"CPU die",                       Category::Temperature, true },
    {"TCXC", L"CPU PECI package",              Category::Temperature, true },  // [observed]
    {"TCXc", L"CPU PECI core",                 Category::Temperature, true },
    {"TC1C", L"CPU core 1",                    Category::Temperature, true },  // [observed]
    {"TC2C", L"CPU core 2",                    Category::Temperature, true },  // [observed]
    {"TC3C", L"CPU core 3",                    Category::Temperature, true },  // [observed]
    {"TC4C", L"CPU core 4",                    Category::Temperature, true },  // [observed]
    {"TC5C", L"CPU core 5",                    Category::Temperature, true },
    {"TC6C", L"CPU core 6",                    Category::Temperature, true },
    {"TC7C", L"CPU core 7",                    Category::Temperature, true },
    {"TC8C", L"CPU core 8",                    Category::Temperature, true },
    {"TCGC", L"CPU integrated graphics",       Category::Temperature, true },  // [observed]
    {"TCSA", L"CPU system agent",              Category::Temperature, true },  // [observed]
    {"TCTD", L"CPU die margin to Tj max",      Category::Temperature, false},  // [observed] delta, not absolute
    {"TCMc", L"CPU core max (unused)",         Category::Temperature, false},  // [observed] reads -128 sentinel
    {"TCMX", L"CPU core maximum",              Category::Temperature, true },  // [MBP15,1] reads above the
                                                                               // individual cores; a max
    {"TCFC", L"CPU core count",                Category::Other,       false},  // [observed] ui16 count, not a temp

    // --- GPU temperatures --------------------------------------------------
    {"TG0D", L"GPU die",                       Category::Temperature, true },  // [observed]
    {"TG0P", L"GPU proximity",                 Category::Temperature, true },  // [observed]
    {"TG0F", L"GPU alt. F",                    Category::Temperature, false},  // [observed]
    {"TGDD", L"Discrete GPU die",              Category::Temperature, true },  // [observed]
    {"TGDF", L"Discrete GPU alt. F",           Category::Temperature, false},  // [MBP15,1] mirrors TG0F
    {"TGVP", L"GPU VRM proximity",             Category::Temperature, true },  // [observed]
    {"TG1D", L"GPU 1 die",                     Category::Temperature, true },

    // --- storage / NAND ----------------------------------------------------
    {"TH0A", L"SSD A",                         Category::Temperature, true },  // [observed]
    {"TH0B", L"SSD B",                         Category::Temperature, true },  // [observed]
    {"TH0C", L"SSD C",                         Category::Temperature, true },  // [observed]
    {"TH0a", L"SSD A (alt)",                   Category::Temperature, false},  // [observed]
    {"TH0b", L"SSD B (alt)",                   Category::Temperature, false},  // [observed]
    {"TH0c", L"SSD C (alt)",                   Category::Temperature, false},  // [observed]
    {"TH0F", L"Drive bay F (absent)",          Category::Temperature, false},  // [observed] -38 sentinel
    {"TH0R", L"Drive bay R (absent)",          Category::Temperature, false},  // [observed] -38 sentinel
    {"TH0x", L"Drive bay X (absent)",          Category::Temperature, false},  // [observed] -127 sentinel
    {"TH0P", L"Hard drive proximity",          Category::Temperature, true },

    // --- battery -----------------------------------------------------------
    {"TB0T", L"Battery 1",                     Category::Temperature, true },  // [observed]
    {"TB1T", L"Battery 2",                     Category::Temperature, true },  // [observed]
    {"TB2T", L"Battery 3",                     Category::Temperature, true },  // [observed]
    {"TBXT", L"Battery max",                   Category::Temperature, true },  // [observed]

    // --- platform / chassis ------------------------------------------------
    {"TA0P", L"Ambient air",                   Category::Temperature, true },
    {"TA0V", L"Ambient (virtual)",             Category::Temperature, true },  // [observed]
    {"TA1P", L"Ambient air 2",                 Category::Temperature, true },
    {"TM0P", L"Memory proximity",              Category::Temperature, true },  // [observed]
    {"Tm0P", L"Memory proximity 2",            Category::Temperature, false},  // [MBP15,1] distinct from TM0P;
                                                                               // meaning inferred, unconfirmed
    {"TPCD", L"Platform controller hub die",   Category::Temperature, true },  // [observed]
    {"TW0P", L"Airport / Wi-Fi proximity",     Category::Temperature, true },  // [observed]
    {"Th1H", L"Heatpipe 1",                    Category::Temperature, true },  // [observed]
    {"Th2H", L"Heatpipe 2",                    Category::Temperature, true },  // [observed]
    {"Ts0P", L"Palm rest left",                Category::Temperature, true },  // [observed]
    {"Ts1P", L"Palm rest right",               Category::Temperature, true },  // [observed]
    {"Ts0S", L"Skin sensor 1",                 Category::Temperature, true },  // [observed]
    {"Ts1S", L"Skin sensor 2",                 Category::Temperature, true },  // [observed]
    {"Ts2S", L"Skin sensor 3",                 Category::Temperature, true },  // [observed]
    {"TTLD", L"Thunderbolt left die",          Category::Temperature, true },  // [observed]
    {"TTRD", L"Thunderbolt right die",         Category::Temperature, true },  // [observed]
    {"TaLC", L"Left cavity / actuator",        Category::Temperature, true },  // [observed]
    {"TaRC", L"Right cavity / actuator",       Category::Temperature, true },  // [observed]
    {"TN0P", L"Northbridge proximity",         Category::Temperature, true },
    {"TN0D", L"Northbridge die",               Category::Temperature, true },
    {"TL0P", L"Display proximity",             Category::Temperature, true },
    {"TO0P", L"Optical drive proximity",       Category::Temperature, true },
    {"TS0P", L"Palm rest",                     Category::Temperature, true },
    {"TI0P", L"Thunderbolt proximity",         Category::Temperature, true },
    {"TZ0C", L"Thermal zone 0",                Category::Temperature, false},

    // --- power / voltage / current (observed on MacBookPro14,3) ------------
    {"PC0R", L"CPU rail power",                Category::Power,   true },  // [observed]
    {"PCPC", L"CPU package cores power",       Category::Power,   true },  // [observed]
    {"PCPG", L"CPU package graphics power",    Category::Power,   true },  // [observed]
    {"PCPT", L"CPU package total power",       Category::Power,   true },  // [observed]
    {"PDTR", L"DC-in / adapter power",         Category::Power,   true },  // [observed]
    {"PSTR", L"System total power",            Category::Power,   true },
    {"VP0R", L"12V rail",                      Category::Voltage, true },  // [observed]
    {"VD0R", L"DC-in voltage",                 Category::Voltage, true },  // [observed]
    {"VG0C", L"GPU core voltage",              Category::Voltage, true },  // [observed]
    {"VCAC", L"CPU core voltage",              Category::Voltage, true },  // [observed]
    {"IC0R", L"CPU rail current",              Category::Current, true },  // [observed]
    {"ID0R", L"DC-in current",                 Category::Current, true },  // [observed]
    {"IB0R", L"Battery current",               Category::Current, true },
    {"IBAC", L"Battery current (avg)",         Category::Current, true },  // [observed]
    {"B0AP", L"Battery power",                 Category::Power,   true },  // [observed]
};

static const std::unordered_map<uint32_t, const Entry*>& catalogueIndex() {
    static const std::unordered_map<uint32_t, const Entry*> idx = [] {
        std::unordered_map<uint32_t, const Entry*> m;
        for (const auto& e : kCatalogue) m.emplace(smc::Key(e.key).raw, &e);
        return m;
    }();
    return idx;
}

static const Entry* lookup(smc::Key k) {
    const auto& idx = catalogueIndex();
    auto it = idx.find(k.raw);
    return it == idx.end() ? nullptr : it->second;
}

// ---- classification -------------------------------------------------------

Category classify(smc::Key k) {
    if (const Entry* e = lookup(k)) return e->cat;

    const std::string s = k.str();
    if (s.empty()) return Category::Other;
    switch (s[0]) {
        case 'T': return Category::Temperature;
        case 'F': return Category::Fan;
        case 'V': return Category::Voltage;
        case 'I': return Category::Current;
        case 'P': return Category::Power;
        default:  return Category::Other;
    }
}

std::wstring describe(smc::Key k) {
    if (const Entry* e = lookup(k)) return e->name;

    const std::string s = k.str();
    std::wstring w(s.begin(), s.end());
    switch (classify(k)) {
        case Category::Temperature: return L"Temperature sensor " + w;
        case Category::Fan:         return L"Fan key " + w;
        case Category::Voltage:     return L"Voltage " + w;
        case Category::Current:     return L"Current " + w;
        case Category::Power:       return L"Power " + w;
        default:                    return w;
    }
}

bool plausible(Category c, double v) {
    switch (c) {
        // Below 1 C on a running laptop means the sensor is not populated;
        // the SMC returns 0, -38.375, -127 or -128 for absent hardware.
        case Category::Temperature: return v > 1.0   && v < 125.0;
        case Category::Fan:         return v >= 0.0  && v < 20000.0;
        case Category::Voltage:     return v > 0.01  && v < 30.0;
        case Category::Current:     return v > 0.005 && v < 40.0;
        case Category::Power:       return v > 0.05  && v < 300.0;
        default:                    return true;
    }
}

bool isNoteworthy(smc::Key k) {
    if (const Entry* e = lookup(k)) return e->noteworthy;
    return false;
}

} // namespace sensors
