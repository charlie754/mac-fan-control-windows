#include "config.h"

#include <windows.h>
#include <shlobj.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace app {

std::wstring configPath() {
    wchar_t* appdata = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appdata)) && appdata) {
        dir = appdata;
        CoTaskMemFree(appdata);
    } else {
        dir = L".";
    }
    dir += L"\\MacFanCtl";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\config.ini";
}

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static std::string serializeCurve(const curve::Curve& c) {
    std::ostringstream os;
    for (size_t i = 0; i < c.points.size(); ++i) {
        if (i) os << ',';
        os << (int)(c.points[i].tempC + 0.5) << ':' << (int)(c.points[i].rpm + 0.5);
    }
    return os.str();
}

static curve::Curve parseCurve(const std::string& s) {
    curve::Curve c;
    std::istringstream is(s);
    std::string tok;
    while (std::getline(is, tok, ',')) {
        const size_t colon = tok.find(':');
        if (colon == std::string::npos) continue;
        curve::Point p;
        p.tempC = atof(tok.substr(0, colon).c_str());
        p.rpm   = atof(tok.substr(colon + 1).c_str());
        if (p.tempC > 0 && p.rpm >= 0) c.points.push_back(p);
    }
    c.sort();
    return c;
}

std::vector<FanConfig> loadConfig() {
    std::vector<FanConfig> out;
    // libstdc++ only accepts wide paths via filesystem::path, not wstring.
    std::ifstream in{std::filesystem::path(configPath())};
    if (!in) return out;

    int current = -1;
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;

        if (line.front() == '[' && line.back() == ']') {
            const std::string sec = line.substr(1, line.size() - 2);
            if (sec.rfind("fan", 0) == 0) {
                current = atoi(sec.c_str() + 3);
                if (current < 0 || current > 15) { current = -1; continue; }
                if ((int)out.size() <= current) out.resize(current + 1);
            } else {
                current = -1;
            }
            continue;
        }
        if (current < 0) continue;

        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = trim(line.substr(0, eq));
        const std::string v = trim(line.substr(eq + 1));
        FanConfig& fc = out[current];

        if (k == "mode") {
            if (v == "manual")     fc.mode = FanMode::Manual;
            else if (v == "curve") fc.mode = FanMode::Curve;
            else                   fc.mode = FanMode::Auto;
        } else if (k == "manual") {
            fc.manualRpm = atof(v.c_str());
        } else if (k == "sensor") {
            fc.sensorKey = v;
        } else if (k == "curve") {
            fc.curve = parseCurve(v);
        }
    }
    return out;
}

bool saveConfig(const std::vector<FanConfig>& cfg) {
    std::ofstream os{std::filesystem::path(configPath()), std::ios::trunc};
    if (!os) return false;

    os << "# MacFanCtl settings\n"
       << "# mode = system | manual | curve\n"
       << "# curve = <tempC>:<rpm>,<tempC>:<rpm>,...\n\n";

    for (size_t i = 0; i < cfg.size(); ++i) {
        const FanConfig& f = cfg[i];
        os << "[fan" << i << "]\n";
        os << "mode="   << (f.mode == FanMode::Manual ? "manual"
                          : f.mode == FanMode::Curve  ? "curve" : "system") << "\n";
        os << "manual=" << (int)(f.manualRpm + 0.5) << "\n";
        os << "sensor=" << f.sensorKey << "\n";
        os << "curve="  << serializeCurve(f.curve) << "\n\n";
    }
    return true;
}

} // namespace app
