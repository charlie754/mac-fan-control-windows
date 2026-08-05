// cli.cpp - scriptable command line front end. Also the harness used to
// validate the write path against real hardware.

#include <windows.h>
#include <clocale>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <string>
#include <vector>

#include "smc.h"
#include "sensordb.h"
#include "fans.h"

static void usage() {
    printf(
        "MacFanCtl CLI - Apple SMC fan and temperature control\n\n"
        "  macfanctl-cli info              device, protocol, key count, fan summary\n"
        "  macfanctl-cli temps             every plausible temperature sensor\n"
        "  macfanctl-cli sensors           temperatures, voltages, currents, power\n"
        "  macfanctl-cli fans              fan speeds, limits and control mode\n"
        "  macfanctl-cli keys              dump every SMC key with type and value\n"
        "  macfanctl-cli get <KEY>         read one key (e.g. TC0P)\n"
        "  macfanctl-cli set <fan> <rpm>   take a fan to manual at <rpm>\n"
        "  macfanctl-cli auto [fan]        return a fan (or all fans) to the SMC\n"
        "  macfanctl-cli selftest          non-destructive write-path check\n");
}

static bool openDev(smc::Device& d) {
    if (d.open()) return true;
    std::wstring e = d.error();
    fwprintf(stderr, L"error: %ls\n", e.c_str());
    return false;
}

static int cmdInfo(smc::Device& d) {
    wprintf(L"device    : \\\\.\\APPLESMC\n");
    wprintf(L"protocol  : %ls\n", d.protocolName());
    printf ("keys      : %u\n", d.keyCount());
    fans::FanSet fs;
    if (fs.discover(d)) {
        printf("fans      : %zu\n", fs.count());
        printf("control   : %s (FS! mask 0x%04X)\n",
               fs.supportsControl() ? "available" : "unavailable", fs.originalMask());
        for (size_t i = 0; i < fs.count(); ++i) {
            const auto& f = fs.at(i);
            wprintf(L"  [%zu] %-14ls %5.0f RPM  (min %.0f, max %.0f, target %.0f)%ls\n",
                    i, f.name.c_str(), f.curRpm, f.minRpm, f.maxRpm, f.tgtRpm,
                    fs.isManual(d, i) ? L"  [MANUAL]" : L"");
        }
    } else {
        printf("fans      : none reported\n");
    }
    return 0;
}

static int cmdSensors(smc::Device& d, bool tempsOnly) {
    std::vector<std::pair<std::wstring, std::wstring>> rows;
    for (const smc::Key k : d.enumerateKeys()) {
        const auto cat = sensors::classify(k);
        if (cat == sensors::Category::Other || cat == sensors::Category::Fan) continue;
        if (tempsOnly && cat != sensors::Category::Temperature) continue;
        auto v = d.readNumber(k);
        if (!v || !sensors::plausible(cat, *v)) continue;

        wchar_t left[128], right[64];
        swprintf(left, 128, L"%-4hs  %ls", k.str().c_str(), sensors::describe(k).c_str());
        swprintf(right, 64, L"%8.2f %ls", *v, sensors::categoryUnit(cat));
        rows.emplace_back(left, right);
    }
    for (const auto& r : rows) wprintf(L"%-44ls %ls\n", r.first.c_str(), r.second.c_str());
    printf("\n%zu sensor(s)\n", rows.size());
    return 0;
}

static int cmdFans(smc::Device& d) {
    fans::FanSet fs;
    if (!fs.discover(d)) { fprintf(stderr, "no fans found\n"); return 1; }
    for (size_t i = 0; i < fs.count(); ++i) {
        const auto& f = fs.at(i);
        const double span = std::max(1.0, f.maxRpm - f.minRpm);
        const int pct = (int)((f.curRpm - f.minRpm) / span * 100.0);
        wprintf(L"[%zu] %-14ls %5.0f RPM  %3d%%  target %5.0f  range %.0f-%.0f  %ls\n",
                i, f.name.c_str(), f.curRpm, std::clamp(pct, 0, 100), f.tgtRpm,
                f.minRpm, f.maxRpm, fs.isManual(d, i) ? L"MANUAL" : L"system");
    }
    return 0;
}

static int cmdKeys(smc::Device& d) {
    for (const smc::Key k : d.enumerateKeys()) {
        smc::KeyInfo ki{};
        if (!d.getKeyInfo(k, &ki)) continue;
        smc::Value v;
        v.type = smc::Key(ki.dataType);
        const bool got = d.readKey(k, ki.dataSize, &v);
        printf("%-4s  %-5s  %2u  attr=0x%02X  %s\n",
               k.str().c_str(), v.type.str().c_str(), ki.dataSize, ki.dataAttributes,
               got ? v.toString().c_str() : "<read failed>");
    }
    return 0;
}

static int cmdGet(smc::Device& d, const char* key) {
    const smc::Key k(key);
    smc::KeyInfo ki{};
    if (!d.getKeyInfo(k, &ki)) { printf("no such key: %s\n", key); return 1; }
    smc::Value v;
    v.type = smc::Key(ki.dataType);
    if (!d.readKey(k, ki.dataSize, &v)) { printf("read failed\n"); return 1; }
    printf("%s  type=%s  size=%u  attr=0x%02X (%s%s)  value=%s\n",
           k.str().c_str(), v.type.str().c_str(), ki.dataSize, ki.dataAttributes,
           (ki.dataAttributes & smc::ATTR_READABLE) ? "r" : "-",
           (ki.dataAttributes & smc::ATTR_WRITABLE) ? "w" : "-",
           v.toString().c_str());
    return 0;
}

static int cmdSet(smc::Device& d, int idx, double rpm) {
    fans::FanSet fs;
    if (!fs.discover(d)) { fprintf(stderr, "no fans found\n"); return 1; }
    if (idx < 0 || idx >= (int)fs.count()) { fprintf(stderr, "fan index out of range\n"); return 1; }
    if (!fs.supportsControl()) { fprintf(stderr, "this Mac exposes no writable fan control key\n"); return 1; }

    const double clamped = fans::clampRpm(fs.at(idx), rpm);
    if (clamped != rpm)
        printf("note: %.0f RPM clamped to %.0f (allowed %.0f-%.0f)\n",
               rpm, clamped, fs.at(idx).minRpm, fs.at(idx).maxRpm);

    if (!fs.setManual(d, idx, clamped)) { fprintf(stderr, "write failed\n"); return 1; }
    Sleep(400);
    fs.poll(d);
    wprintf(L"fan %d (%ls) -> manual, target %.0f RPM (now reading %.0f)\n",
            idx, fs.at(idx).name.c_str(), fs.at(idx).tgtRpm, fs.at(idx).curRpm);
    printf("NOTE: the fan stays under manual control until you run 'auto'.\n");
    return 0;
}

static int cmdAuto(smc::Device& d, int idx) {
    fans::FanSet fs;
    if (!fs.discover(d)) { fprintf(stderr, "no fans found\n"); return 1; }
    if (idx < 0) {
        // restoreAll() would rewrite the mask we just captured, which is
        // already whatever the machine had; clear each fan explicitly instead.
        for (size_t i = 0; i < fs.count(); ++i) fs.setAuto(d, i);
        printf("all fans returned to system control\n");
        return 0;
    }
    if (idx >= (int)fs.count()) { fprintf(stderr, "fan index out of range\n"); return 1; }
    if (!fs.setAuto(d, idx)) { fprintf(stderr, "write failed\n"); return 1; }
    wprintf(L"fan %d (%ls) returned to system control\n", idx, fs.at(idx).name.c_str());
    return 0;
}

// Verifies the write path without changing how fast anything actually spins:
// it engages manual mode, sets the target to the speed the fan is already at,
// confirms the readback, then restores the original state.
static int cmdSelftest(smc::Device& d) {
    fans::FanSet fs;
    if (!fs.discover(d)) { fprintf(stderr, "no fans found\n"); return 1; }
    if (!fs.supportsControl()) { fprintf(stderr, "no writable control key\n"); return 1; }

    const uint16_t before = fs.originalMask();
    printf("FS! before        : 0x%04X\n", before);

    const auto& f = fs.at(0);
    const double hold = fans::clampRpm(f, f.curRpm);
    printf("fan 0 current     : %.0f RPM (target %.0f)\n", f.curRpm, f.tgtRpm);
    printf("engaging manual at: %.0f RPM (same speed - should be inaudible)\n", hold);

    if (!fs.setManual(d, 0, hold)) { fprintf(stderr, "FAIL: setManual\n"); return 1; }

    auto mask = d.readNumber(smc::Key("FS! "));
    printf("FS! after set     : 0x%04X  %s\n", mask ? (uint16_t)*mask : 0,
           (mask && ((uint16_t)*mask & 1)) ? "OK (bit 0 set)" : "FAIL");

    Sleep(700);
    fs.poll(d);
    printf("target readback   : %.0f RPM  %s\n", fs.at(0).tgtRpm,
           std::abs(fs.at(0).tgtRpm - hold) < 60 ? "OK" : "MISMATCH");
    printf("manual flag       : %s\n", fs.isManual(d, 0) ? "OK" : "FAIL");

    printf("restoring...\n");
    fs.restoreAll(d);
    mask = d.readNumber(smc::Key("FS! "));
    printf("FS! after restore : 0x%04X  %s\n", mask ? (uint16_t)*mask : 0xFFFF,
           (mask && (uint16_t)*mask == before) ? "OK" : "FAIL");
    return 0;
}

int main(int argc, char** argv) {
    // Sensor labels carry a degree sign; without this the console mangles it.
    SetConsoleOutputCP(CP_UTF8);
    setlocale(LC_ALL, ".UTF8");

    if (argc < 2) { usage(); return 1; }
    const std::string cmd = argv[1];

    smc::Device d;
    if (!openDev(d)) return 2;

    if (cmd == "info")     return cmdInfo(d);
    if (cmd == "temps")    return cmdSensors(d, true);
    if (cmd == "sensors")  return cmdSensors(d, false);
    if (cmd == "fans")     return cmdFans(d);
    if (cmd == "keys")     return cmdKeys(d);
    if (cmd == "selftest") return cmdSelftest(d);
    if (cmd == "get"  && argc >= 3) return cmdGet(d, argv[2]);
    if (cmd == "set"  && argc >= 4) return cmdSet(d, atoi(argv[2]), atof(argv[3]));
    if (cmd == "auto") return cmdAuto(d, argc >= 3 ? atoi(argv[2]) : -1);

    usage();
    return 1;
}
