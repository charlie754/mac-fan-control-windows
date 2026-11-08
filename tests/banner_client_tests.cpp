#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <objidl.h>

namespace Gdiplus {
using std::max;
using std::min;
} // namespace Gdiplus
#include <gdiplus.h>

namespace gp = Gdiplus;

#include "goatcambanner.h"
#include "online_banner.h"
#include "online_banner_client.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "gdiplus.lib")

namespace fs = std::filesystem;

namespace {

using Bytes = std::vector<std::uint8_t>;

std::int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

Bytes ReadBytes(const fs::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    return {};
  input.seekg(0, std::ios::end);
  const std::streamoff size = input.tellg();
  if (size < 0)
    return {};
  input.seekg(0, std::ios::beg);
  Bytes bytes(static_cast<std::size_t>(size));
  if (!bytes.empty() &&
      !input.read(reinterpret_cast<char *>(bytes.data()), size)) {
    return {};
  }
  return bytes;
}

bool WriteBytes(const fs::path &path, const Bytes &bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output)
    return false;
  if (!bytes.empty()) {
    output.write(reinterpret_cast<const char *>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
  }
  return output.good();
}

bool WriteText(const fs::path &path, const std::string &text) {
  return WriteBytes(path, Bytes(text.begin(), text.end()));
}

Bytes Sha256(const Bytes &bytes) {
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  Bytes digest(32);
  if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(
          &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
    return {};
  }
  ULONG objectLength = 0;
  ULONG resultLength = 0;
  if (!BCRYPT_SUCCESS(BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                                        reinterpret_cast<PUCHAR>(&objectLength),
                                        sizeof(objectLength), &resultLength,
                                        0)) ||
      !BCRYPT_SUCCESS(
          BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0)) ||
      !BCRYPT_SUCCESS(BCryptHashData(hash, const_cast<PUCHAR>(bytes.data()),
                                     static_cast<ULONG>(bytes.size()), 0)) ||
      !BCRYPT_SUCCESS(BCryptFinishHash(hash, digest.data(),
                                       static_cast<ULONG>(digest.size()), 0))) {
    if (hash)
      BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return {};
  }
  (void)objectLength;
  BCryptDestroyHash(hash);
  BCryptCloseAlgorithmProvider(algorithm, 0);
  return digest;
}

std::string Hex(const Bytes &bytes) {
  static constexpr char digits[] = "0123456789abcdef";
  std::string result;
  result.reserve(bytes.size() * 2u);
  for (std::uint8_t byte : bytes) {
    result.push_back(digits[byte >> 4]);
    result.push_back(digits[byte & 0x0f]);
  }
  return result;
}

std::string Base64(const Bytes &bytes) {
  static constexpr char alphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string result;
  result.reserve(((bytes.size() + 2u) / 3u) * 4u);
  for (std::size_t offset = 0; offset < bytes.size(); offset += 3u) {
    const std::size_t remaining = bytes.size() - offset;
    const std::uint32_t value =
        (static_cast<std::uint32_t>(bytes[offset]) << 16) |
        (remaining > 1u ? static_cast<std::uint32_t>(bytes[offset + 1u]) << 8
                        : 0u) |
        (remaining > 2u ? static_cast<std::uint32_t>(bytes[offset + 2u]) : 0u);
    result.push_back(alphabet[(value >> 18) & 0x3f]);
    result.push_back(alphabet[(value >> 12) & 0x3f]);
    result.push_back(remaining > 1u ? alphabet[(value >> 6) & 0x3f] : '=');
    result.push_back(remaining > 2u ? alphabet[value & 0x3f] : '=');
  }
  return result;
}

struct KeyMaterial {
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_KEY_HANDLE key = nullptr;
  std::uint8_t publicKey[65]{};

  ~KeyMaterial() {
    if (key)
      BCryptDestroyKey(key);
    if (algorithm)
      BCryptCloseAlgorithmProvider(algorithm, 0);
  }
};

bool MakeKey(KeyMaterial *material) {
  if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(
          &material->algorithm, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0)) ||
      !BCRYPT_SUCCESS(
          BCryptGenerateKeyPair(material->algorithm, &material->key, 256, 0)) ||
      !BCRYPT_SUCCESS(BCryptFinalizeKeyPair(material->key, 0))) {
    return false;
  }
  ULONG blobSize = 0;
  ULONG resultSize = 0;
  if (!BCRYPT_SUCCESS(BCryptExportKey(material->key, nullptr,
                                      BCRYPT_ECCPUBLIC_BLOB, nullptr, 0,
                                      &blobSize, 0))) {
    return false;
  }
  Bytes blob(blobSize);
  if (!BCRYPT_SUCCESS(BCryptExportKey(material->key, nullptr,
                                      BCRYPT_ECCPUBLIC_BLOB, blob.data(),
                                      blobSize, &resultSize, 0)) ||
      resultSize < sizeof(BCRYPT_ECCKEY_BLOB) + 64u) {
    return false;
  }
  material->publicKey[0] = 0x04;
  std::copy_n(blob.data() + sizeof(BCRYPT_ECCKEY_BLOB), 64,
              material->publicKey + 1);
  return true;
}

Bytes Sign(KeyMaterial *material, const std::string &json) {
  const std::string message = "GOATCAM-BANNER-V1\n" + json;
  const Bytes messageBytes(message.begin(), message.end());
  const Bytes digest = Sha256(messageBytes);
  if (digest.size() != 32u)
    return {};
  Bytes signature(64);
  ULONG signatureSize = 0;
  if (!BCRYPT_SUCCESS(BCryptSignHash(
          material->key, nullptr, const_cast<PUCHAR>(digest.data()),
          static_cast<ULONG>(digest.size()), signature.data(),
          static_cast<ULONG>(signature.size()), &signatureSize, 0)) ||
      signatureSize != signature.size()) {
    return {};
  }
  return signature;
}

std::string Iso(std::int64_t ms) {
  const std::time_t seconds = static_cast<std::time_t>(ms / 1000);
  std::tm utc{};
  gmtime_s(&utc, &seconds);
  char text[32]{};
  sprintf_s(text, "%04d-%02d-%02dT%02d:%02d:%02dZ", utc.tm_year + 1900,
            utc.tm_mon + 1, utc.tm_mday, utc.tm_hour, utc.tm_min, utc.tm_sec);
  return text;
}

struct Fixture {
  std::string json;
  Bytes signedBytes;
  std::string sha256;
  std::int64_t revision = 0;
};

constexpr const char *kHarnessQr[] = {
    "0000000000000000000000000000000000000",
    "0000000000000000000000000000000000000",
    "0000000000000000000000000000000000000",
    "0000000000000000000000000000000000000",
    "0000111111101100010010101011111110000",
    "0000100000101010101001010010000010000",
    "0000101110101000011110011010111010000",
    "0000101110100010111101011010111010000",
    "0000101110101001000001110010111010000",
    "0000100000100111000011101010000010000",
    "0000111111101010101010101011111110000",
    "0000000000000100110110000000000000000",
    "0000100111111111100101000100101110000",
    "0000010111001001100110010000101100000",
    "0000011101100001010101100010001000000",
    "0000111111010110000111110001110010000",
    "0000101100110101111100011010000010000",
    "0000000010010110001100000011111110000",
    "0000111111100110101110110110101010000",
    "0000100100001100111100110110101010000",
    "0000000110111110100110111000010000000",
    "0000110111010100100110110100101100000",
    "0000111000111111111100010111110010000",
    "0000111101000011110000010000111000000",
    "0000111001101101010101011111111100000",
    "0000000000001000010111101000110000000",
    "0000111111101010110101111010110000000",
    "0000100000101010110011011000100100000",
    "0000101110101110010000011111110010000",
    "0000101110101100101100111101000010000",
    "0000101110100010101110001001101110000",
    "0000100000100110011100100011111010000",
    "0000111111101101001110111101100000000",
    "0000000000000000000000000000000000000",
    "0000000000000000000000000000000000000",
    "0000000000000000000000000000000000000",
    "0000000000000000000000000000000000000",
    "0000000000000000000000000000000000000",
};

Fixture MakeFixture(KeyMaterial *material, const Bytes &webp, const Bytes &jpg,
                    std::int64_t revision,
                    std::int64_t expiresDeltaMs = 86400000) {
  const std::string keyId = ob::KeyIdOf(material->publicKey);
  const std::string webpHash = Hex(Sha256(webp));
  const std::string jpgHash = Hex(Sha256(jpg));
  const std::int64_t now = NowMs();
  std::ostringstream json;
  json << "{\"schema\":1,\"keyId\":\"" << keyId
       << "\",\"revision\":" << revision << ",\"issued\":\"" << Iso(now - 60000)
       << "\",\"expires\":\"" << Iso(now + expiresDeltaMs)
       << "\",\"enabled\":true"
       << ",\"headline\":\"GOAT Cam \xE2\x80\x94 your iPhone, NOW your "
          "PC\xE2\x80\x99s webcam.\""
       << ",\"headlineEm\":\"NOW\""
       << ",\"badge\":\"1440p \xC2\xB7 60 fps\""
       << ",\"wordmark\":\"GOAT Cam\""
       << ",\"tagline\":[\"Your iPhone, NOW\",\"your PC\xE2\x80\x99s webcam.\"]"
       << ",\"effects\":{\"viewfinder\":true,\"steam\":true}"
       << ",\"link\":\"https://goatcam.dagoat.io\""
       << ",\"qr\":{\"text\":\"https://apps.apple.com/app/"
          "id6816268316\",\"size\":37,\"rows\":[";
  for (int row = 0; row < 37; ++row) {
    if (row != 0)
      json << ',';
    json << '"' << kHarnessQr[row] << '"';
  }
  json << "]}"
       << ",\"photo\":{\"webp\":\"hero-coffee-800.webp\",\"jpg\":\"hero-coffee-"
          "800.jpg\""
       << ",\"sha256\":{\"webp\":\"" << webpHash << "\",\"jpg\":\"" << jpgHash
       << "\"}"
       << ",\"w\":800,\"h\":498,\"focus\":[0.3,0.55]"
       << ",\"steamAt\":[0.17,0.62],\"textBox\":[0.33,0.1,0.4,0.45]}"
       << ",\"labels\":{\"link\":\"Open GOAT Cam\",\"qr\":\"GOAT Cam App Store "
          "QR\"}}";
  Fixture fixture;
  fixture.json = json.str();
  const Bytes signature = Sign(material, fixture.json);
  const std::string signedText = Base64(signature) + "\n" + fixture.json;
  fixture.signedBytes.assign(signedText.begin(), signedText.end());
  fixture.sha256 = Hex(Sha256(fixture.signedBytes));
  fixture.revision = revision;
  return fixture;
}

std::string StoredRecord(const Fixture &fixture, const Bytes &webp,
                         const Bytes &jpg, std::int64_t expiresMs) {
  return "{\"hwm\":" + std::to_string(fixture.revision) +
         ",\"current\":{\"revision\":" + std::to_string(fixture.revision) +
         ",\"sha256\":\"" + fixture.sha256 +
         "\",\"enabled\":true,\"expiresMs\":" + std::to_string(expiresMs) +
         ",\"photos\":{\"webp\":\"" + Hex(Sha256(webp)) + "\",\"jpg\":\"" +
         Hex(Sha256(jpg)) + "\"}},\"lastCheckMs\":" + std::to_string(NowMs()) +
         ",\"failures\":0}";
}

fs::path MakeTemp(const wchar_t *name) {
  wchar_t tempPath[MAX_PATH]{};
  const DWORD length = GetTempPathW(MAX_PATH, tempPath);
  if (length == 0 || length >= MAX_PATH)
    return {};
  const fs::path path =
      fs::path(tempPath) /
      (std::wstring(name) + L"-" + std::to_wstring(GetCurrentProcessId()) +
       L"-" + std::to_wstring(GetTickCount64()));
  std::error_code error;
  fs::create_directories(path, error);
  return error ? fs::path() : path;
}

class Server {
public:
  ~Server() { Stop(); }

  bool Start(const fs::path &script, const fs::path &root,
             const fs::path &log) {
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &security, 0))
      return false;
    if (!SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0)) {
      CloseHandle(readPipe);
      CloseHandle(writePipe);
      return false;
    }
    std::wstring command = L"python -I \"" + script.wstring() +
                           L"\" --root \"" + root.wstring() +
                           L"\" --port 0 --log \"" + log.wstring() + L"\"";
    std::vector<wchar_t> commandLine(command.begin(), command.end());
    commandLine.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = writePipe;
    startup.hStdError = writePipe;
    if (!CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                        &process_)) {
      CloseHandle(readPipe);
      CloseHandle(writePipe);
      return false;
    }
    CloseHandle(writePipe);
    output_ = readPipe;
    std::string portText;
    char character = 0;
    DWORD got = 0;
    while (ReadFile(output_, &character, 1, &got, nullptr) && got == 1) {
      if (character == '\n')
        break;
      portText.push_back(character);
      if (portText.size() > 10u)
        break;
    }
    try {
      port_ = static_cast<INTERNET_PORT>(std::stoul(portText));
    } catch (...) {
      Stop();
      return false;
    }
    return port_ != 0;
  }

  void Stop() {
    if (process_.hProcess) {
      TerminateProcess(process_.hProcess, 0);
      WaitForSingleObject(process_.hProcess, 5000);
      CloseHandle(process_.hThread);
      CloseHandle(process_.hProcess);
      process_ = PROCESS_INFORMATION{};
    }
    if (output_) {
      CloseHandle(output_);
      output_ = nullptr;
    }
  }

  INTERNET_PORT port() const { return port_; }

private:
  PROCESS_INFORMATION process_{};
  HANDLE output_ = nullptr;
  INTERNET_PORT port_ = 0;
};

std::size_t CountLines(const fs::path &path) {
  std::ifstream input(path);
  std::size_t count = 0;
  std::string line;
  while (std::getline(input, line))
    ++count;
  return count;
}

bool ReadRecord(const fs::path &path, std::int64_t *hwm, int *failures) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    return false;
  const std::string text((std::istreambuf_iterator<char>(input)),
                         std::istreambuf_iterator<char>());
  ob::json::Value root;
  if (ob::json::Parse(text, 32, &root) != nullptr ||
      root.type != ob::json::Type::Object)
    return false;
  const ob::json::Value *hwmValue = root.Find("hwm");
  const ob::json::Value *failuresValue = root.Find("failures");
  if (!hwmValue || !failuresValue || hwmValue->type != ob::json::Type::Number ||
      failuresValue->type != ob::json::Type::Number)
    return false;
  try {
    *hwm = std::stoll(hwmValue->text);
    *failures = std::stoi(failuresValue->text);
  } catch (...) {
    return false;
  }
  return true;
}

bool RecordParses(const fs::path &path) {
  const Bytes bytes = ReadBytes(path);
  ob::json::Value root;
  const std::string text(bytes.begin(), bytes.end());
  return !bytes.empty() && ob::json::Parse(text, 32, &root) == nullptr &&
         root.type == ob::json::Type::Object;
}

bool RecordCurrentIsNull(const fs::path &path) {
  const Bytes bytes = ReadBytes(path);
  const std::string text(bytes.begin(), bytes.end());
  ob::json::Value root;
  if (ob::json::Parse(text, 32, &root) != nullptr ||
      root.type != ob::json::Type::Object)
    return false;
  const ob::json::Value *current = root.Find("current");
  return current != nullptr && current->type == ob::json::Type::Null;
}

bool PumpUntil(const std::function<bool()> &done, DWORD timeoutMs) {
  const ULONGLONG deadline = GetTickCount64() + timeoutMs;
  for (;;) {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      if (message.message == WM_QUIT)
        return done();
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    if (done())
      return true;
    const ULONGLONG now = GetTickCount64();
    if (now >= deadline)
      return done();
    const DWORD remaining =
        static_cast<DWORD>(std::min<ULONGLONG>(deadline - now, 50));
    MsgWaitForMultipleObjectsEx(0, nullptr, remaining, QS_ALLINPUT,
                                MWMO_INPUTAVAILABLE);
  }
}

void PumpFor(DWORD durationMs) {
  const ULONGLONG deadline = GetTickCount64() + durationMs;
  PumpUntil([deadline]() { return GetTickCount64() >= deadline; },
            durationMs + 50);
}

bool ProcessHandleCount(DWORD *count) {
  return GetProcessHandleCount(GetCurrentProcess(), count) != FALSE;
}

ob::Config BaseConfig(const fs::path &cache, const KeyMaterial &key) {
  ob::Config config;
  config.host = L"127.0.0.1";
  config.https = false;
  config.allowInsecureForTests = true;
  config.cacheDir = cache.wstring();
  config.publicKey65 = &key.publicKey;
  config.floor = 0;
  config.kinds = {ob::Kind::Webp, ob::Kind::Jpg};
  config.userAgent = L"GOATCam-Harness/1";
  config.deadlineMs = 2000;
  return config;
}

bool InstallFixture(const fs::path &root, const Fixture &fixture,
                    const Bytes &webp, const Bytes &jpg) {
  std::error_code error;
  fs::create_directories(root / "banner", error);
  if (error)
    return false;
  return WriteBytes(root / "banner" / "v1.signed", fixture.signedBytes) &&
         WriteBytes(root / "banner" / "hero-coffee-800.webp", webp) &&
         WriteBytes(root / "banner" / "hero-coffee-800.jpg", jpg);
}

bool RunMode(const KeyMaterial &key, INTERNET_PORT port, const char *mode,
             bool expectOnline) {
  const fs::path cache = MakeTemp(L"goat-banner-cache");
  if (cache.empty())
    return false;
  ob::Config config = BaseConfig(cache, key);
  config.port = port;
  config.pathPrefix = L"/" + std::wstring(mode, mode + std::strlen(mode));
  bool result = false;
  {
    ob::Client client(config, nullptr, WM_APP + 73);
    client.Shown();
    const fs::path record = cache / "record.json";
    if (expectOnline) {
      result = PumpUntil(
          [&]() {
            std::int64_t hwm = 0;
            int failures = 0;
            return client.Current().online &&
                   ReadRecord(record, &hwm, &failures) && hwm == 1 &&
                   failures == 0;
          },
          7000);
    } else {
      result = PumpUntil(
          [&]() {
            std::int64_t hwm = 0;
            int failures = 0;
            return !client.Current().online &&
                   ReadRecord(record, &hwm, &failures) && failures == 1;
          },
          7000);
    }
  }
  std::error_code error;
  fs::remove_all(cache, error);
  return result;
}

bool RunHiddenMidAttempt(INTERNET_PORT port, const KeyMaterial &key,
                         const fs::path &log) {
  const fs::path cache = MakeTemp(L"goat-banner-hidden-cache");
  if (cache.empty())
    return false;
  const std::size_t beforeRequests = CountLines(log);
  ob::Config config = BaseConfig(cache, key);
  config.port = port;
  config.pathPrefix = L"/slowheaders";
  {
    ob::Client client(config, nullptr, WM_APP + 73);
    client.Shown();
    PumpFor(300);
    client.Hidden();
    PumpFor(1000);
  }
  const bool noRecord = !fs::exists(cache / "record.json");
  const bool oneRequest = CountLines(log) == beforeRequests + 1u;
  std::error_code error;
  fs::remove_all(cache, error);
  return noRecord && oneRequest;
}

bool RunCrashSimulation(INTERNET_PORT port, const KeyMaterial &key,
                        const Fixture &revision1, const Fixture &revision2,
                        const Bytes &webp, const Bytes &jpg) {
  const fs::path cache = MakeTemp(L"goat-banner-crash-cache");
  if (cache.empty())
    return false;
  ob::Config config = BaseConfig(cache, key);
  config.port = port;
  config.pathPrefix = L"/ok";
  bool result = false;
  bool firstRun = false;
  {
    ob::Client client(config, nullptr, WM_APP + 73);
    client.Shown();
    firstRun = PumpUntil(
        [&]() {
          std::int64_t hwm = 0;
          int failures = 0;
          return client.Current().online &&
                 ReadRecord(cache / "record.json", &hwm, &failures) &&
                 hwm == 1 && failures == 0;
        },
        7000);
  }
  const fs::path orphan = cache / "sets" / revision2.sha256;
  std::error_code error;
  fs::create_directories(orphan, error);
  const bool orphanWritten =
      !error && WriteBytes(orphan / "v1.signed", revision2.signedBytes) &&
      WriteBytes(orphan / "photo.webp", webp) &&
      WriteBytes(orphan / "photo.jpg", jpg);
  result = firstRun && orphanWritten;
  bool secondRun = false;
  {
    ob::Client client(config, nullptr, WM_APP + 73);
    client.Shown();
    secondRun =
        client.Current().online &&
        client.Current().banner.revision == revision1.revision &&
        PumpUntil(
            [&]() {
              return client.Current().online &&
                     client.Current().banner.revision == revision1.revision;
            },
            1000) &&
        !fs::exists(orphan);
    result = result && secondRun;
  }
  fs::remove_all(cache, error);
  return result;
}

bool RunStartupTamperedExpires(const KeyMaterial &key, const Fixture &fixture,
                               const Bytes &webp, const Bytes &jpg) {
  const fs::path cache = MakeTemp(L"goat-banner-tampered-expires-cache");
  if (cache.empty())
    return false;
  const fs::path setDir = cache / "sets" / fixture.sha256;
  std::error_code error;
  fs::create_directories(setDir, error);
  if (error || !WriteBytes(setDir / "v1.signed", fixture.signedBytes) ||
      !WriteBytes(setDir / "photo.webp", webp) ||
      !WriteBytes(setDir / "photo.jpg", jpg)) {
    fs::remove_all(cache, error);
    return false;
  }
  ob::json::Value banner;
  if (ob::json::Parse(fixture.json, 32, &banner) != nullptr) {
    fs::remove_all(cache, error);
    return false;
  }
  const ob::json::Value *expires = banner.Find("expires");
  std::int64_t expiresMs = 0;
  if (!expires || expires->type != ob::json::Type::String ||
      !ob::ParseTimestamp(expires->text, &expiresMs)) {
    fs::remove_all(cache, error);
    return false;
  }
  if (!WriteText(
          cache / "record.json",
          StoredRecord(fixture, webp, jpg, expiresMs + 30ll * 86400000ll))) {
    fs::remove_all(cache, error);
    return false;
  }
  ob::Config config = BaseConfig(cache, key);
  config.host.clear();
  bool result = false;
  {
    ob::Client client(config, nullptr, WM_APP + 73);
    result =
        !client.Current().online && RecordCurrentIsNull(cache / "record.json");
  }
  fs::remove_all(cache, error);
  return result;
}

bool RunOversizeRecord(const KeyMaterial &key) {
  const fs::path cache = MakeTemp(L"goat-banner-oversize-record-cache");
  if (cache.empty())
    return false;
  const std::size_t size = 17u * 1024u;
  const bool written = WriteText(cache / "record.json", std::string(size, 'x'));
  ob::Config config = BaseConfig(cache, key);
  config.host.clear();
  bool result = false;
  if (written) {
    ob::Client client(config, nullptr, WM_APP + 73);
    std::error_code error;
    result = !client.Current().online &&
             fs::exists(cache / "record.json", error) &&
             fs::file_size(cache / "record.json", error) == size && !error;
  }
  std::error_code error;
  fs::remove_all(cache, error);
  return result;
}

bool RunPoisonedRecord(INTERNET_PORT port, const KeyMaterial &key) {
  const fs::path cache = MakeTemp(L"goat-banner-poisoned-record-cache");
  if (cache.empty())
    return false;
  const bool written = WriteText(cache / "record.json",
                                 "{\"hwm\":9007199254740992,\"current\":null,"
                                 "\"lastCheckMs\":0,\"failures\":0}");
  ob::Config config = BaseConfig(cache, key);
  config.port = port;
  config.pathPrefix = L"/ok";
  bool result = false;
  if (written) {
    ob::Client client(config, nullptr, WM_APP + 73);
    client.Shown();
    result = PumpUntil(
        [&]() {
          std::int64_t hwm = 0;
          int failures = 0;
          return client.Current().online &&
                 ReadRecord(cache / "record.json", &hwm, &failures) &&
                 hwm == 1 && failures == 0;
        },
        7000);
  }
  std::error_code error;
  fs::remove_all(cache, error);
  return result;
}

bool RunWorkerReaping(INTERNET_PORT port, const KeyMaterial &key) {
  const fs::path cache = MakeTemp(L"goat-banner-worker-cache");
  if (cache.empty())
    return false;
  ob::Config config = BaseConfig(cache, key);
  config.port = port;
  config.pathPrefix = L"/ok";
  bool result = true;
  {
    ob::Client client(config, nullptr, WM_APP + 73);
    DWORD baseline = 0;
    result = ProcessHandleCount(&baseline);
    for (int cycle = 0; cycle < 20 && result; ++cycle) {
      client.Shown();
      client.Hidden();
      PumpFor(25);
      DWORD current = 0;
      result = ProcessHandleCount(&current) && current < baseline + 10u;
    }
  }
  std::error_code error;
  fs::remove_all(cache, error);
  return result;
}

bool RunCapSigned(const KeyMaterial &key, const Fixture &fixture,
                  const Bytes &webp, const Bytes &jpg) {
  const fs::path cache = MakeTemp(L"goat-banner-cap-signed-cache");
  if (cache.empty())
    return false;
  std::string oversized(fixture.signedBytes.begin(), fixture.signedBytes.end());
  oversized.resize(16385u, '\0');
  const fs::path setDir = cache / "sets" / fixture.sha256;
  std::error_code error;
  fs::create_directories(setDir, error);
  const bool seeded =
      !error &&
      WriteText(cache / "record.json",
                StoredRecord(fixture, webp, jpg, NowMs() + 86400000ll)) &&
      WriteText(setDir / "v1.signed", oversized) &&
      WriteBytes(setDir / "photo.webp", webp) &&
      WriteBytes(setDir / "photo.jpg", jpg);
  bool result = false;
  if (seeded) {
    ob::Config config = BaseConfig(cache, key);
    config.host.clear();
    ob::Client client(config, nullptr, WM_APP + 73);
    result = !client.Current().online;
  }
  fs::remove_all(cache, error);
  return result;
}

bool RunCapPhoto(const KeyMaterial &key, const Fixture &fixture,
                 const Bytes &webp, const Bytes &jpg) {
  const fs::path cache = MakeTemp(L"goat-banner-cap-photo-cache");
  if (cache.empty())
    return false;
  Bytes oversized = jpg;
  oversized.resize(300001u, 0);
  const fs::path setDir = cache / "sets" / fixture.sha256;
  std::error_code error;
  fs::create_directories(setDir, error);
  const bool seeded =
      !error &&
      WriteText(cache / "record.json",
                StoredRecord(fixture, webp, jpg, NowMs() + 86400000ll)) &&
      WriteBytes(setDir / "v1.signed", fixture.signedBytes) &&
      WriteBytes(setDir / "photo.webp", webp) &&
      WriteBytes(setDir / "photo.jpg", oversized);
  bool result = false;
  if (seeded) {
    ob::Config config = BaseConfig(cache, key);
    config.host.clear();
    ob::Client client(config, nullptr, WM_APP + 73);
    result = !client.Current().online;
  }
  fs::remove_all(cache, error);
  return result;
}

bool HasTmpSuffix(const fs::path &path) {
  const std::wstring name = path.filename().wstring();
  return name.size() >= 4u && name.compare(name.size() - 4u, 4u, L".tmp") == 0;
}

bool RunAtomicSet(INTERNET_PORT port, const KeyMaterial &key) {
  const fs::path cache = MakeTemp(L"goat-banner-atomic-cache");
  if (cache.empty())
    return false;
  std::error_code error;
  const bool planted =
      WriteText(cache / "record.json.tmp", "stale") &&
      (fs::create_directories(cache / "sets" / "stale", error), !error);
  if (!planted || error ||
      !WriteText(cache / "sets" / "stale" / "v1.signed.tmp", "stale")) {
    fs::remove_all(cache, error);
    return false;
  }
  ob::Config config = BaseConfig(cache, key);
  config.port = port;
  config.pathPrefix = L"/ok";
  bool fetched = false;
  {
    ob::Client client(config, nullptr, WM_APP + 73);
    client.Shown();
    fetched = PumpUntil(
        [&]() {
          std::int64_t hwm = 0;
          int failures = 0;
          return client.Current().online &&
                 ReadRecord(cache / "record.json", &hwm, &failures) &&
                 hwm == 1 && failures == 0;
        },
        7000);
  }
  bool noTemporaryFiles = true;
  for (fs::recursive_directory_iterator it(cache, error), end;
       !error && it != end; ++it) {
    if (HasTmpSuffix(it->path()))
      noTemporaryFiles = false;
  }
  const bool result =
      fetched && noTemporaryFiles && RecordParses(cache / "record.json");
  fs::remove_all(cache, error);
  return result;
}

struct LogEntry {
  std::map<std::string, std::string> headers;
};

std::string Lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });
  return value;
}

std::vector<LogEntry> ReadLog(const fs::path &path) {
  std::vector<LogEntry> entries;
  std::ifstream input(path);
  std::string line;
  while (std::getline(input, line)) {
    ob::json::Value root;
    if (ob::json::Parse(line, 32, &root) != nullptr ||
        root.type != ob::json::Type::Object)
      continue;
    const ob::json::Value *headers = root.Find("headers");
    if (!headers || headers->type != ob::json::Type::Object)
      continue;
    LogEntry entry;
    for (const auto &member : headers->members) {
      if (member.second.type == ob::json::Type::String) {
        entry.headers[Lower(member.first)] = member.second.text;
      }
    }
    entries.push_back(std::move(entry));
  }
  return entries;
}

bool CheckHeaders(const fs::path &log) {
  const std::vector<LogEntry> entries = ReadLog(log);
  std::set<std::string> names;
  bool good = !entries.empty();
  for (const LogEntry &entry : entries) {
    for (const auto &header : entry.headers)
      names.insert(header.first);
    const auto userAgent = entry.headers.find("user-agent");
    if (userAgent == entry.headers.end() ||
        userAgent->second != "GOATCam-Harness/1") {
      good = false;
    }
    for (const char *forbidden :
         {"cookie", "referer", "accept-encoding", "authorization"}) {
      if (entry.headers.find(forbidden) != entry.headers.end())
        good = false;
    }
  }
  std::cout << "HEADER NAMES SEEN:";
  for (const std::string &name : names)
    std::cout << ' ' << name;
  std::cout << '\n';
  return good;
}

HWND MakeBannerWindow(const ob::Config &config, int dpi = 96) {
  ui::goatCamTestConfig(&config);
  ui::registerGoatCamClass(GetModuleHandleW(nullptr));
  const SIZE size = ui::goatCamPreferredSize(dpi);
  HWND hwnd = CreateWindowExW(0, ui::kGoatCamClass, L"", WS_POPUP | WS_TABSTOP,
                              -32000, -32000, size.cx, size.cy, nullptr,
                              nullptr, GetModuleHandleW(nullptr), nullptr);
  if (hwnd) {
    SetWindowPos(hwnd, nullptr, -32000, -32000, size.cx, size.cy,
                 SWP_NOACTIVATE | SWP_NOZORDER);
    ui::goatCamTestSetDpi(hwnd, dpi);
  }
  return hwnd;
}

void DestroyBannerWindow(HWND hwnd) {
  if (hwnd) {
    ui::goatCamVisible(hwnd, false);
    DestroyWindow(hwnd);
  }
  ui::goatCamTestConfig(nullptr);
}

bool WaitBannerOnline(HWND hwnd, DWORD timeoutMs = 7000) {
  return PumpUntil([hwnd]() { return ui::goatCamTestIsOnline(hwnd); },
                   timeoutMs);
}

bool RectInside(const RECT &inner, const RECT &outer) {
  return inner.left >= outer.left && inner.top >= outer.top &&
         inner.right <= outer.right && inner.bottom <= outer.bottom;
}

bool RectNonEmpty(const RECT &rect) {
  return rect.right > rect.left && rect.bottom > rect.top;
}

bool RectsClear(const RECT &left, const RECT &right, int minimum) {
  return left.right + minimum <= right.left ||
         right.right + minimum <= left.left;
}

bool CheckProbeFits(const ui::GoatCamTestLayoutProbe &probe, int dpi,
                    ui::GoatCamTestModelKind kind, bool active) {
  const RECT client{0, 0, probe.control.cx, probe.control.cy};
  const int margin = static_cast<int>(std::ceil(4.0 * dpi / 96.0));
  for (int i = 0; i < probe.textCount; ++i) {
    if (!RectInside(probe.text[i].extent, probe.text[i].box))
      return false;
    if (!RectInside(probe.text[i].extent, client))
      return false;
  }
  if (!RectInside(probe.plate, client))
    return false;
  if (kind == ui::GoatCamTestModelKind::Online) {
    RECT safe = probe.bracket;
    safe.left += margin;
    safe.top += margin;
    safe.right -= margin;
    safe.bottom -= margin;
    if (!RectInside(probe.textColumn, safe) || !RectInside(probe.badge, safe) ||
        !RectInside(probe.iphone, safe) || !RectInside(probe.qr, safe))
      return false;
    if (!RectInside(probe.iphone, client) || !RectInside(probe.qr, client) ||
        !RectsClear(probe.textColumn, probe.iphone,
                    static_cast<int>(std::ceil(6.0 * dpi / 96.0))) ||
        !RectsClear(probe.iphone, probe.qr,
                    static_cast<int>(std::ceil(6.0 * dpi / 96.0))))
      return false;
    if (!RectNonEmpty(probe.headline))
      return false;
  }
  (void)active;
  return true;
}

bool CheckLiveTypography(const ui::GoatCamTestLayoutProbe &probe) {
  for (int i = 0; i < probe.textCount; ++i) {
    const ui::GoatCamTestTextProbe &text = probe.text[i];
    if (text.ellipsized || text.actualSize != text.requestedSize)
      return false;
  }
  return true;
}

bool RunDpi200Fits(INTERNET_PORT port, const KeyMaterial &key,
                   const Fixture &onlineFixture, const fs::path &root) {
  if (!InstallFixture(root, onlineFixture,
                      ReadBytes(root / "banner" / "hero-coffee-800.webp"),
                      ReadBytes(root / "banner" / "hero-coffee-800.jpg")))
    return false;
  const fs::path cache = MakeTemp(L"goat-banner-layout-cache");
  if (cache.empty())
    return false;
  ob::Config config = BaseConfig(cache, key);
  config.port = port;
  config.floor = 2;
  config.kinds = {ob::Kind::Jpg};
  config.pathPrefix = L"/ok";
  HWND hwnd = MakeBannerWindow(config);
  bool result = hwnd != nullptr;
  if (result) {
    ui::goatCamVisible(hwnd, true);
    result = WaitBannerOnline(hwnd);
  }
  for (int dpi : {96, 144, 192}) {
    if (!result)
      break;
    const SIZE size = ui::goatCamPreferredSize(dpi);
    SetWindowPos(hwnd, nullptr, -32000, -32000, size.cx, size.cy,
                 SWP_NOACTIVATE | SWP_NOZORDER);
    ui::goatCamTestSetDpi(hwnd, dpi);
    for (bool active : {false, true}) {
      ui::GoatCamTestLayoutProbe onlineProbe;
      ui::GoatCamTestLayoutProbe offlineProbe;
      result = ui::goatCamTestProbeLayout(
                   hwnd, dpi, ui::GoatCamTestModelKind::Online, active,
                   &onlineProbe) &&
               ui::goatCamTestProbeLayout(
                   hwnd, dpi, ui::GoatCamTestModelKind::DaGoat, active,
                   &offlineProbe) &&
               CheckProbeFits(onlineProbe, dpi,
                              ui::GoatCamTestModelKind::Online, active) &&
               CheckLiveTypography(onlineProbe) &&
               CheckProbeFits(offlineProbe, dpi,
                              ui::GoatCamTestModelKind::DaGoat, active);
      if (!result)
        break;
    }
  }
  DestroyBannerWindow(hwnd);
  std::error_code error;
  fs::remove_all(cache, error);
  return result;
}

bool RunKillswitch(INTERNET_PORT port, const KeyMaterial &key,
                   const Fixture &onlineFixture, const Bytes &webp,
                   const Bytes &jpg, const fs::path &root) {
  const Fixture kill =
      MakeFixture(const_cast<KeyMaterial *>(&key), webp, jpg, 3);
  std::string killJson = kill.json;
  const std::string enabled = "\"enabled\":true";
  const std::size_t enabledPos = killJson.find(enabled);
  if (enabledPos == std::string::npos)
    return false;
  killJson.replace(enabledPos, enabled.size(), "\"enabled\":false");
  KeyMaterial *mutableKey = const_cast<KeyMaterial *>(&key);
  Fixture killFixture = kill;
  killFixture.json = killJson;
  const Bytes signature = Sign(mutableKey, killFixture.json);
  const std::string signedText = Base64(signature) + "\n" + killFixture.json;
  killFixture.signedBytes.assign(signedText.begin(), signedText.end());
  killFixture.sha256 = Hex(Sha256(killFixture.signedBytes));
  if (!InstallFixture(root, onlineFixture, webp, jpg))
    return false;
  const fs::path cache = MakeTemp(L"goat-banner-killswitch-cache");
  if (cache.empty())
    return false;
  ob::Config config = BaseConfig(cache, key);
  config.port = port;
  config.floor = 2;
  config.kinds = {ob::Kind::Jpg};
  config.pathPrefix = L"/ok";
  HWND hwnd = MakeBannerWindow(config);
  bool result = hwnd != nullptr;
  if (result) {
    ui::goatCamVisible(hwnd, true);
    result = WaitBannerOnline(hwnd);
  }
  if (result) {
    SetFocus(hwnd);
    WriteBytes(root / "banner" / "v1.signed", killFixture.signedBytes);
    const fs::path record = cache / "record.json";
    const Bytes recordBytes = ReadBytes(record);
    std::string recordText(recordBytes.begin(), recordBytes.end());
    const std::string marker = "\"lastCheckMs\":";
    const std::size_t markerPos = recordText.find(marker);
    if (markerPos != std::string::npos) {
      const std::size_t valueStart = markerPos + marker.size();
      const std::size_t valueEnd = recordText.find(',', valueStart);
      if (valueEnd != std::string::npos) {
        recordText.replace(valueStart, valueEnd - valueStart, "0");
        WriteText(record, recordText);
      }
    }
    ui::goatCamTestForceCheck(hwnd);
    result =
        PumpUntil([hwnd]() { return !ui::goatCamTestIsOnline(hwnd); }, 7000);
    result = result && (GetFocus() == hwnd);
  }
  DestroyBannerWindow(hwnd);
  std::error_code error;
  fs::remove_all(cache, error);
  InstallFixture(root, onlineFixture, webp, jpg);
  return result;
}

bool RunResumeAfterExpiry(INTERNET_PORT port, const KeyMaterial &key,
                          const Bytes &webp, const Bytes &jpg,
                          const fs::path &root) {
  const Fixture expiring =
      MakeFixture(const_cast<KeyMaterial *>(&key), webp, jpg, 4, 3000);
  if (!InstallFixture(root, expiring, webp, jpg))
    return false;
  const fs::path cache = MakeTemp(L"goat-banner-expiry-cache");
  if (cache.empty())
    return false;
  ob::Config config = BaseConfig(cache, key);
  config.port = port;
  config.floor = 2;
  config.kinds = {ob::Kind::Jpg};
  config.pathPrefix = L"/ok";
  HWND hwnd = MakeBannerWindow(config);
  bool result = hwnd != nullptr;
  if (result) {
    ui::goatCamVisible(hwnd, true);
    result = WaitBannerOnline(hwnd);
  }
  if (result) {
    PumpFor(4000);
    ui::goatCamResumed(hwnd);
    result =
        PumpUntil([hwnd]() { return !ui::goatCamTestIsOnline(hwnd); }, 7000);
  }
  DestroyBannerWindow(hwnd);
  std::error_code error;
  fs::remove_all(cache, error);
  return result;
}

std::wstring g_openedUrl;
void CaptureOpenedUrl(const wchar_t *url) { g_openedUrl = url ? url : L""; }

bool RunClickTargets(INTERNET_PORT port, const KeyMaterial &key,
                     const Fixture &onlineFixture, const Bytes &webp,
                     const Bytes &jpg, const fs::path &root) {
  ui::goatCamTestSetOpenUrl(&CaptureOpenedUrl);
  const fs::path offlineCache = MakeTemp(L"goat-banner-offline-click-cache");
  if (offlineCache.empty())
    return false;
  ob::Config offline = BaseConfig(offlineCache, key);
  offline.host.clear();
  offline.floor = 2;
  offline.kinds = {ob::Kind::Jpg};
  HWND offlineWindow = MakeBannerWindow(offline);
  bool result = offlineWindow != nullptr;
  if (result) {
    ui::goatCamVisible(offlineWindow, true);
    g_openedUrl.clear();
    SendMessageW(offlineWindow, WM_KEYDOWN, VK_RETURN, 0);
    result = g_openedUrl == L"https://dagoat.io";
  }
  DestroyBannerWindow(offlineWindow);
  std::error_code error;
  fs::remove_all(offlineCache, error);

  if (!InstallFixture(root, onlineFixture, webp, jpg))
    result = false;
  const fs::path onlineCache = MakeTemp(L"goat-banner-online-click-cache");
  if (onlineCache.empty())
    return false;
  ob::Config online = BaseConfig(onlineCache, key);
  online.port = port;
  online.floor = 2;
  online.kinds = {ob::Kind::Jpg};
  online.pathPrefix = L"/ok";
  HWND onlineWindow = MakeBannerWindow(online);
  if (!onlineWindow)
    result = false;
  if (onlineWindow) {
    ui::goatCamVisible(onlineWindow, true);
    result = result && WaitBannerOnline(onlineWindow);
    g_openedUrl.clear();
    SendMessageW(onlineWindow, WM_KEYDOWN, VK_SPACE, 0);
    result = result && g_openedUrl == L"https://goatcam.dagoat.io";
  }
  DestroyBannerWindow(onlineWindow);
  fs::remove_all(onlineCache, error);
  ui::goatCamTestSetOpenUrl(nullptr);
  return result;
}

int PngEncoderClsid(CLSID *clsid) {
  UINT count = 0;
  UINT bytes = 0;
  if (gp::GetImageEncodersSize(&count, &bytes) != gp::Ok || bytes == 0)
    return -1;
  std::vector<BYTE> buffer(bytes);
  auto *codecs = reinterpret_cast<gp::ImageCodecInfo *>(buffer.data());
  if (gp::GetImageEncoders(count, bytes, codecs) != gp::Ok)
    return -1;
  for (UINT i = 0; i < count; ++i) {
    if (wcscmp(codecs[i].MimeType, L"image/png") == 0) {
      *clsid = codecs[i].Clsid;
      return 0;
    }
  }
  return -1;
}

bool CapturePng(HWND hwnd, int dpi, bool active, const fs::path &path) {
  const SIZE size = ui::goatCamPreferredSize(dpi);
  SetWindowPos(hwnd, nullptr, -32000, -32000, size.cx, size.cy,
               SWP_NOACTIVATE | SWP_NOZORDER);
  ui::goatCamTestSetDpi(hwnd, dpi);
  ui::goatCamTestSetActive(hwnd, active, true);
  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = size.cx;
  info.bmiHeader.biHeight = -size.cy;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  void *bits = nullptr;
  HDC dc = CreateCompatibleDC(nullptr);
  HBITMAP bitmap =
      CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (!dc || !bitmap || !bits) {
    if (bitmap)
      DeleteObject(bitmap);
    if (dc)
      DeleteDC(dc);
    return false;
  }
  HGDIOBJ old = SelectObject(dc, bitmap);
  SendMessageW(hwnd, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT);
  SelectObject(dc, old);
  CLSID png{};
  bool result = PngEncoderClsid(&png) == 0;
  if (result) {
    gp::Bitmap image(size.cx, size.cy, size.cx * 4, PixelFormat32bppRGB,
                     static_cast<BYTE *>(bits));
    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    result = !error && image.Save(path.c_str(), &png) == gp::Ok;
  }
  DeleteObject(bitmap);
  DeleteDC(dc);
  return result;
}

bool RunQrDecoder(const fs::path &image, const std::string &expected) {
  std::wstring command =
      L"python -I -c \"import cv2,sys; image=cv2.imread(sys.argv[1]); "
      L"text,_,_=cv2.QRCodeDetector().detectAndDecode(image); raise "
      L"SystemExit(0 if text==sys.argv[2] else 1)\" \"" +
      image.wstring() + L"\" \"" +
      std::wstring(expected.begin(), expected.end()) + L"\"";
  std::vector<wchar_t> line(command.begin(), command.end());
  line.push_back(L'\0');
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE,
                      CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process))
    return false;
  WaitForSingleObject(process.hProcess, 10000);
  DWORD exitCode = 1;
  GetExitCodeProcess(process.hProcess, &exitCode);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return exitCode == 0;
}

bool RunQrDecodes(INTERNET_PORT port, const KeyMaterial &key,
                  const Fixture &onlineFixture, const Bytes &webp,
                  const Bytes &jpg, const fs::path &root) {
  if (!InstallFixture(root, onlineFixture, webp, jpg))
    return false;
  const fs::path cache = MakeTemp(L"goat-banner-qr-cache");
  const fs::path captures = MakeTemp(L"goat-banner-qr-captures");
  if (cache.empty() || captures.empty())
    return false;
  ob::Config config = BaseConfig(cache, key);
  config.port = port;
  config.floor = 2;
  config.kinds = {ob::Kind::Jpg};
  config.pathPrefix = L"/ok";
  HWND hwnd = MakeBannerWindow(config);
  bool result = hwnd != nullptr;
  if (result) {
    ui::goatCamVisible(hwnd, true);
    result = WaitBannerOnline(hwnd);
  }
  const std::string expected = "https://apps.apple.com/app/id6816268316";
  for (int dpi : {96, 144, 192}) {
    for (bool active : {false, true}) {
      if (!result)
        break;
      const fs::path file =
          captures /
          (std::to_wstring(dpi) + (active ? L"-hover.png" : L"-rest.png"));
      const bool captured = CapturePng(hwnd, dpi, active, file);
      const bool decoded = captured && RunQrDecoder(file, expected);
      result = captured && decoded;
      ui::GoatCamTestLayoutProbe probe;
      result = result &&
               ui::goatCamTestProbeLayout(hwnd, dpi,
                                          ui::GoatCamTestModelKind::Online,
                                          active, &probe) &&
               RectNonEmpty(probe.headline) && probe.textCount > 0 &&
               RectNonEmpty(probe.text[0].extent);
    }
  }
  DestroyBannerWindow(hwnd);
  std::error_code error;
  fs::remove_all(cache, error);
  fs::remove_all(captures, error);
  return result;
}

bool RunBannerShots(INTERNET_PORT port, const KeyMaterial &key,
                    const Fixture &onlineFixture, const Bytes &webp,
                    const Bytes &jpg, const fs::path &root,
                    const fs::path &outdir) {
  if (!InstallFixture(root, onlineFixture, webp, jpg))
    return false;
  std::error_code error;
  fs::create_directories(outdir, error);
  if (error)
    return false;

  const fs::path onlineCache = MakeTemp(L"goat-banner-shots-online-cache");
  ob::Config online = BaseConfig(onlineCache, key);
  online.port = port;
  online.floor = 2;
  online.kinds = {ob::Kind::Jpg};
  online.pathPrefix = L"/ok";
  HWND onlineWindow = MakeBannerWindow(online);
  bool result = onlineWindow != nullptr;
  if (result) {
    ui::goatCamVisible(onlineWindow, true);
    result = WaitBannerOnline(onlineWindow);
  }
  int written = 0;
  for (int dpi : {96, 144, 192}) {
    for (bool active : {false, true}) {
      if (!result)
        break;
      const fs::path path =
          outdir / (std::wstring(L"online-") + std::to_wstring(dpi) +
                    (active ? L"-hover.png" : L"-rest.png"));
      if (!CapturePng(onlineWindow, dpi, active, path))
        result = false;
      else
        ++written;
    }
  }
  DestroyBannerWindow(onlineWindow);
  fs::remove_all(onlineCache, error);

  const fs::path offlineCache = MakeTemp(L"goat-banner-shots-offline-cache");
  ob::Config offline = BaseConfig(offlineCache, key);
  offline.host.clear();
  offline.floor = 2;
  offline.kinds = {ob::Kind::Jpg};
  HWND offlineWindow = MakeBannerWindow(offline);
  if (!offlineWindow)
    result = false;
  if (offlineWindow)
    ui::goatCamVisible(offlineWindow, true);
  for (int dpi : {96, 144, 192}) {
    for (bool active : {false, true}) {
      if (!result)
        break;
      const fs::path path =
          outdir / (std::wstring(L"dagoat-") + std::to_wstring(dpi) +
                    (active ? L"-hover.png" : L"-rest.png"));
      if (!CapturePng(offlineWindow, dpi, active, path))
        result = false;
      else
        ++written;
    }
  }
  DestroyBannerWindow(offlineWindow);
  fs::remove_all(offlineCache, error);
  return result && written == 12;
}

} // namespace

int main(int argc, char **argv) {
  const fs::path executable = fs::absolute(fs::path(argv[0]));
  const bool shotsMode =
      argc > 1 && argv[1] != nullptr && std::strcmp(argv[1], "--shots") == 0;
  const fs::path repo =
      !shotsMode && argc > 1 && argv[1] != nullptr && argv[1][0] != '\0'
          ? fs::absolute(fs::path(argv[1]))
          : executable.parent_path().parent_path();
  const fs::path shotsDir = shotsMode && argc > 2 && argv[2] != nullptr
                                ? fs::absolute(fs::path(argv[2]))
                                : fs::path();
  const fs::path assetDir = repo / "tests" / "banner-vectors" / "photos";
  const fs::path script = repo / "tests" / "serve.py";
  const Bytes webp = ReadBytes(assetDir / "hero-coffee-800.webp");
  const Bytes jpg = ReadBytes(assetDir / "hero-coffee-800.jpg");
  KeyMaterial key;
  int failures = 0;
  if (webp.empty() || jpg.empty() || !MakeKey(&key)) {
    std::cout << "BANNER CLIENT HARNESS: FAIL=1\n";
    return 1;
  }

  const Fixture revision1 = MakeFixture(&key, webp, jpg, 1);
  const Fixture revision2 = MakeFixture(&key, webp, jpg, 2);
  const fs::path serverRoot = MakeTemp(L"goat-banner-server");
  if (serverRoot.empty() || !InstallFixture(serverRoot, revision1, webp, jpg)) {
    std::cout << "BANNER CLIENT HARNESS: FAIL=1\n";
    return 1;
  }
  const fs::path log = serverRoot / "requests.log";
  Server server;
  if (!server.Start(script, serverRoot, log)) {
    std::cout << "BANNER CLIENT HARNESS: FAIL=1\n";
    return 1;
  }

  if (shotsMode) {
    const bool shotsPassed =
        !shotsDir.empty() && RunBannerShots(server.port(), key, revision2, webp,
                                            jpg, serverRoot, shotsDir);
    server.Stop();
    std::error_code shotsError;
    fs::remove_all(serverRoot, shotsError);
    std::cout << (shotsPassed ? "12 PNGs written\n"
                              : "12 PNGs written: FAIL\n");
    return shotsPassed ? 0 : 1;
  }

  const std::vector<std::pair<const char *, bool>> cases = {
      {"ok", true},         {"drip", false},
      {"redirect", false},  {"redirect-cross", false},
      {"gzip", false},      {"bigheaders", false},
      {"status500", false},
  };
  for (const auto &test : cases) {
    const bool passed = RunMode(key, server.port(), test.first, test.second);
    std::cout << test.first << ": " << (passed ? "PASS" : "FAIL") << '\n';
    if (!passed)
      ++failures;
  }
  const bool hiddenPassed = RunHiddenMidAttempt(server.port(), key, log);
  std::cout << "hidden-mid-attempt: " << (hiddenPassed ? "PASS" : "FAIL")
            << '\n';
  if (!hiddenPassed)
    ++failures;

  const bool crashPassed =
      RunCrashSimulation(server.port(), key, revision1, revision2, webp, jpg);
  std::cout << "crash-simulation: " << (crashPassed ? "PASS" : "FAIL") << '\n';
  if (!crashPassed)
    ++failures;
  const bool tamperedExpiresPassed =
      RunStartupTamperedExpires(key, revision1, webp, jpg);
  std::cout << "startup-tampered-expires: "
            << (tamperedExpiresPassed ? "PASS" : "FAIL") << '\n';
  if (!tamperedExpiresPassed)
    ++failures;
  const bool oversizeRecordPassed = RunOversizeRecord(key);
  std::cout << "oversize-record: " << (oversizeRecordPassed ? "PASS" : "FAIL")
            << '\n';
  if (!oversizeRecordPassed)
    ++failures;
  const bool capSignedPassed = RunCapSigned(key, revision1, webp, jpg);
  std::cout << "cap-signed: " << (capSignedPassed ? "PASS" : "FAIL") << '\n';
  if (!capSignedPassed)
    ++failures;
  const bool capPhotoPassed = RunCapPhoto(key, revision1, webp, jpg);
  std::cout << "cap-photo: " << (capPhotoPassed ? "PASS" : "FAIL") << '\n';
  if (!capPhotoPassed)
    ++failures;
  const bool atomicSetPassed = RunAtomicSet(server.port(), key);
  std::cout << "atomic-set: " << (atomicSetPassed ? "PASS" : "FAIL") << '\n';
  if (!atomicSetPassed)
    ++failures;
  const bool poisonedRecordPassed = RunPoisonedRecord(server.port(), key);
  std::cout << "poisoned-record: " << (poisonedRecordPassed ? "PASS" : "FAIL")
            << '\n';
  if (!poisonedRecordPassed)
    ++failures;
  const bool workerReapingPassed = RunWorkerReaping(server.port(), key);
  std::cout << "worker-reaping: " << (workerReapingPassed ? "PASS" : "FAIL")
            << '\n';
  if (!workerReapingPassed)
    ++failures;
  const bool headersPassed = CheckHeaders(log);
  std::cout << "headers: " << (headersPassed ? "PASS" : "FAIL") << '\n';
  if (!headersPassed)
    ++failures;

  const bool dpiFitsPassed =
      RunDpi200Fits(server.port(), key, revision2, serverRoot);
  std::cout << "dpi-200-fits: " << (dpiFitsPassed ? "PASS" : "FAIL") << '\n';
  if (!dpiFitsPassed)
    ++failures;
  const bool killswitchPassed =
      RunKillswitch(server.port(), key, revision2, webp, jpg, serverRoot);
  std::cout << "killswitch-under-focus: "
            << (killswitchPassed ? "PASS" : "FAIL") << '\n';
  if (!killswitchPassed)
    ++failures;
  const bool expiryPassed =
      RunResumeAfterExpiry(server.port(), key, webp, jpg, serverRoot);
  std::cout << "resume-after-expiry: " << (expiryPassed ? "PASS" : "FAIL")
            << '\n';
  if (!expiryPassed)
    ++failures;
  const bool clicksPassed =
      RunClickTargets(server.port(), key, revision2, webp, jpg, serverRoot);
  std::cout << "click-targets: " << (clicksPassed ? "PASS" : "FAIL") << '\n';
  if (!clicksPassed)
    ++failures;
  const bool qrPassed =
      RunQrDecodes(server.port(), key, revision2, webp, jpg, serverRoot);
  std::cout << "qr-decodes: " << (qrPassed ? "PASS" : "FAIL") << '\n';
  if (!qrPassed)
    ++failures;

  server.Stop();
  std::error_code error;
  fs::remove_all(serverRoot, error);
  std::cout << "BANNER CLIENT HARNESS: FAIL=" << failures << '\n';
  return failures == 0 ? 0 : 1;
}
