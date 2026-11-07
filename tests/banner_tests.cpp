#include "online_banner.h"

#include <algorithm>
#include <cstdint>
#include <clocale>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace {

int checkFailures = 0;
#define CHECK(condition) do { if (!(condition)) ++checkFailures; } while (0)

int HexDigit(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

bool HexBytes(const std::string& text, std::vector<std::uint8_t>* out)
{
    if ((text.size() & 1u) != 0u) {
        return false;
    }
    out->clear();
    out->reserve(text.size() / 2u);
    for (std::size_t i = 0; i < text.size(); i += 2u) {
        const int hi = HexDigit(text[i]);
        const int lo = HexDigit(text[i + 1u]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        out->push_back(static_cast<std::uint8_t>((hi << 4) | lo));
    }
    return true;
}

bool ReadBytes(const std::filesystem::path& path, std::string* out)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return false;
    }
    const std::streampos end = file.tellg();
    if (end < 0) {
        return false;
    }
    out->assign(static_cast<std::size_t>(end), '\0');
    file.seekg(0, std::ios::beg);
    if (!out->empty()) {
        file.read(&(*out)[0], static_cast<std::streamsize>(out->size()));
    }
    return file.good() || file.eof();
}

std::int64_t DaysFromCivil(int year, unsigned month, unsigned day)
{
    year -= month <= 2u ? 1 : 0;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - era * 400);
    const unsigned mp = month > 2u ? month - 3u : month + 9u;
    const unsigned doy = (153u * mp + 2u) / 5u + day - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return static_cast<std::int64_t>(era) * 146097 +
           static_cast<std::int64_t>(doe) - 719468;
}

bool DigitsAt(const std::string& text, std::size_t start, std::size_t count)
{
    if (start > text.size() || count > text.size() - start) {
        return false;
    }
    for (std::size_t i = start; i < start + count; ++i) {
        if (text[i] < '0' || text[i] > '9') {
            return false;
        }
    }
    return true;
}

bool ParseTime(const std::string& text, std::int64_t* out)
{
    if (text.size() != 20u || text[4] != '-' || text[7] != '-' ||
        text[10] != 'T' || text[13] != ':' || text[16] != ':' ||
        text[19] != 'Z' || !DigitsAt(text, 0, 4) || !DigitsAt(text, 5, 2) ||
        !DigitsAt(text, 8, 2) || !DigitsAt(text, 11, 2) ||
        !DigitsAt(text, 14, 2) || !DigitsAt(text, 17, 2)) {
        return false;
    }
    const int year = std::stoi(text.substr(0, 4));
    const unsigned month = static_cast<unsigned>(std::stoi(text.substr(5, 2)));
    const unsigned day = static_cast<unsigned>(std::stoi(text.substr(8, 2)));
    const unsigned hour = static_cast<unsigned>(std::stoi(text.substr(11, 2)));
    const unsigned minute = static_cast<unsigned>(std::stoi(text.substr(14, 2)));
    const unsigned second = static_cast<unsigned>(std::stoi(text.substr(17, 2)));
    if (year < 2000 || year > 9999 || month < 1u || month > 12u ||
        hour > 23u || minute > 59u || second > 59u) {
        return false;
    }
    const unsigned leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ? 1u : 0u;
    const unsigned daysInMonth[] = {0u, 31u, 28u + leap, 31u, 30u, 31u, 30u,
                                    31u, 31u, 30u, 31u, 30u, 31u};
    if (day < 1u || day > daysInMonth[month]) {
        return false;
    }
    const std::int64_t days = DaysFromCivil(year, month, day);
    *out = days * 86400000ll + static_cast<std::int64_t>(hour) * 3600000ll +
           static_cast<std::int64_t>(minute) * 60000ll +
           static_cast<std::int64_t>(second) * 1000ll;
    return true;
}

const ob::json::Value* Member(const ob::json::Value* object, const char* key)
{
    return object == nullptr ? nullptr : object->Find(key);
}

bool Integer(const ob::json::Value* value, std::int64_t* out)
{
    if (value == nullptr || value->type != ob::json::Type::Number ||
        value->text.empty()) {
        return false;
    }
    const bool negative = value->text[0] == '-';
    const std::size_t first = negative ? 1u : 0u;
    if (first == value->text.size()) {
        return false;
    }
    const std::uint64_t positiveLimit =
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    const std::uint64_t limit = negative ? positiveLimit + 1u : positiveLimit;
    std::uint64_t magnitude = 0;
    for (std::size_t i = first; i < value->text.size(); ++i) {
        const char ch = value->text[i];
        if (ch < '0' || ch > '9') {
            return false;
        }
        const std::uint64_t digit = static_cast<std::uint64_t>(ch - '0');
        if (magnitude > (limit - digit) / 10u) {
            return false;
        }
        magnitude = magnitude * 10u + digit;
    }
    if (negative) {
        *out = magnitude == positiveLimit + 1u
                   ? std::numeric_limits<std::int64_t>::min()
                   : -static_cast<std::int64_t>(magnitude);
    } else {
        *out = static_cast<std::int64_t>(magnitude);
    }
    return true;
}

int Base64Value(char ch)
{
    if (ch >= 'A' && ch <= 'Z') {
        return ch - 'A';
    }
    if (ch >= 'a' && ch <= 'z') {
        return ch - 'a' + 26;
    }
    if (ch >= '0' && ch <= '9') {
        return ch - '0' + 52;
    }
    if (ch == '+') {
        return 62;
    }
    if (ch == '/') {
        return 63;
    }
    return -1;
}

bool DecodeSignatureLine(const std::string& line, std::vector<std::uint8_t>* out)
{
    if (line.size() != 88u || line[86] != '=' || line[87] != '=') {
        return false;
    }
    out->clear();
    out->reserve(64u);
    for (std::size_t i = 0; i < line.size(); i += 4u) {
        const int a = Base64Value(line[i]);
        const int b = Base64Value(line[i + 1u]);
        const int c = line[i + 2u] == '=' ? 0 : Base64Value(line[i + 2u]);
        const int d = line[i + 3u] == '=' ? 0 : Base64Value(line[i + 3u]);
        if (a < 0 || b < 0 || c < 0 || d < 0 ||
            (i + 4u == line.size() && (c != 0 || d != 0))) {
            return false;
        }
        const unsigned triple = (static_cast<unsigned>(a) << 18) |
                                (static_cast<unsigned>(b) << 12) |
                                (static_cast<unsigned>(c) << 6) |
                                static_cast<unsigned>(d);
        out->push_back(static_cast<std::uint8_t>(triple >> 16));
        if (i + 4u != line.size()) {
            out->push_back(static_cast<std::uint8_t>(triple >> 8));
            out->push_back(static_cast<std::uint8_t>(triple));
        }
    }
    return out->size() == 64u;
}

ob::Action ActionFor(const std::string& text)
{
    if (text == "show") {
        return ob::Action::Show;
    }
    if (text == "killswitch") {
        return ob::Action::KillSwitch;
    }
    return ob::Action::Reject;
}

const char* ReasonFor(const ob::json::Value* value)
{
    if (value == nullptr || value->type == ob::json::Type::Null) {
        return nullptr;
    }
    return value->text.c_str();
}

const char* ActionName(ob::Action action)
{
    switch (action) {
    case ob::Action::Show: return "show";
    case ob::Action::KillSwitch: return "killswitch";
    case ob::Action::Reject: return "reject";
    }
    return "unknown";
}

bool SameReason(const char* a, const char* b)
{
    if (a == nullptr || b == nullptr) {
        return a == b;
    }
    return std::strcmp(a, b) == 0;
}

} // namespace

int RunVectors(const std::filesystem::path& vectors)
{
    std::string casesBytes;
    if (!ReadBytes(vectors / "cases.json", &casesBytes)) {
        std::printf("FAIL cases.json could not be read\n");
        std::printf("VECTORS 0 of 0\n");
        CHECK(false);
        return 1;
    }

    ob::json::Value document;
    const char* parseReason = ob::json::Parse(casesBytes, 64, &document);
    const ob::json::Value* publicKeyHex = Member(&document, "publicKeyHex");
    const ob::json::Value* keyId = Member(&document, "keyId");
    const ob::json::Value* cases = Member(&document, "cases");
    if (parseReason != nullptr || publicKeyHex == nullptr || keyId == nullptr ||
        cases == nullptr || cases->type != ob::json::Type::Array ||
        publicKeyHex->type != ob::json::Type::String ||
        keyId->type != ob::json::Type::String) {
        std::printf("FAIL cases.json parse %s\n", parseReason == nullptr ? "schema" : parseReason);
        std::printf("VECTORS 0 of 0\n");
        CHECK(false);
        return 1;
    }

    std::vector<std::uint8_t> publicKey;
    std::uint8_t publicKeyArray[65]{};
    const bool publicKeyHexLengthOk = publicKeyHex->text.size() == 130u;
    CHECK(publicKeyHexLengthOk);
    const bool publicKeyOk = publicKeyHexLengthOk &&
                             HexBytes(publicKeyHex->text, &publicKey) &&
                             publicKey.size() == 65u;
    CHECK(publicKeyOk);
    if (!publicKeyOk) {
        std::printf("VECTORS 0 of %zu\n", cases->items.size());
        return 1;
    }
    std::copy(publicKey.begin(), publicKey.end(), publicKeyArray);
    CHECK(ob::KeyIdOf(publicKeyArray) == keyId->text);

    std::size_t passed = 0;
    const std::size_t total = cases->items.size();
    CHECK(total >= 42u);
    std::string validSigned;
    std::vector<std::uint8_t> validSignature;
    std::int64_t validNowMs = 0;
    std::int64_t validHwm = 0;
    std::int64_t validFloor = 0;
    bool validSetup = false;

    for (const ob::json::Value& item : cases->items) {
        const ob::json::Value* name = Member(&item, "name");
        const ob::json::Value* file = Member(&item, "file");
        const ob::json::Value* now = Member(&item, "now");
        const ob::json::Value* hwm = Member(&item, "hwm");
        const ob::json::Value* floor = Member(&item, "floor");
        const ob::json::Value* stored = Member(&item, "stored");
        const ob::json::Value* expect = Member(&item, "expect");
        const ob::json::Value* reason = Member(&item, "reason");
        const std::string caseName = name != nullptr ? name->text : "<missing>";
        std::string signedBytes;
        std::int64_t nowMs = 0;
        std::int64_t hwmValue = 0;
        std::int64_t floorValue = 0;
        bool setupOk = name != nullptr && file != nullptr && now != nullptr &&
                       hwm != nullptr && floor != nullptr && stored != nullptr &&
                       expect != nullptr && reason != nullptr &&
                       name->type == ob::json::Type::String &&
                       file->type == ob::json::Type::String &&
                       now->type == ob::json::Type::String &&
                       ParseTime(now->text, &nowMs) && Integer(hwm, &hwmValue) &&
                       Integer(floor, &floorValue) &&
                       expect->type == ob::json::Type::String;
        std::string storedBytes;
        const std::string* storedPtr = nullptr;
        if (setupOk && stored->type == ob::json::Type::String) {
            setupOk = ReadBytes(vectors / stored->text, &storedBytes);
            storedPtr = &storedBytes;
        } else if (setupOk && stored->type != ob::json::Type::Null) {
            setupOk = false;
        }
        if (setupOk) {
            setupOk = ReadBytes(vectors / file->text, &signedBytes);
        }

        ob::Result result{};
        if (setupOk) {
            const ob::Context context{&publicKeyArray, nowMs, hwmValue,
                                      floorValue, storedPtr};
            result = ob::Evaluate(signedBytes, context);
        }
        const ob::Action expectedAction = setupOk ? ActionFor(expect->text)
                                                   : ob::Action::Reject;
        const char* expectedReason = setupOk ? ReasonFor(reason) : "test-setup";
        const bool actionOk = setupOk && result.action == expectedAction;
        const bool reasonOk = setupOk && SameReason(result.reason, expectedReason);
        if (!actionOk || !reasonOk) {
            std::printf("FAIL case %s action %s/%s reason %s/%s\n",
                        caseName.c_str(), ActionName(result.action),
                        ActionName(expectedAction), result.reason == nullptr ? "null" : result.reason,
                        expectedReason == nullptr ? "null" : expectedReason);
        }
        CHECK(actionOk);
        CHECK(reasonOk);

        bool photoOk = true;
        const ob::json::Value* photo = Member(&item, "photo");
        if (setupOk && photo != nullptr && photo->type != ob::json::Type::Null) {
            const ob::json::Value* photoFile = Member(photo, "file");
            const ob::json::Value* kind = Member(photo, "kind");
            const ob::json::Value* photoExpect = Member(photo, "expect");
            std::string photoBytes;
            photoOk = photo->type == ob::json::Type::Object &&
                      photoFile != nullptr && kind != nullptr && photoExpect != nullptr &&
                      photoFile->type == ob::json::Type::String &&
                      kind->type == ob::json::Type::String &&
                      photoExpect->type == ob::json::Type::String &&
                      ReadBytes(vectors / photoFile->text, &photoBytes);
            if (photoOk) {
                const bool isJpg = kind->text == "jpg";
                const char* photoReason = ob::CheckPhoto(
                    photoBytes, isJpg,
                    isJpg ? result.banner.sha256Jpg : result.banner.sha256Webp,
                    result.banner.w, result.banner.h);
                const char* expectedPhoto = photoExpect->text == "ok"
                                                ? nullptr
                                                : photoExpect->text.c_str();
                photoOk = SameReason(photoReason, expectedPhoto);
                if (!photoOk) {
                    std::printf("FAIL case %s photo %s/%s\n", caseName.c_str(),
                                photoReason == nullptr ? "ok" : photoReason,
                                expectedPhoto == nullptr ? "ok" : expectedPhoto);
                }
            }
            CHECK(photoOk);
        }

        if (actionOk && reasonOk && photoOk) {
            ++passed;
        }
        if (caseName == "valid") {
            validSigned = signedBytes;
            validSetup = setupOk;
            validNowMs = nowMs;
            validHwm = hwmValue;
            validFloor = floorValue;
            const std::size_t lf = validSigned.find('\n');
            if (lf != std::string::npos) {
                DecodeSignatureLine(validSigned.substr(0, lf), &validSignature);
            }
        }
    }

    std::printf("VECTORS %zu of %zu\n", passed, total);

    const std::size_t lf = validSigned.find('\n');
    const bool domainSetup = validSigned.size() > lf && validSignature.size() == 64u;
    CHECK(domainSetup);
    if (domainSetup) {
        const std::string jsonBytes = validSigned.substr(lf + 1u);
        CHECK(!ob::VerifyP256(publicKeyArray, jsonBytes, validSignature.data()));
    }

    const char* commaLocale = std::setlocale(LC_ALL, "de-DE");
    if (commaLocale == nullptr) {
        commaLocale = std::setlocale(LC_ALL, "German_Germany.1252");
    }
    CHECK(commaLocale != nullptr);
    if (commaLocale != nullptr && validSetup) {
        const ob::Context context{&publicKeyArray, validNowMs, validHwm,
                                  validFloor, nullptr};
        const ob::Result localeResult = ob::Evaluate(validSigned, context);
        CHECK(localeResult.action == ob::Action::Show);
        CHECK(localeResult.reason == nullptr);
    }
    CHECK(std::setlocale(LC_ALL, "C") != nullptr);

    return passed == total && checkFailures == 0 ? 0 : 1;
}


#include "online_banner.h"
#include "online_banner_machine.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace {

bool ParseFailure(std::string* failure, const std::string& field)
{
    if (failure != nullptr && failure->empty()) {
        *failure = field;
    }
    return false;
}

bool StringValue(const ob::json::Value* value, std::string* out)
{
    if (value == nullptr || value->type != ob::json::Type::String) {
        return false;
    }
    *out = value->text;
    return true;
}

bool BoolValue(const ob::json::Value* value, bool* out)
{
    if (value == nullptr || value->type != ob::json::Type::Bool) {
        return false;
    }
    *out = value->b;
    return true;
}

bool IntegerValue(const ob::json::Value* value, std::int64_t* out)
{
    if (value == nullptr || value->type != ob::json::Type::Number ||
        value->text.empty()) {
        return false;
    }

    bool negative = false;
    std::size_t pos = 0;
    if (value->text[0] == '-') {
        negative = true;
        pos = 1;
    }
    if (pos == value->text.size()) {
        return false;
    }
    const std::uint64_t limit = negative
                                    ? static_cast<std::uint64_t>(
                                          std::numeric_limits<std::int64_t>::max()) + 1u
                                    : static_cast<std::uint64_t>(
                                          std::numeric_limits<std::int64_t>::max());
    std::uint64_t result = 0;
    for (; pos < value->text.size(); ++pos) {
        const char ch = value->text[pos];
        if (ch < '0' || ch > '9') {
            return false;
        }
        const std::uint64_t digit = static_cast<std::uint64_t>(ch - '0');
        if (result > (limit - digit) / 10u) {
            return false;
        }
        result = result * 10u + digit;
    }
    if (negative) {
        if (result == static_cast<std::uint64_t>(
                          std::numeric_limits<std::int64_t>::max()) + 1u) {
            *out = std::numeric_limits<std::int64_t>::min();
        } else {
            *out = -static_cast<std::int64_t>(result);
        }
    } else {
        *out = static_cast<std::int64_t>(result);
    }
    return true;
}

bool ReadFile(const std::string& path, std::string* out)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return false;
    }
    const std::streampos end = file.tellg();
    if (end < 0) {
        return false;
    }
    out->assign(static_cast<std::size_t>(end), '\0');
    file.seekg(0, std::ios::beg);
    if (!out->empty()) {
        file.read(&(*out)[0], static_cast<std::streamsize>(out->size()));
    }
    return file.good() || file.eof();
}

bool ParseKind(const std::string& text, ob::Kind* out)
{
    if (text == "webp") {
        *out = ob::Kind::Webp;
        return true;
    }
    if (text == "jpg") {
        *out = ob::Kind::Jpg;
        return true;
    }
    return false;
}

bool ParseRecord(const ob::json::Value* value, ob::Record* out)
{
    if (value == nullptr || value->type != ob::json::Type::Object) {
        return false;
    }
    std::int64_t number = 0;
    if (!IntegerValue(Member(value, "hwm"), &number)) {
        return false;
    }
    out->hwm = number;
    if (!IntegerValue(Member(value, "lastCheckMs"), &out->lastCheckMs) ||
        !IntegerValue(Member(value, "failures"), &number)) {
        return false;
    }
    out->failures = static_cast<int>(number);

    const ob::json::Value* current = Member(value, "current");
    if (current == nullptr) {
        return false;
    }
    if (current->type == ob::json::Type::Null) {
        out->hasCurrent = false;
        out->current = ob::CurrentSet{};
        return true;
    }
    if (current->type != ob::json::Type::Object ||
        !IntegerValue(Member(current, "revision"), &out->current.revision) ||
        !StringValue(Member(current, "sha256"), &out->current.sha256) ||
        !BoolValue(Member(current, "enabled"), &out->current.enabled) ||
        !IntegerValue(Member(current, "expiresMs"), &out->current.expiresMs)) {
        return false;
    }
    const ob::json::Value* photos = Member(current, "photos");
    if (photos == nullptr || photos->type != ob::json::Type::Object) {
        return false;
    }
    out->current.photoWebp.clear();
    out->current.photoJpg.clear();
    const ob::json::Value* photoWebp = Member(photos, "webp");
    const ob::json::Value* photoJpg = Member(photos, "jpg");
    if ((photoWebp != nullptr && !StringValue(photoWebp, &out->current.photoWebp)) ||
        (photoJpg != nullptr && !StringValue(photoJpg, &out->current.photoJpg))) {
        return false;
    }
    out->hasCurrent = true;
    return true;
}

bool ParseEvent(const ob::json::Value* value, const std::vector<ob::Kind>& kinds,
                ob::Event* out, std::string* failure)
{
    if (value == nullptr || value->type != ob::json::Type::Object) {
        return ParseFailure(failure, "event");
    }
    std::string type;
    if (!StringValue(Member(value, "type"), &type)) {
        return ParseFailure(failure, "event.type");
    }
    if (type == "START") out->type = ob::EventType::Start;
    else if (type == "SHOWN") out->type = ob::EventType::Shown;
    else if (type == "HIDDEN") out->type = ob::EventType::Hidden;
    else if (type == "SIGNED") out->type = ob::EventType::Signed;
    else if (type == "PHOTOS") out->type = ob::EventType::Photos;
    else if (type == "COMMITTED") out->type = ob::EventType::Committed;
    else if (type == "FAILED") out->type = ob::EventType::Failed;
    else if (type == "TIMER") out->type = ob::EventType::Timer;
    else if (type == "RESUME") out->type = ob::EventType::Resume;
    else if (type == "CLICK") out->type = ob::EventType::Click;
    else if (type == "POINTER") out->type = ob::EventType::Pointer;
    else if (type == "FOCUS") out->type = ob::EventType::Focus;
    else return ParseFailure(failure, "event.type");

    const ob::json::Value* record = Member(value, "record");
    if (record != nullptr && record->type != ob::json::Type::Null) {
        out->hasRecord = true;
        if (!ParseRecord(record, &out->record)) {
            return ParseFailure(failure, "event.record");
        }
    }
    if (!BoolValue(Member(value, "setValid"), &out->setValid) &&
        out->type == ob::EventType::Start) {
        return ParseFailure(failure, "event.setValid");
    }
    if (!BoolValue(Member(value, "mayFetch"), &out->mayFetch) &&
        (out->type == ob::EventType::Shown || out->type == ob::EventType::Timer ||
         out->type == ob::EventType::Resume)) {
        return ParseFailure(failure, "event.mayFetch");
    }
    if (Member(value, "gen") != nullptr &&
        !IntegerValue(Member(value, "gen"), &out->gen)) {
        return ParseFailure(failure, "event.gen");
    }
    if (Member(value, "ok") != nullptr &&
        !BoolValue(Member(value, "ok"), &out->ok)) {
        return ParseFailure(failure, "event.ok");
    }
    if (Member(value, "over") != nullptr &&
        !BoolValue(Member(value, "over"), &out->over)) {
        return ParseFailure(failure, "event.over");
    }
    if (Member(value, "inside") != nullptr &&
        !BoolValue(Member(value, "inside"), &out->inside)) {
        return ParseFailure(failure, "event.inside");
    }

    if (out->type != ob::EventType::Signed) {
        return true;
    }
    const ob::json::Value* result = Member(value, "result");
    if (result == nullptr || result->type != ob::json::Type::Object) {
        return ParseFailure(failure, "event.result");
    }
    if (Member(result, "reason") == nullptr) {
        return ParseFailure(failure, "event.result.reason");
    }
    std::string action;
    if (!StringValue(Member(result, "action"), &action)) {
        return ParseFailure(failure, "event.result.action");
    }
    if (!IntegerValue(Member(result, "revision"), &out->result.banner.revision)) {
        return ParseFailure(failure, "event.result.revision");
    }
    if (!StringValue(Member(result, "sha256"), &out->result.sha256)) {
        return ParseFailure(failure, "event.result.sha256");
    }
    if (action == "show") out->result.action = ob::Action::Show;
    else if (action == "killswitch") out->result.action = ob::Action::KillSwitch;
    else if (action == "reject") out->result.action = ob::Action::Reject;
    else return ParseFailure(failure, "event.result.action");

    const ob::json::Value* banner = Member(result, "banner");
    if (banner == nullptr || banner->type != ob::json::Type::Object) {
        return ParseFailure(failure, "event.result.banner");
    }
    const ob::json::Value* expires = Member(banner, "expires");
    std::string expiresText;
    if (!StringValue(expires, &expiresText)) {
        return ParseFailure(failure, "event.result.banner.expires");
    }
    if (!ob::ParseTimestamp(expiresText, &out->result.banner.expiresMs)) {
        out->result.banner.expiresMs = std::numeric_limits<std::int64_t>::min();
    }
    if (out->result.action == ob::Action::Show) {
        const ob::json::Value* photo = Member(banner, "photo");
        if (photo == nullptr || photo->type != ob::json::Type::Object) {
            return ParseFailure(failure, "event.result.banner.photo");
        }
        const ob::json::Value* hashes = Member(photo, "sha256");
        if (hashes == nullptr || hashes->type != ob::json::Type::Object) {
            return ParseFailure(failure, "event.result.banner.photo.sha256");
        }
        for (const ob::Kind kind : kinds) {
            const char* name = kind == ob::Kind::Webp ? "webp" : "jpg";
            std::string* hash = kind == ob::Kind::Webp
                                    ? &out->result.banner.sha256Webp
                                    : &out->result.banner.sha256Jpg;
            const ob::json::Value* hashValue = Member(hashes, name);
            if (hashValue != nullptr && !StringValue(hashValue, hash)) {
                return ParseFailure(
                    failure, std::string("event.result.banner.photo.sha256.") + name);
            }
        }
    }
    return true;
}

bool EqualCurrent(const ob::CurrentSet& a, const ob::CurrentSet& b)
{
    return a.revision == b.revision && a.sha256 == b.sha256 &&
           a.enabled == b.enabled && a.expiresMs == b.expiresMs &&
           a.photoWebp == b.photoWebp && a.photoJpg == b.photoJpg;
}

bool EqualRecord(const ob::Record& a, const ob::Record& b)
{
    return a.hwm == b.hwm && a.hasCurrent == b.hasCurrent &&
           (!a.hasCurrent || EqualCurrent(a.current, b.current)) &&
           a.lastCheckMs == b.lastCheckMs && a.failures == b.failures;
}

bool ParseEffect(const ob::json::Value* value, ob::Effect* out,
                 std::string* failure)
{
    if (value == nullptr || value->type != ob::json::Type::Object) {
        return ParseFailure(failure, "effect");
    }
    std::string type;
    if (!StringValue(Member(value, "type"), &type)) {
        return ParseFailure(failure, "effect.type");
    }
    if (type == "fetch-signed") out->type = ob::EffectType::FetchSigned;
    else if (type == "fetch-photos") out->type = ob::EffectType::FetchPhotos;
    else if (type == "cancel") out->type = ob::EffectType::Cancel;
    else if (type == "write-set") out->type = ob::EffectType::WriteSet;
    else if (type == "write-record") out->type = ob::EffectType::WriteRecord;
    else if (type == "delete-unnamed-sets") out->type = ob::EffectType::DeleteUnnamedSets;
    else if (type == "display") out->type = ob::EffectType::Display;
    else if (type == "arm-timer") out->type = ob::EffectType::ArmTimer;
    else if (type == "disarm-timer") out->type = ob::EffectType::DisarmTimer;
    else if (type == "open-link") out->type = ob::EffectType::OpenLink;
    else return ParseFailure(failure, "effect.type");

    const auto Has = [value](const char* key) {
        return Member(value, key) != nullptr;
    };
    const auto Allowed = [out](const std::string& key) {
        if (key == "type") return true;
        switch (out->type) {
        case ob::EffectType::FetchSigned:
        case ob::EffectType::Cancel:
            return key == "gen";
        case ob::EffectType::FetchPhotos:
            return key == "gen" || key == "kinds";
        case ob::EffectType::WriteSet:
            return key == "gen" || key == "sha256";
        case ob::EffectType::WriteRecord:
            return key == "record";
        case ob::EffectType::DeleteUnnamedSets:
        case ob::EffectType::DisarmTimer:
        case ob::EffectType::OpenLink:
            return false;
        case ob::EffectType::Display:
            return key == "what" || key == "immediate";
        case ob::EffectType::ArmTimer:
            return key == "atMs";
        }
        return false;
    };
    for (const auto& member : value->members) {
        if (!Allowed(member.first)) {
            return ParseFailure(failure, "effect." + member.first);
        }
    }
    if ((out->type == ob::EffectType::FetchSigned ||
         out->type == ob::EffectType::FetchPhotos ||
         out->type == ob::EffectType::Cancel ||
         out->type == ob::EffectType::WriteSet) && !Has("gen")) {
        return ParseFailure(failure, "effect.gen");
    }
    if (out->type == ob::EffectType::FetchPhotos && !Has("kinds")) {
        return ParseFailure(failure, "effect.kinds");
    }
    if (out->type == ob::EffectType::WriteSet && !Has("sha256")) {
        return ParseFailure(failure, "effect.sha256");
    }
    if (out->type == ob::EffectType::WriteRecord && !Has("record")) {
        return ParseFailure(failure, "effect.record");
    }
    if (out->type == ob::EffectType::Display &&
        (!Has("what") || !Has("immediate"))) {
        return ParseFailure(failure, "effect.display");
    }
    if (out->type == ob::EffectType::ArmTimer && !Has("atMs")) {
        return ParseFailure(failure, "effect.atMs");
    }

    const ob::json::Value* gen = Member(value, "gen");
    if (gen != nullptr && !IntegerValue(gen, &out->gen)) {
        return ParseFailure(failure, "effect.gen");
    }
    const ob::json::Value* sha256 = Member(value, "sha256");
    if (sha256 != nullptr && !StringValue(sha256, &out->sha256)) {
        return ParseFailure(failure, "effect.sha256");
    }
    const ob::json::Value* atMs = Member(value, "atMs");
    if (atMs != nullptr && !IntegerValue(atMs, &out->atMs)) {
        return ParseFailure(failure, "effect.atMs");
    }
    const ob::json::Value* immediate = Member(value, "immediate");
    if (immediate != nullptr && !BoolValue(immediate, &out->immediate)) {
        return ParseFailure(failure, "effect.immediate");
    }
    const ob::json::Value* what = Member(value, "what");
    if (what != nullptr) {
        std::string name;
        if (!StringValue(what, &name)) {
            return ParseFailure(failure, "effect.what");
        }
        if (name == "online") out->online = true;
        else if (name != "dagoat") {
            return ParseFailure(failure, "effect.what");
        }
    }
    const ob::json::Value* record = Member(value, "record");
    if (record != nullptr && !ParseRecord(record, &out->record)) {
        return ParseFailure(failure, "effect.record");
    }

    const ob::json::Value* kinds = Member(value, "kinds");
    if (kinds != nullptr) {
        if (kinds->type != ob::json::Type::Array) {
            return ParseFailure(failure, "effect.kinds");
        }
        for (const ob::json::Value& item : kinds->items) {
            std::string name;
            ob::Kind kind = ob::Kind::Webp;
            if (!StringValue(&item, &name) || !ParseKind(name, &kind)) {
                return ParseFailure(failure, "effect.kinds");
            }
            out->kinds.push_back(kind);
        }
    }
    return true;
}

bool EqualEffect(const ob::Effect& a, const ob::Effect& b)
{
    if (a.type != b.type || a.gen != b.gen || a.kinds != b.kinds ||
        a.sha256 != b.sha256 || a.online != b.online ||
        a.immediate != b.immediate || a.atMs != b.atMs ||
        !EqualRecord(a.record, b.record)) {
        return false;
    }
    return true;
}

bool ParseStateRecord(const ob::json::Value* state, ob::Record* record)
{
    return ParseRecord(Member(state, "record"), record);
}

void AddIssue(std::string* issue, const std::string& text)
{
    if (!issue->empty()) {
        *issue += "; ";
    }
    *issue += text;
}

bool CheckState(const ob::json::Value* expected, const ob::Machine::State& state,
                std::string* issue)
{
    if (expected == nullptr) return true;
    if (expected->type != ob::json::Type::Object) {
        AddIssue(issue, "state is not an object");
        return false;
    }
    bool ok = true;
    for (const auto& member : expected->members) {
        const std::string& key = member.first;
        const ob::json::Value* value = &member.second;
        if (key == "record") {
            ob::Record expectedRecord;
            if (!ParseStateRecord(expected, &expectedRecord) ||
                !EqualRecord(expectedRecord, state.record)) {
                AddIssue(issue, "state.record");
                ok = false;
            }
        } else if (key == "visible") {
            bool actual = false;
            if (!BoolValue(value, &actual) || actual != state.visible) {
                AddIssue(issue, "state.visible");
                ok = false;
            }
        } else if (key == "gen") {
            std::int64_t actual = 0;
            if (!IntegerValue(value, &actual) || actual != state.gen) {
                AddIssue(issue, "state.gen");
                ok = false;
            }
        } else if (key == "attempting") {
            bool actual = false;
            if (!BoolValue(value, &actual) || actual != state.attempting) {
                AddIssue(issue, "state.attempting");
                ok = false;
            }
        } else if (key == "shown") {
            std::string actual;
            const std::string expectedShown = state.shownOnline ? "online" : "dagoat";
            if (!StringValue(value, &actual) || actual != expectedShown) {
                AddIssue(issue, "state.shown");
                ok = false;
            }
        } else if (key == "pendingSwap") {
            bool actual = false;
            if (!BoolValue(value, &actual) || actual != state.pendingSwap) {
                AddIssue(issue, "state.pendingSwap");
                ok = false;
            }
        } else if (key == "floor") {
            std::int64_t actual = 0;
            if (!IntegerValue(value, &actual) || actual != state.floor) {
                AddIssue(issue, "state.floor");
                ok = false;
            }
        } else if (key == "timerAtMs") {
            if (value->type == ob::json::Type::Null) {
                if (state.timerArmed) {
                    AddIssue(issue, "state.timerAtMs");
                    ok = false;
                }
            } else {
                std::int64_t actual = 0;
                if (!IntegerValue(value, &actual) || !state.timerArmed ||
                    actual != state.timerAtMs) {
                    AddIssue(issue, "state.timerAtMs");
                    ok = false;
                }
            }
        } else {
            AddIssue(issue, "unknown state key");
            ok = false;
        }
    }
    return ok;
}

bool IsForbiddenWhileHidden(ob::EffectType type)
{
    return type == ob::EffectType::FetchSigned ||
           type == ob::EffectType::FetchPhotos ||
           type == ob::EffectType::WriteSet ||
           type == ob::EffectType::WriteRecord ||
           type == ob::EffectType::ArmTimer ||
           type == ob::EffectType::Display;
}

} // namespace

int RunMachine(const std::string& vectors)
{
    std::string bytes;
    ob::json::Value document;
    if (!ReadFile(vectors + "\\transitions.json", &bytes) ||
        ob::json::Parse(bytes, 64, &document) != nullptr) {
        std::printf("FAIL transitions.json parse\n");
        std::printf("MACHINE 0 of 0\n");
        return 1;
    }
    const ob::json::Value* scenarios = Member(&document, "scenarios");
    if (scenarios == nullptr || scenarios->type != ob::json::Type::Array) {
        std::printf("FAIL transitions.json scenarios\n");
        std::printf("MACHINE 0 of 0\n");
        return 1;
    }
    if (scenarios->items.empty()) {
        std::printf("FAIL transitions.json scenarios is empty\n");
        std::printf("MACHINE 0 of 0\n");
        return 1;
    }

    std::size_t passed = 0;
    for (std::size_t scenarioIndex = 0; scenarioIndex < scenarios->items.size();
         ++scenarioIndex) {
        const ob::json::Value& scenario = scenarios->items[scenarioIndex];
        std::string name;
        std::int64_t floor = 0;
        const ob::json::Value* kindsValue = Member(&scenario, "kinds");
        const ob::json::Value* steps = Member(&scenario, "steps");
        bool scenarioOk = StringValue(Member(&scenario, "name"), &name) &&
                          IntegerValue(Member(&scenario, "floor"), &floor) &&
                          kindsValue != nullptr &&
                          kindsValue->type == ob::json::Type::Array &&
                          steps != nullptr && steps->type == ob::json::Type::Array;
        std::vector<ob::Kind> kinds;
        if (scenarioOk) {
            for (const ob::json::Value& item : kindsValue->items) {
                std::string kindText;
                ob::Kind kind = ob::Kind::Webp;
                if (!StringValue(&item, &kindText) || !ParseKind(kindText, &kind)) {
                    scenarioOk = false;
                    break;
                }
                kinds.push_back(kind);
            }
        }
        ob::Machine machine(floor, kinds);
        if (!scenarioOk) {
            std::printf("FAIL scenario %zu step -1: setup\n", scenarioIndex);
            continue;
        }

        for (std::size_t stepIndex = 0; stepIndex < steps->items.size(); ++stepIndex) {
            const ob::json::Value& step = steps->items[stepIndex];
            std::int64_t atMs = 0;
            ob::Event event;
            std::vector<ob::Effect> expected;
            const ob::json::Value* eventValue = Member(&step, "event");
            const ob::json::Value* expectedValue = Member(&step, "expect");
            std::string setupIssue;
            bool stepOk = IntegerValue(Member(&step, "atMs"), &atMs);
            if (!stepOk) {
                AddIssue(&setupIssue, "atMs");
            }
            if (stepOk && !ParseEvent(eventValue, kinds, &event, &setupIssue)) {
                stepOk = false;
            }
            if (stepOk && (expectedValue == nullptr ||
                           expectedValue->type != ob::json::Type::Array)) {
                AddIssue(&setupIssue, "expect");
                stepOk = false;
            }
            if (stepOk) {
                for (std::size_t effectIndex = 0;
                     effectIndex < expectedValue->items.size(); ++effectIndex) {
                    const ob::json::Value& item = expectedValue->items[effectIndex];
                    ob::Effect effect;
                    std::string effectIssue;
                    if (!ParseEffect(&item, &effect, &effectIssue)) {
                        AddIssue(&setupIssue,
                                 "expect[" + std::to_string(effectIndex) + "]" +
                                     (effectIssue.empty() ? "" : "." + effectIssue));
                        stepOk = false;
                        break;
                    }
                    expected.push_back(effect);
                }
            }

            std::string issue;
            if (!stepOk) {
                AddIssue(&issue, setupIssue.empty() ? "setup" :
                                                       "setup: " + setupIssue);
            } else {
                const std::vector<ob::Effect> actual = machine.Dispatch(event, atMs);
                if (actual.size() != expected.size()) {
                    AddIssue(&issue, "effects length");
                } else {
                    for (std::size_t effectIndex = 0; effectIndex < actual.size();
                         ++effectIndex) {
                        if (!EqualEffect(actual[effectIndex], expected[effectIndex])) {
                            AddIssue(&issue, "effect fields");
                            break;
                        }
                    }
                }
                const ob::Machine::State state = machine.GetState();
                if (!CheckState(Member(&step, "state"), state, &issue)) {
                    // CheckState records the fields that differ.
                }
                const std::vector<std::string> broken = machine.CheckInvariants();
                if (!broken.empty()) {
                    AddIssue(&issue, "invariants");
                }
                if (event.type != ob::EventType::Start && !state.visible) {
                    for (const ob::Effect& effect : actual) {
                        if (IsForbiddenWhileHidden(effect.type)) {
                            AddIssue(&issue, "forbidden hidden effect");
                            break;
                        }
                    }
                }
            }
            if (!issue.empty()) {
                std::printf("FAIL %s step %zu: %s\n", name.c_str(), stepIndex,
                            issue.c_str());
                scenarioOk = false;
            }
        }
        if (scenarioOk) {
            ++passed;
        }
    }

    std::printf("MACHINE %zu of %zu\n", passed, scenarios->items.size());
    return passed == scenarios->items.size() ? 0 : 1;
}


int main(int argc, char** argv)
{
    const std::filesystem::path vectors =
        argc > 1 && argv[1] != nullptr && argv[1][0] != '\0'
            ? std::filesystem::path(argv[1])
            : std::filesystem::path(argv[0]).parent_path() / ".." /
                  "tests" / "banner-vectors";
    const int vectorStatus = RunVectors(vectors);
    const int machineStatus = RunMachine(vectors.string());
    return vectorStatus == 0 && machineStatus == 0 ? 0 : 1;
}
