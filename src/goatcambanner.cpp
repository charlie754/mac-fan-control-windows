// goatcambanner.cpp - built-in DaGoat banner and the signed online GOAT Cam
// model.
//
// DRAWING: the control paints a photo-backed GOAT Cam banner when the signed
// model is online, and a vector DaGoat lockup when it is offline. On hover or
// focus the plate's stage (badge, wordmark, slogan rows, iPhone and QR; the goat
// lockup offline) grows about the plate centre while the brackets close in; the
// copy above the plate never moves. The animation timer is created only while
// hover/focus/spark transitions run, so the banner at rest costs nothing.

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "goatcambanner.h"

#include "banner_key.h"
#include "online_banner_client.h"
#include "version.h"

#include <commctrl.h>
#include <objidl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <initializer_list>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Gdiplus {
using std::max;
using std::min;
} // namespace Gdiplus
#include <gdiplus.h>

namespace ui {

const wchar_t *kGoatCamClass = L"MacFanCtlGoatCam";

namespace {

namespace gp = Gdiplus;
namespace fs = std::filesystem;

constexpr UINT kModelChanged = WM_APP + 0x52;
constexpr UINT_PTR kAnimationTimer = 1;
constexpr UINT kAnimationFrameMs = 15;
constexpr int kQrN = 37;

constexpr double kControlH = 202.0;
constexpr double kOnlinePlateTop = 26.0;
constexpr double kOnlinePlateH = 170.0;
constexpr double kOfflinePlateTop = 55.0;
constexpr double kOfflinePlateH = 141.0;
constexpr double kPlateRadius = 10.0;
constexpr double kBracketRest = 6.0;
constexpr double kBracketTight = 9.0;
constexpr double kStageScale = 1.05;
constexpr double kPhotoScale = 1.06;

struct Rgb {
  BYTE r, g, b;
};
constexpr Rgb kEspresso950{0x14, 0x0d, 0x08};
constexpr Rgb kEspresso900{0x1c, 0x13, 0x0c};
constexpr Rgb kCaramel{0xc9, 0x8a, 0x46};
constexpr Rgb kCaramelDeep{0x8f, 0x4f, 0x1c};
constexpr Rgb kGold{0xf0, 0xc0, 0x7a};
constexpr Rgb kCream{0xf8, 0xee, 0xdf};
constexpr Rgb kQrLight{0xfb, 0xf5, 0xea};
constexpr Rgb kQrDark{0x1c, 0x13, 0x0c};
constexpr Rgb kSealRed{0xb8, 0x32, 0x2a};
constexpr Rgb kMutedNeutral{0x6b, 0x65, 0x60};
constexpr Rgb kBlack{0x00, 0x00, 0x00};

const wchar_t *const kOfflineLabel = L"Goat Project — open dagoat.io";
const char *const kOfflineUrl = "https://dagoat.io";
const wchar_t *const kDefaultHeadline =
    L"GOAT Cam \u2014 your iPhone, NOW your PC\u2019s webcam.";
const wchar_t *const kDefaultWordmark = L"GOAT Cam";
const wchar_t *const kDefaultBadge = L"1440p \u00b7 60 fps";
const wchar_t *const kDefaultTagline0 = L"Your iPhone, NOW";
const wchar_t *const kDefaultTagline1 = L"your PC’s webcam.";

// The fallback is the founder-approved 37-module App Store code. A valid online
// model replaces it with its signed rows.
constexpr const char *kDefaultQr[] = {
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

bool g_gdiplusOk = false;
ULONG_PTR g_gdiplusToken = 0;
HINSTANCE g_inst = nullptr;
const ob::Config *g_testConfig = nullptr;
void (*g_openUrl)(const wchar_t *) = nullptr;
gp::Bitmap *g_iphone = nullptr;
bool g_iphoneTried = false;

inline double clamp01(double value) {
  return value < 0.0 ? 0.0 : (value > 1.0 ? 1.0 : value);
}
inline BYTE chan(double value) {
  const long n = std::lround(value);
  return static_cast<BYTE>(n < 0 ? 0 : (n > 255 ? 255 : n));
}
inline COLORREF cr(Rgb color) { return RGB(color.r, color.g, color.b); }
inline gp::Color gc(Rgb color, double alpha = 1.0) {
  return gp::Color(chan(clamp01(alpha) * 255.0), color.r, color.g, color.b);
}
inline int px(double design, int dpi) {
  return static_cast<int>(std::lround(design * dpi / 96.0));
}
inline float pxf(double design, int dpi) {
  return static_cast<float>(design * dpi / 96.0);
}

void addRoundRect(gp::GraphicsPath &path, const gp::RectF &r, float radius) {
  radius = std::min(radius, std::min(r.Width, r.Height) / 2.0f);
  if (radius <= 0.0f) {
    path.AddRectangle(r);
    return;
  }
  const float diameter = radius * 2.0f;
  path.StartFigure();
  path.AddArc(r.X, r.Y, diameter, diameter, 180.0f, 90.0f);
  path.AddArc(r.X + r.Width - diameter, r.Y, diameter, diameter, 270.0f, 90.0f);
  path.AddArc(r.X + r.Width - diameter, r.Y + r.Height - diameter, diameter,
              diameter, 0.0f, 90.0f);
  path.AddArc(r.X, r.Y + r.Height - diameter, diameter, diameter, 90.0f, 90.0f);
  path.CloseFigure();
}

gp::RectF rectF(const RECT &r) {
  return gp::RectF(static_cast<float>(r.left), static_cast<float>(r.top),
                   static_cast<float>(r.right - r.left),
                   static_cast<float>(r.bottom - r.top));
}

RECT transformRect(const RECT &r, double scale, double cx, double cy) {
  return RECT{static_cast<LONG>(std::lround(cx + (r.left - cx) * scale)),
              static_cast<LONG>(std::lround(cy + (r.top - cy) * scale)),
              static_cast<LONG>(std::lround(cx + (r.right - cx) * scale)),
              static_cast<LONG>(std::lround(cy + (r.bottom - cy) * scale))};
}

std::wstring utf8Wide(const std::string &text) {
  if (text.empty())
    return {};
  const int needed =
      MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                          static_cast<int>(text.size()), nullptr, 0);
  if (needed <= 0)
    return {};
  std::wstring result(static_cast<std::size_t>(needed), L'\0');
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                          static_cast<int>(text.size()), result.data(),
                          needed) != needed)
    return {};
  return result;
}

HFONT makeFont(int style, int height, int weight) {
  const wchar_t *face = L"Segoe UI";
  if (style == 2 || style == 3)
    face = L"Georgia";
  if (style == 4)
    face = L"Consolas";
  if (style == 5)
    face = L"Microsoft YaHei UI";
  if (style == 6)
    face = L"SimSun";
  return CreateFontW(-std::max(1, height), 0, 0, 0, weight,
                     style == 1 || style == 3 ? TRUE : FALSE, FALSE, FALSE,
                     DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                     CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
}

struct FontCache {
  std::map<std::uint64_t, HFONT> fonts;
  ~FontCache() {
    for (const auto &item : fonts)
      DeleteObject(item.second);
  }
  HFONT get(int style, int height, int weight = FW_NORMAL) {
    const std::uint64_t key = (static_cast<std::uint64_t>(style) << 48) |
                              (static_cast<std::uint64_t>(weight) << 32) |
                              static_cast<std::uint32_t>(height);
    const auto found = fonts.find(key);
    if (found != fonts.end())
      return found->second;
    HFONT font = makeFont(style, height, weight);
    fonts.emplace(key, font);
    return font;
  }
};

int measureText(HDC dc, HFONT font, const std::wstring &text,
                int tracking = 0) {
  if (!dc || !font)
    return 0;
  HGDIOBJ old = SelectObject(dc, font);
  const int oldExtra = SetTextCharacterExtra(dc, tracking);
  SIZE size{};
  GetTextExtentPoint32W(dc, text.data(), static_cast<int>(text.size()), &size);
  SetTextCharacterExtra(dc, oldExtra);
  SelectObject(dc, old);
  return size.cx;
}

int textHeight(HDC dc, HFONT font) {
  if (!dc || !font)
    return 0;
  HGDIOBJ old = SelectObject(dc, font);
  TEXTMETRICW metrics{};
  GetTextMetricsW(dc, &metrics);
  SelectObject(dc, old);
  return metrics.tmHeight;
}

double naturalTextWidth(HDC dc, FontCache &cache, const std::wstring &text,
                        int style, double designSize, int dpi,
                        int weight = FW_NORMAL, int tracking = 0) {
  const int size =
      std::max(1, static_cast<int>(std::lround(designSize * dpi / 96.0)));
  const int measured =
      measureText(dc, cache.get(style, size, weight), text, tracking);
  return dpi > 0 ? measured * 96.0 / dpi : 0.0;
}

struct Fit {
  std::wstring text;
  HFONT font = nullptr;
  int width = 0;
  int height = 0;
  int style = 0;
  int size = 0;
};

Fit fitText(HDC dc, FontCache &cache, const std::wstring &source, int style,
            int size, int maxWidth, int weight = FW_NORMAL, int tracking = 0) {
  Fit result;
  result.text = source;
  result.style = style;
  result.size = std::max(1, size);
  result.font = cache.get(style, result.size, weight);
  result.width = measureText(dc, result.font, result.text, tracking);
  if (result.width > maxWidth) {
    result.size =
        std::max(1, static_cast<int>(std::lround(result.size * 0.85)));
    result.font = cache.get(style, result.size, weight);
    result.width = measureText(dc, result.font, result.text, tracking);
    if (result.width > maxWidth) {
      for (std::size_t count = source.size();; --count) {
        const std::wstring candidate = source.substr(0, count) + L"…";
        if (measureText(dc, result.font, candidate, tracking) <= maxWidth ||
            count == 0) {
          result.text = candidate;
          result.width = measureText(dc, result.font, result.text, tracking);
          break;
        }
      }
    }
  }
  result.height = textHeight(dc, result.font);
  return result;
}

// -------------------------------------------------------------------------
// DaGoat mark: the v1 Game Optimizer paths, retained as vectors.

struct PtF {
  double x = 0.0, y = 0.0;
};
using SubPath = std::vector<PtF>;
using Contours = std::vector<SubPath>;

class PathReader {
public:
  explicit PathReader(const char *text) : text_(text) {}
  Contours run() {
    Contours output;
    char command = 0;
    while (true) {
      skip();
      if (*text_ == '\0')
        break;
      if (isAlpha(*text_))
        command = *text_++;
      else if (command == 'M')
        command = 'L';
      else if (command == 'm')
        command = 'l';
      else
        break;
      if (!read(command, output))
        break;
    }
    flush(output);
    return output;
  }

private:
  static bool isAlpha(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
  }
  static bool isSep(char c) {
    return c == ' ' || c == ',' || c == '\t' || c == '\r' || c == '\n';
  }
  void skip() {
    while (*text_ && isSep(*text_))
      ++text_;
  }
  bool number(double &value) {
    skip();
    char *end = nullptr;
    value = std::strtod(text_, &end);
    if (!end || end == text_)
      return false;
    text_ = end;
    return true;
  }
  void emit(double x, double y) {
    current_ = {x, y};
    open_.push_back(current_);
  }
  void flush(Contours &output) {
    if (open_.size() >= 2)
      output.push_back(open_);
    open_.clear();
  }
  void cubic(double x1, double y1, double x2, double y2, double x, double y) {
    const double x0 = current_.x, y0 = current_.y;
    for (int i = 1; i <= 20; ++i) {
      const double t = i / 20.0, u = 1.0 - t;
      emit(u * u * u * x0 + 3 * u * u * t * x1 + 3 * u * t * t * x2 +
               t * t * t * x,
           u * u * u * y0 + 3 * u * u * t * y1 + 3 * u * t * t * y2 +
               t * t * t * y);
    }
    cubicControl_ = {x2, y2};
    hasCubic_ = true;
    hasQuad_ = false;
  }
  void quad(double x1, double y1, double x, double y) {
    const double x0 = current_.x, y0 = current_.y;
    for (int i = 1; i <= 14; ++i) {
      const double t = i / 14.0, u = 1.0 - t;
      emit(u * u * x0 + 2 * u * t * x1 + t * t * x,
           u * u * y0 + 2 * u * t * y1 + t * t * y);
    }
    quadControl_ = {x1, y1};
    hasQuad_ = true;
    hasCubic_ = false;
  }
  bool read(char command, Contours &output) {
    const bool relative = command >= 'a' && command <= 'z';
    const char c = relative ? static_cast<char>(command - 32) : command;
    const double bx = relative ? current_.x : 0.0;
    const double by = relative ? current_.y : 0.0;
    double a[6]{};
    switch (c) {
    case 'M':
      if (!number(a[0]) || !number(a[1]))
        return false;
      flush(output);
      start_ = {bx + a[0], by + a[1]};
      current_ = start_;
      open_.push_back(current_);
      hasCubic_ = hasQuad_ = false;
      return true;
    case 'L':
      if (!number(a[0]) || !number(a[1]))
        return false;
      emit(bx + a[0], by + a[1]);
      hasCubic_ = hasQuad_ = false;
      return true;
    case 'H':
      if (!number(a[0]))
        return false;
      emit(bx + a[0], current_.y);
      hasCubic_ = hasQuad_ = false;
      return true;
    case 'V':
      if (!number(a[0]))
        return false;
      emit(current_.x, by + a[0]);
      hasCubic_ = hasQuad_ = false;
      return true;
    case 'C':
      for (int i = 0; i < 6; ++i)
        if (!number(a[i]))
          return false;
      cubic(bx + a[0], by + a[1], bx + a[2], by + a[3], bx + a[4], by + a[5]);
      return true;
    case 'Q':
      for (int i = 0; i < 4; ++i)
        if (!number(a[i]))
          return false;
      quad(bx + a[0], by + a[1], bx + a[2], by + a[3]);
      return true;
    case 'Z':
      if (!open_.empty()) {
        emit(start_.x, start_.y);
        flush(output);
      }
      current_ = start_;
      open_.push_back(start_);
      hasCubic_ = hasQuad_ = false;
      return true;
    default:
      return false;
    }
  }
  const char *text_;
  PtF current_{}, start_{}, cubicControl_{}, quadControl_{};
  bool hasCubic_ = false, hasQuad_ = false;
  SubPath open_;
};

namespace art {
const char *const kHead[5] = {
    "M62 210 C74 148 120 98 190 80 C152 106 106 150 82 216 Z",
    "M186 84 C214 62 226 32 210 8 C216 36 200 62 174 80 Z",
    "M170 82 C190 60 194 34 180 16 C186 40 172 62 156 78 Z",
    "M188 82 C206 90 218 104 222 122 C212 106 200 94 182 90 Z",
    "M206 122 C215 134 217 149 209 160 C212 147 208 133 200 127 Z"};
constexpr double kEyeCx = 197.0, kEyeCy = 99.0, kEyeR = 3.6;
constexpr double kVbX = 46.0, kVbY = -16.0, kVbH = 250.0;
constexpr double kSealX = 214.0, kSealY = 182.0, kSealW = 36.0, kSealRx = 4.0,
                 kInner = 0.36;
const char *const kThumb =
    "M30 50 C26 50 24 52 24 56 L24 87 C24 91 26 93 30 93 L73 93 C79 93 84 90 "
    "85 84 L89 55 C90 49 86 44 80 44 L63 44 C65 38 67 25 65 18 C63 12 55 12 53 "
    "18 C51 24 53 33 51 42 C50 47 47 50 42 50 Z";
const char *const kChar =
    "M33.5 18.12Q39.14 21.51 41.54 23.62Q43.93 25.74 43.65 27.85Q43.37 29.97 "
    "42.24 30.96Q41.11 31.94 40.55 31.94Q39.42 31.94 38.57 29.12Q37.45 25.17 "
    "32.93 18.97Z M20.52 33.07H52.68Q58.89 23.76 60.86 16.99L66.79 20.66Q63.96 "
    "22.07 61.14 25.74Q58.04 29.41 54.37 33.07H68.2L72.43 28.84L77.79 "
    "34.77H51.55V47.46H63.96L68.2 43.79L72.99 49.15H51.55V62.13H71.58L76.1 "
    "57.9L81.46 63.82H51.55V73.7Q51.55 76.8 51.83 81.32L47.04 83.01Q47.32 "
    "78.78 47.32 63.82H27.85Q24.19 63.82 21.08 64.67L18.54 "
    "62.13H47.32V49.15H35.47Q31.8 49.15 28.7 50L26.16 "
    "47.46H47.32V34.77H29.83Q26.16 34.77 23.06 35.61Z";
constexpr double kBurstCx = 53.0, kBurstCy = 0.0, kRingR = 12.0, kRayR0 = 10.0,
                 kRayR1 = 40.0;
constexpr double kRayDeg[6] = {-168.0, -138.0, -108.0, -76.0, -44.0, -14.0};
} // namespace art

void addContours(gp::GraphicsPath &path, const Contours &contours, double sx,
                 double sy, double tx, double ty) {
  for (const SubPath &sub : contours) {
    if (sub.size() < 3)
      continue;
    std::vector<gp::PointF> points;
    points.reserve(sub.size());
    for (const PtF &p : sub)
      points.emplace_back(static_cast<float>(p.x * sx + tx),
                          static_cast<float>(p.y * sy + ty));
    path.StartFigure();
    path.AddLines(points.data(), static_cast<INT>(points.size()));
    path.CloseFigure();
  }
}

struct Art {
  int dpi = 0;
  bool ok = false;
  float s = 0.0f, inner = 0.0f;
  std::unique_ptr<gp::GraphicsPath> head, chr, thumb, seal;
  gp::RectF sealRect;
  gp::PointF burst;
  void build(int d) {
    dpi = d;
    ok = false;
    head.reset();
    chr.reset();
    thumb.reset();
    seal.reset();
    s = pxf(76.0, d) / static_cast<float>(art::kVbH);
    inner = s * static_cast<float>(art::kInner);
    const double ox = -art::kVbX * s, oy = -art::kVbY * s;
    sealRect = gp::RectF(static_cast<float>(art::kSealX * s + ox),
                         static_cast<float>(art::kSealY * s + oy),
                         static_cast<float>(art::kSealW * s),
                         static_cast<float>(art::kSealW * s));
    burst = gp::PointF(sealRect.X + static_cast<float>(art::kBurstCx * inner),
                       sealRect.Y + static_cast<float>(art::kBurstCy * inner));
    if (!g_gdiplusOk)
      return;
    head.reset(new gp::GraphicsPath(gp::FillModeWinding));
    seal.reset(new gp::GraphicsPath());
    chr.reset(new gp::GraphicsPath(gp::FillModeWinding));
    thumb.reset(new gp::GraphicsPath(gp::FillModeWinding));
    if (!head || !seal || !chr || !thumb)
      return;
    for (const char *path : art::kHead)
      addContours(*head, PathReader(path).run(), s, s, ox, oy);
    head->AddEllipse(static_cast<float>((art::kEyeCx - art::kEyeR) * s + ox),
                     static_cast<float>((art::kEyeCy - art::kEyeR) * s + oy),
                     static_cast<float>(2 * art::kEyeR * s),
                     static_cast<float>(2 * art::kEyeR * s));
    addRoundRect(*seal, sealRect, static_cast<float>(art::kSealRx * s));
    addContours(*chr, PathReader(art::kChar).run(), inner, inner, sealRect.X,
                sealRect.Y);
    addContours(*thumb, PathReader(art::kThumb).run(), inner, inner,
                sealRect.X - 5 * inner, sealRect.Y - 3 * inner);
    ok = true;
  }
};

struct Tween {
  double from = 0.0, to = 0.0, t0 = 0.0, duration = 0.0;
  double at(double now) const {
    if (duration <= 0.0)
      return to;
    const double t = (now - t0) / duration;
    if (t <= 0.0)
      return from;
    if (t >= 1.0)
      return to;
    const double u = 1.0 - t;
    return from + (to - from) * (3 * u * u * t + 3 * u * t * t + t * t * t);
  }
  bool busy(double now) const { return duration > 0.0 && now < t0 + duration; }
  void go(double now, double target, double milliseconds) {
    from = at(now);
    to = target;
    t0 = now;
    duration = milliseconds * std::fabs(target - from);
    if (duration < 1.0) {
      from = to;
      duration = 0.0;
    }
  }
  void settle(double value) {
    from = to = value;
    duration = 0.0;
  }
};

double nowMs() {
  static const double perMs = [] {
    LARGE_INTEGER f{};
    QueryPerformanceFrequency(&f);
    return static_cast<double>(f.QuadPart) / 1000.0;
  }();
  LARGE_INTEGER c{};
  QueryPerformanceCounter(&c);
  return static_cast<double>(c.QuadPart) / perMs;
}

struct Motion {
  Tween active;
  double sparkAt = 0.0;
  bool target = false;
  void setActive(bool on, double now) {
    if (on == target)
      return;
    target = on;
    active.go(now, on ? 1.0 : 0.0, on ? 420.0 : 360.0);
    sparkAt = on ? now : 0.0;
  }
  void settle(bool on) {
    target = on;
    active.settle(on ? 1.0 : 0.0);
    sparkAt = 0.0;
  }
  bool busy(double now) const {
    return active.busy(now) || (sparkAt > 0.0 && now < sparkAt + 720.0);
  }
};

enum class TextRole {
  Headline,
  Badge,
  Wordmark,
  Tagline,
  OfflinePrimary,
  OfflineSecondary,
  GoatWordmark,
  GoatTag
};
struct TextItem {
  TextRole role{};
  std::wstring text, em;
  RECT box{}, extent{};
  Fit fit;
  int requestedSize = 0;
  int tracking = 0;
};

struct Layout {
  int dpi = 96, w = 0, h = 0;
  bool online = false;
  double stage = 1.0;
  RECT plate{}, bracket{}, textColumn{}, badge{}, iphone{}, qr{}, headline{},
      mark{};
  std::vector<TextItem> texts;
  static RECT designRect(double x, double y, double width, double height,
                         int dpi) {
    return RECT{px(x, dpi), px(y, dpi), px(x + width, dpi),
                px(y + height, dpi)};
  }
  void add(HDC dc, FontCache &cache, TextRole role, const std::wstring &source,
           const std::wstring &em, RECT box, double fontDesign, int style,
           int weight = FW_NORMAL, int tracking = 0) {
    if (source.empty())
      return;
    TextItem item;
    item.role = role;
    item.text = source;
    item.em = em;
    item.box = box;
    item.tracking = tracking;
    // Only plate content grows with the hover stage; the copy above the plate
    // (headline, DaGoat copy lines) keeps its size so it never shifts.
    const bool abovePlate = role == TextRole::Headline ||
                            role == TextRole::OfflinePrimary ||
                            role == TextRole::OfflineSecondary;
    const double grow = abovePlate ? 1.0 : stage;
    item.requestedSize = std::max(
        1, static_cast<int>(std::lround(fontDesign * dpi / 96.0 * grow)));
    item.fit = fitText(dc, cache, source, style, item.requestedSize,
                       box.right - box.left, weight, tracking);
    item.extent = RECT{box.left, box.top, box.left + item.fit.width,
                       box.top + item.fit.height};
    texts.push_back(std::move(item));
  }

  RECT stageRect(double x, double y, double width, double height, double cx,
                 double cy) const {
    return transformRect(designRect(x, y, width, height, dpi), stage, cx, cy);
  }

  void buildOnline(HDC dc, FontCache &cache, const ob::Banner *banner,
                   double cx, double cy) {
    const double dpiScale = dpi / 96.0;
    const std::wstring headlineText = banner && !banner->headline.empty()
                                          ? banner->headline
                                          : kDefaultHeadline;
    const std::wstring headlineEm =
        banner && banner->hasHeadlineEm ? banner->headlineEm : L"";
    headline = RECT{0, 0, w, px(20.0, dpi)};
    add(dc, cache, TextRole::Headline, headlineText, headlineEm, headline, 13.5,
        0, FW_SEMIBOLD);

    const std::wstring word = banner && !banner->wordmark.empty()
                                  ? banner->wordmark
                                  : kDefaultWordmark;
    const std::wstring badgeText =
        banner && banner->hasBadge ? banner->badge : kDefaultBadge;
    std::vector<std::wstring> tags =
        banner ? banner->tagline : std::vector<std::wstring>{};
    if (tags.empty())
      tags = {kDefaultTagline0, kDefaultTagline1};

    constexpr double textLeft = 34.0;
    constexpr double qrLeft = 300.0;
    constexpr double phoneNominalWidth = 62.0;
    constexpr double phoneMinimumWidth = 48.0;
    const double qrDesign =
        std::max(2, static_cast<int>(std::lround(2.0 * dpiScale))) * kQrN /
        dpiScale;
    const double badgeTextWidth =
        naturalTextWidth(dc, cache, badgeText, 4, 8.5, dpi);
    const double wordWidth = naturalTextWidth(dc, cache, word, 2, 30.0, dpi);
    const double tag0Width = naturalTextWidth(dc, cache, tags[0], 2, 15.0, dpi);
    const double tag1Width =
        tags.size() > 1 ? naturalTextWidth(dc, cache, tags[1], 2, 15.0, dpi)
                        : 0.0;
    const double badgeWidth = badgeTextWidth + 26.0;
    const double naturalColumnWidth =
        std::max({wordWidth, tag0Width, tag1Width, badgeWidth}) + 4.0;
    const double maxColumnWidth = qrLeft - 12.0 - phoneMinimumWidth - textLeft;
    const double columnWidth = std::min(naturalColumnWidth, maxColumnWidth);
    const double columnRight = textLeft + columnWidth;
    const double gapLeft = columnRight + 6.0;
    const double gapRight = qrLeft - 6.0;
    const double phoneWidth = std::min(
        phoneNominalWidth, std::max(phoneMinimumWidth, gapRight - gapLeft));
    const double phoneHeight = phoneWidth * 128.0 / 60.0;
    const double plateTop = kOnlinePlateTop;
    const double plateHeight = kOnlinePlateH;
    const double badgeTop = plateTop + 17.0;
    const double phoneTop = plateTop + (plateHeight - phoneHeight) / 2.0;
    const double phoneLeft = (gapLeft + gapRight - phoneWidth) / 2.0;

    textColumn =
        stageRect(textLeft, badgeTop, columnWidth, plateHeight - 34.0, cx, cy);
    badge = stageRect(textLeft, badgeTop, badgeWidth, 16.0, cx, cy);
    const RECT badgeTextBox =
        stageRect(textLeft + 16.0, badgeTop, badgeWidth - 22.0, 16.0, cx, cy);
    add(dc, cache, TextRole::Badge, badgeText, L"", badgeTextBox, 8.5, 4);
    add(dc, cache, TextRole::Wordmark, word, L"",
        stageRect(textLeft, plateTop + 38.0, columnWidth, 38.0, cx, cy), 30.0,
        2);

    const std::wstring em =
        banner && banner->hasHeadlineEm ? banner->headlineEm : L"";
    add(dc, cache, TextRole::Tagline, tags[0], em,
        stageRect(textLeft, plateTop + 80.0, columnWidth, 20.0, cx, cy), 15.0,
        2);
    if (tags.size() > 1) {
      add(dc, cache, TextRole::Tagline, tags[1], em,
          stageRect(textLeft, plateTop + 99.0, columnWidth, 20.0, cx, cy), 15.0,
          2);
    }

    iphone = stageRect(phoneLeft, phoneTop, phoneWidth, phoneHeight, cx, cy);
    qr = stageRect(qrLeft, plateTop + (plateHeight - qrDesign) / 2.0, qrDesign,
                   qrDesign, cx, cy);
  }

  void buildOffline(HDC dc, FontCache &cache, double dpiScale, double cx,
                    double cy) {
    const double widthDesign = w / dpiScale;
    add(dc, cache, TextRole::OfflinePrimary,
        L"Goat Project - Help fight Cancer, Alzheimer\u2019s,", L"",
        designRect(0, 0, widthDesign, 20, dpi), 13.0, 0);
    add(dc, cache, TextRole::OfflinePrimary,
        L"Parkinson\u2019s, COVID-19, Dengue, Hepatitis C etc.", L"",
        designRect(0, 19, widthDesign, 20, dpi), 13.0, 0);
    add(dc, cache, TextRole::OfflineSecondary,
        L"Contribute your idle compute to Earn GOAT.", L"",
        designRect(0, 38, widthDesign, 20, dpi), 11.5, 0);
    mark = transformRect(designRect(28, kOfflinePlateTop + 24, 68, 76, dpi),
                         stage, cx, cy);
    add(dc, cache, TextRole::GoatWordmark, L"GOATPROJECT", L"",
        transformRect(designRect(112, kOfflinePlateTop + 46, 190, 26, dpi),
                      stage, cx, cy),
        18.0, 0, FW_LIGHT, px(1.0, dpi));
    add(dc, cache, TextRole::GoatTag, L"THE PEOPLE\u2019S COMPUTE COMMONS", L"",
        transformRect(designRect(113, kOfflinePlateTop + 77, 190, 14, dpi),
                      stage, cx, cy),
        7.5, 0, FW_SEMIBOLD, px(0.75, dpi));
  }

  void build(HDC dc, FontCache &cache, int d, int width, bool isOnline,
             const ob::Banner *banner, double requestedStage) {
    dpi = d;
    w = width;
    h = px(kControlH, d);
    online = isOnline;
    stage = requestedStage;
    const double dpiScale = d / 96.0;
    const int plateTop = px(isOnline ? kOnlinePlateTop : kOfflinePlateTop, d);
    const int plateH = px(isOnline ? kOnlinePlateH : kOfflinePlateH, d);
    plate = RECT{0, plateTop, width, plateTop + plateH};
    const double cx = width / 2.0, cy = (plate.top + plate.bottom) / 2.0;
    const double bracketRatio = (stage - 1.0) / (kStageScale - 1.0);
    const int bracketInset =
        px(kBracketRest + (kBracketTight - kBracketRest) * bracketRatio, d);
    bracket = RECT{plate.left + bracketInset, plate.top + bracketInset,
                   plate.right - bracketInset, plate.bottom - bracketInset};
    texts.clear();
    iphone = RECT{};
    qr = RECT{};
    textColumn = RECT{};
    headline = RECT{};
    mark = RECT{};
    if (isOnline) {
      buildOnline(dc, cache, banner, cx, cy);
      return;
    }
    buildOffline(dc, cache, dpiScale, cx, cy);
  }
};

gp::Bitmap *decodeBitmap(const void *data, std::size_t size) {
  if (!g_gdiplusOk || !data || size == 0)
    return nullptr;
  HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, size);
  if (!memory)
    return nullptr;
  void *destination = GlobalLock(memory);
  if (!destination) {
    GlobalFree(memory);
    return nullptr;
  }
  std::memcpy(destination, data, size);
  GlobalUnlock(memory);
  IStream *stream = nullptr;
  if (FAILED(CreateStreamOnHGlobal(memory, TRUE, &stream))) {
    GlobalFree(memory);
    return nullptr;
  }
  std::unique_ptr<gp::Bitmap> source(new gp::Bitmap(stream));
  std::unique_ptr<gp::Bitmap> result;
  if (source && source->GetLastStatus() == gp::Ok && source->GetWidth() > 0 &&
      source->GetHeight() > 0) {
    result.reset(new gp::Bitmap(static_cast<INT>(source->GetWidth()),
                                static_cast<INT>(source->GetHeight()),
                                PixelFormat32bppPARGB));
    if (!result || result->GetLastStatus() != gp::Ok)
      result.reset();
    else {
      gp::Graphics g(result.get());
      if (g.GetLastStatus() != gp::Ok ||
          g.DrawImage(source.get(), 0, 0, static_cast<INT>(source->GetWidth()),
                      static_cast<INT>(source->GetHeight())) != gp::Ok)
        result.reset();
    }
  }
  stream->Release();
  return result.release();
}

gp::Bitmap *loadResourceBitmap(const wchar_t *name) {
  if (!g_inst)
    return nullptr;
  HRSRC resource = FindResourceW(g_inst, name, MAKEINTRESOURCEW(10));
  if (!resource)
    return nullptr;
  HGLOBAL loaded = LoadResource(g_inst, resource);
  return loaded ? decodeBitmap(LockResource(loaded),
                               SizeofResource(g_inst, resource))
                : nullptr;
}
gp::Bitmap *iphone() {
  if (!g_gdiplusOk)
    return nullptr;
  if (!g_iphoneTried) {
    g_iphoneTried = true;
    g_iphone = loadResourceBitmap(L"GOATCAM_IPHONE");
  }
  return g_iphone;
}

struct State {
  HWND hwnd = nullptr;
  std::unique_ptr<ob::Client> client;
  ob::Model model;
  HWND tip = nullptr;
  bool hot = false, pressed = false, focused = false, tracking = false,
       timer = false;
  int dpiOverride = 0;
  Motion motion;
  FontCache fonts;
  Art art;
  HDC memDC = nullptr;
  HBITMAP dib = nullptr;
  HGDIOBJ oldBitmap = nullptr;
  void *bits = nullptr;
  int bw = 0, bh = 0;
  std::unique_ptr<gp::Bitmap> photo;
  ~State() { freeBuffer(); }
  void freeBuffer() {
    if (memDC) {
      if (oldBitmap)
        SelectObject(memDC, oldBitmap);
      DeleteDC(memDC);
    }
    if (dib)
      DeleteObject(dib);
    memDC = nullptr;
    dib = nullptr;
    oldBitmap = nullptr;
    bits = nullptr;
    bw = bh = 0;
  }
  bool ensureBuffer(int width, int height) {
    if (memDC && bw == width && bh == height)
      return true;
    freeBuffer();
    if (width <= 0 || height <= 0)
      return false;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    dib = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dib || !bits) {
      freeBuffer();
      return false;
    }
    memDC = CreateCompatibleDC(nullptr);
    if (!memDC) {
      freeBuffer();
      return false;
    }
    oldBitmap = SelectObject(memDC, dib);
    bw = width;
    bh = height;
    return true;
  }
  int dpi() const;
  void syncModel();
};

State *stateOf(HWND hwnd) {
  return reinterpret_cast<State *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}
int dpiOf(HWND hwnd) {
  if (State *state = stateOf(hwnd); state && state->dpiOverride > 0)
    return state->dpiOverride;
  if (HMODULE user = GetModuleHandleW(L"user32.dll")) {
    using Fn = UINT(WINAPI *)(HWND);
    auto fn = reinterpret_cast<Fn>(
        reinterpret_cast<void *>(GetProcAddress(user, "GetDpiForWindow")));
    if (fn) {
      const int d = static_cast<int>(fn(hwnd));
      if (d > 0)
        return d;
    }
  }
  HDC dc = GetDC(hwnd);
  const int d = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
  if (dc)
    ReleaseDC(hwnd, dc);
  return d > 0 ? d : 96;
}
int State::dpi() const { return dpiOverride > 0 ? dpiOverride : dpiOf(hwnd); }

std::wstring prodCache() {
  PWSTR path = nullptr;
  std::wstring result;
  if (SUCCEEDED(
          SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path)) &&
      path) {
    result = path;
    CoTaskMemFree(path);
  }
  if (result.empty())
    return {};
  result += L"\\MacFanCtl\\banner";
  std::error_code error;
  fs::create_directories(result, error);
  return result;
}
ob::Config bannerConfig() {
  if (g_testConfig)
    return *g_testConfig;
  ob::Config config;
  config.host = L"goatcam.dagoat.io";
  config.https = true;
  config.port = 443;
  config.cacheDir = prodCache();
  config.publicKey65 = &kBannerPublicKey;
  config.floor = 2;
  config.kinds = {ob::Kind::Jpg};
  config.userAgent = std::wstring(L"MacFanCtl/") + MACFANCTL_VERSION_W;
  return config;
}

void State::syncModel() {
  if (!client)
    return;
  model = client->Current();
  photo.reset();
  if (model.online && !model.jpg.empty())
    photo.reset(decodeBitmap(model.jpg.data(), model.jpg.size()));
  std::wstring label = kOfflineLabel;
  if (model.online && !model.banner.labelLink.empty())
    label = model.banner.labelLink;
  SetWindowTextW(hwnd, label.c_str());
  if (tip) {
    TTTOOLINFOW info{};
    info.cbSize = sizeof(info);
    info.hwnd = hwnd;
    info.uId = 1;
    info.lpszText = const_cast<LPWSTR>(label.c_str());
    SendMessageW(tip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&info));
  }
  InvalidateRect(hwnd, nullptr, FALSE);
}

double luminance(COLORREF color) {
  auto linear = [](BYTE value) {
    const double s = value / 255.0;
    return s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
  };
  return 0.2126 * linear(GetRValue(color)) + 0.7152 * linear(GetGValue(color)) +
         0.0722 * linear(GetBValue(color));
}
void gdiRoundRect(HDC dc, const RECT &r, int radius, const Rgb *fill, Rgb line,
                  int width) {
  HBRUSH brush = fill ? CreateSolidBrush(cr(*fill)) : nullptr;
  HPEN pen = CreatePen(PS_SOLID, width, cr(line));
  HGDIOBJ oldBrush =
      SelectObject(dc, brush ? brush : GetStockObject(NULL_BRUSH));
  HGDIOBJ oldPen = SelectObject(dc, pen);
  RoundRect(dc, r.left, r.top, r.right, r.bottom, radius * 2, radius * 2);
  SelectObject(dc, oldPen);
  SelectObject(dc, oldBrush);
  DeleteObject(pen);
  if (brush)
    DeleteObject(brush);
}

void drawBadge(gp::Graphics &graphics, const RECT &rect, int dpi) {
  const float height = static_cast<float>(rect.bottom - rect.top);
  gp::GraphicsPath pill;
  addRoundRect(pill, rectF(rect), std::max(1.0f, height / 2.0f));
  gp::SolidBrush pillBrush(gc(kEspresso950, 0.62));
  graphics.FillPath(&pillBrush, &pill);

  const float diameter = std::max(1.0f, height * 0.36f);
  const float centerX = rect.left + height * 0.5f;
  const float centerY = (rect.top + rect.bottom) * 0.5f;
  gp::SolidBrush dotBrush(gc(kGold));
  graphics.FillEllipse(&dotBrush, centerX - diameter / 2.0f,
                       centerY - diameter / 2.0f, diameter, diameter);
  (void)dpi;
}

bool hasGlyph(HDC dc, HFONT font, wchar_t character) {
  if (!dc || !font)
    return false;
  HGDIOBJ old = SelectObject(dc, font);
  WORD glyph = 0;
  const bool present =
      GetGlyphIndicesW(dc, &character, 1, &glyph,
                       GGI_MARK_NONEXISTING_GLYPHS) != GDI_ERROR &&
      glyph != 0xffff;
  SelectObject(dc, old);
  return present;
}

void drawSheepSeal(HDC dc, const Layout &layout, const Art &artData,
                   FontCache &cache) {
  const RECT seal{
      layout.mark.left + static_cast<LONG>(std::lround(artData.sealRect.X)),
      layout.mark.top + static_cast<LONG>(std::lround(artData.sealRect.Y)),
      layout.mark.left + static_cast<LONG>(std::lround(artData.sealRect.X +
                                                       artData.sealRect.Width)),
      layout.mark.top + static_cast<LONG>(std::lround(
                            artData.sealRect.Y + artData.sealRect.Height))};
  const int height =
      std::max(8, static_cast<int>(seal.bottom - seal.top) * 2 / 3);
  HFONT font = cache.get(5, height, FW_NORMAL);
  if (!hasGlyph(dc, font, L'\u7f8a'))
    font = cache.get(6, height, FW_NORMAL);
  if (!hasGlyph(dc, font, L'\u7f8a'))
    return;

  HGDIOBJ old = SelectObject(dc, font);
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, cr(kCream));
  DrawTextW(dc, L"\u7f8a", 1, const_cast<RECT *>(&seal),
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  SelectObject(dc, old);
}

void drawQr(HDC dc, const ob::Model &model, double x, double y, double side) {
  // Round every module edge independently so the grid stays crisp at the
  // fractional 1.05 stage scale.
  const double module = side / kQrN;
  HBRUSH brush = CreateSolidBrush(cr(kQrDark));
  auto dark = [&](int row, int col) {
    if (model.banner.qrRows.size() == static_cast<std::size_t>(kQrN) &&
        model.banner.qrRows[static_cast<std::size_t>(row)].size() ==
            static_cast<std::size_t>(kQrN))
      return model.banner.qrRows[static_cast<std::size_t>(row)]
                                [static_cast<std::size_t>(col)] == '1';
    return kDefaultQr[row][col] == '1';
  };
  for (int row = 0; row < kQrN; ++row) {
    for (int col = 0; col < kQrN;) {
      if (!dark(row, col)) {
        ++col;
        continue;
      }
      int end = col;
      while (end < kQrN && dark(row, end))
        ++end;
      RECT r{static_cast<LONG>(std::lround(x + col * module)),
             static_cast<LONG>(std::lround(y + row * module)),
             static_cast<LONG>(std::lround(x + end * module)),
             static_cast<LONG>(std::lround(y + (row + 1) * module))};
      FillRect(dc, &r, brush);
      col = end;
    }
  }
  DeleteObject(brush);
}

void drawText(HDC dc, TextItem &item, COLORREF color, COLORREF emColor,
              FontCache &cache) {
  if (item.fit.text.empty() || !item.fit.font)
    return;
  HGDIOBJ old = SelectObject(dc, item.fit.font);
  SetBkMode(dc, TRANSPARENT);
  SetTextAlign(dc, TA_TOP | TA_LEFT);
  SetTextCharacterExtra(dc, item.tracking);
  const std::size_t pos =
      item.em.empty() ? std::wstring::npos : item.fit.text.find(item.em);
  if (pos == std::wstring::npos) {
    SetTextColor(dc, color);
    TextOutW(dc, item.box.left, item.box.top, item.fit.text.data(),
             static_cast<int>(item.fit.text.size()));
  } else {
    const std::wstring pre = item.fit.text.substr(0, pos),
                       em = item.fit.text.substr(pos, item.em.size()),
                       post = item.fit.text.substr(pos + item.em.size());
    int x = item.box.left;
    SetTextColor(dc, color);
    TextOutW(dc, x, item.box.top, pre.data(), static_cast<int>(pre.size()));
    x += measureText(dc, item.fit.font, pre, item.tracking);
    HFONT italic = cache.get(item.fit.style == 2 ? 3 : 1, item.fit.size);
    SelectObject(dc, italic);
    SetTextColor(dc, emColor);
    TextOutW(dc, x, item.box.top, em.data(), static_cast<int>(em.size()));
    x += measureText(dc, italic, em);
    SelectObject(dc, item.fit.font);
    SetTextColor(dc, color);
    TextOutW(dc, x, item.box.top, post.data(), static_cast<int>(post.size()));
  }
  SetTextCharacterExtra(dc, 0);
  SelectObject(dc, old);
}

void drawSpark(gp::Graphics &graphics, const Art &artData, double elapsed) {
  if (elapsed <= 0.0 || elapsed >= 720.0)
    return;
  const double progress = elapsed / 720.0;
  const double opacity =
      progress < 0.2 ? progress / 0.2 : (1.0 - progress) / 0.8;
  const float radius =
      static_cast<float>(4.0 + 10.0 * progress) * artData.inner;
  gp::Pen pen(gc(kGold, opacity), std::max(1.0f, 2.0f * artData.inner));
  pen.SetStartCap(gp::LineCapRound);
  pen.SetEndCap(gp::LineCapRound);
  for (int i = 0; i < 6; ++i) {
    const double angle = art::kRayDeg[i] * 3.141592653589793 / 180.0;
    const float x0 =
        artData.burst.X + static_cast<float>(std::cos(angle) * radius);
    const float y0 =
        artData.burst.Y + static_cast<float>(std::sin(angle) * radius);
    const float x1 =
        artData.burst.X +
        static_cast<float>(std::cos(angle) * (radius + 12 * artData.inner));
    const float y1 =
        artData.burst.Y +
        static_cast<float>(std::sin(angle) * (radius + 12 * artData.inner));
    graphics.DrawLine(&pen, x0, y0, x1, y1);
  }
  gp::Pen ring(gc(kGold, opacity), std::max(1.0f, 2.0f * artData.inner));
  graphics.DrawEllipse(&ring, artData.burst.X - radius / 2,
                       artData.burst.Y - radius / 2, radius, radius);
}

struct Cover {
  double x = 0, y = 0, scale = 1;
};
Cover cover(const RECT &plate, gp::Bitmap &image, double focusX,
            double focusY) {
  const double iw = image.GetWidth(), ih = image.GetHeight(),
               pw = plate.right - plate.left, ph = plate.bottom - plate.top,
               scale = std::max(pw / iw, ph / ih), dw = iw * scale,
               dh = ih * scale;
  double x = plate.left + (pw - dw) * focusX,
         y = plate.top + (ph - dh) * focusY;
  x = std::min<double>(plate.left, std::max<double>(plate.right - dw, x));
  y = std::min<double>(plate.top, std::max<double>(plate.bottom - dh, y));
  return {x, y, scale};
}

bool render(State &state, HDC target, int dpi, int width, int height,
            double time) {
  if (!state.ensureBuffer(width, height))
    return false;
  const bool online = state.model.online;
  const double active = state.motion.active.at(time),
               stage = 1.0 + 0.05 * active, photoScale = 1.0 + 0.06 * active;
  HDC dc = state.memDC;
  const COLORREF host = GetSysColor(COLOR_BTNFACE);
  RECT all{0, 0, width, height};
  HBRUSH hostBrush = CreateSolidBrush(host);
  FillRect(dc, &all, hostBrush);
  DeleteObject(hostBrush);
  Layout layout;
  layout.build(dc, state.fonts, dpi, width, online,
               online ? &state.model.banner : nullptr, stage);
  if (state.art.dpi != dpi)
    state.art.build(dpi);
  if (g_gdiplusOk) {
    std::unique_ptr<gp::Bitmap> wrapped(
        new gp::Bitmap(width, height, width * 4, PixelFormat32bppRGB,
                       static_cast<BYTE *>(state.bits)));
    if (wrapped && wrapped->GetLastStatus() == gp::Ok) {
      gp::Graphics graphics(wrapped.get());
      graphics.SetPageUnit(gp::UnitPixel);
      graphics.SetSmoothingMode(gp::SmoothingModeAntiAlias);
      graphics.SetPixelOffsetMode(gp::PixelOffsetModeHalf);
      gp::GraphicsPath platePath;
      addRoundRect(platePath, rectF(layout.plate), pxf(kPlateRadius, dpi));
      gp::SolidBrush plateBrush(gc(kEspresso950));
      graphics.FillPath(&plateBrush, &platePath);
      graphics.SetClip(&platePath);
      if (online && state.photo) {
        const Cover cv =
            cover(layout.plate, *state.photo, state.model.banner.focus[0],
                  state.model.banner.focus[1]);
        const double fx = cv.x + state.model.banner.focus[0] *
                                     state.photo->GetWidth() * cv.scale,
                     fy = cv.y + state.model.banner.focus[1] *
                                     state.photo->GetHeight() * cv.scale,
                     scale = cv.scale * photoScale;
        gp::RectF destination(
            static_cast<float>(fx - state.model.banner.focus[0] *
                                        state.photo->GetWidth() * scale),
            static_cast<float>(fy - state.model.banner.focus[1] *
                                        state.photo->GetHeight() * scale),
            static_cast<float>(state.photo->GetWidth() * scale),
            static_cast<float>(state.photo->GetHeight() * scale));
        gp::ImageAttributes attributes;
        attributes.SetWrapMode(gp::WrapModeTileFlipXY);
        graphics.DrawImage(state.photo.get(), destination, 0, 0,
                           static_cast<float>(state.photo->GetWidth()),
                           static_cast<float>(state.photo->GetHeight()),
                           gp::UnitPixel, &attributes);
      }
      if (online) {
        gp::RectF glow = rectF(layout.textColumn);
        glow.Inflate(pxf(28, dpi), pxf(20, dpi));
        gp::GraphicsPath ellipse;
        ellipse.AddEllipse(glow);
        gp::PathGradientBrush brush(&ellipse);
        brush.SetCenterColor(gc(kEspresso950, 0.78));
        gp::Color edge = gc(kEspresso950, 0.0);
        INT count = 1;
        brush.SetSurroundColors(&edge, &count);
        graphics.FillPath(&brush, &ellipse);
      }
      graphics.ResetClip();
      if (online && state.model.banner.viewfinder) {
        graphics.SetSmoothingMode(gp::SmoothingModeNone);
        gp::SolidBrush bracketBrush(gc(kCream, 0.65));
        const int arm = px(11, dpi), bar = std::max(1, px(2, dpi)),
                  l = layout.bracket.left, t = layout.bracket.top,
                  r = layout.bracket.right, b = layout.bracket.bottom;
        const gp::Rect bars[8] = {{l, t, arm, bar},
                                  {l, t, bar, arm},
                                  {r - arm, t, arm, bar},
                                  {r - bar, t, bar, arm},
                                  {l, b - bar, arm, bar},
                                  {l, b - arm, bar, arm},
                                  {r - arm, b - bar, arm, bar},
                                  {r - bar, b - arm, bar, arm}};
        for (const gp::Rect &item : bars)
          graphics.FillRectangle(&bracketBrush, item);
        graphics.SetSmoothingMode(gp::SmoothingModeAntiAlias);
      }
      if (online) {
        const RECT phone = layout.iphone;
        gp::GraphicsPath body;
        addRoundRect(body, rectF(phone), pxf(6.5, dpi));
        gp::SolidBrush black(gc(kEspresso900));
        graphics.FillPath(&black, &body);
        if (gp::Bitmap *image = iphone()) {
          RECT inner = phone;
          InflateRect(&inner, -std::max(1, px(1, dpi)),
                      -std::max(1, px(1, dpi)));
          gp::GraphicsPath clip;
          addRoundRect(clip, rectF(inner), pxf(5.5, dpi));
          graphics.SetClip(&clip);
          graphics.DrawImage(image, rectF(inner));
          graphics.ResetClip();
        }
        gp::Pen border(gc(kCream, 0.28), std::max(1.0f, pxf(1, dpi)));
        graphics.DrawPath(&border, &body);
        drawBadge(graphics, layout.badge, dpi);
      }
      if (!online && state.art.ok) {
        graphics.TranslateTransform(static_cast<float>(layout.mark.left),
                                    static_cast<float>(layout.mark.top));
        gp::SolidBrush headBrush(gc(kCream));
        graphics.FillPath(&headBrush, state.art.head.get());
        gp::SolidBrush sealBrush(gc(kSealRed));
        graphics.FillPath(&sealBrush, state.art.seal.get());
        if (state.motion.sparkAt > 0.0)
          drawSpark(graphics, state.art, time - state.motion.sparkAt);
        graphics.ResetTransform();
      }
      if (state.focused) {
        gp::GraphicsPath focus;
        RECT ring = layout.plate;
        InflateRect(&ring, -px(3, dpi), -px(3, dpi));
        addRoundRect(focus, rectF(ring), pxf(7, dpi));
        gp::Pen focusPen(gc(kCream, 0.72), std::max(1.0f, pxf(1, dpi)));
        graphics.DrawPath(&focusPen, &focus);
      }
    }
  } else {
    gdiRoundRect(dc, layout.plate, px(kPlateRadius, dpi), &kEspresso950, kCream,
                 1);
  }
  if (!online && state.art.ok)
    drawSheepSeal(dc, layout, state.art, state.fonts);
  if (online) {
    if (g_gdiplusOk) {
      std::unique_ptr<gp::Bitmap> wrapped(
          new gp::Bitmap(width, height, width * 4, PixelFormat32bppRGB,
                         static_cast<BYTE *>(state.bits)));
      if (wrapped && wrapped->GetLastStatus() == gp::Ok) {
        gp::Graphics graphics(wrapped.get());
        gp::GraphicsPath tile;
        addRoundRect(tile, rectF(layout.qr), pxf(6, dpi));
        gp::SolidBrush cream(gc(kQrLight));
        graphics.FillPath(&cream, &tile);
      }
    } else {
      gdiRoundRect(dc, layout.qr, px(6, dpi), &kQrLight, kQrLight, 1);
      gdiRoundRect(dc, layout.badge, px(8, dpi), &kEspresso950, kEspresso950,
                   1);
    }
  }
  for (TextItem &item : layout.texts) {
    COLORREF color = cr(kCream), emColor = cr(kGold);
    switch (item.role) {
    case TextRole::Headline:
      color = luminance(host) > 0.4 ? cr(kEspresso900) : cr(kCream);
      emColor = luminance(host) > 0.4 ? cr(kCaramelDeep) : cr(kGold);
      break;
    case TextRole::OfflinePrimary:
      color = cr(kEspresso900);
      break;
    case TextRole::OfflineSecondary:
      color = cr(kMutedNeutral);
      break;
    case TextRole::GoatTag:
      color = cr(kCaramel);
      break;
    default:
      break;
    }
    drawText(dc, item, color, emColor, state.fonts);
  }
  if (online) {
    const double side = layout.qr.right - layout.qr.left,
                 cx = (layout.qr.left + layout.qr.right) / 2.0,
                 cy = (layout.qr.top + layout.qr.bottom) / 2.0;
    drawQr(dc, state.model, cx - side / 2.0, cy - side / 2.0, side);
  }
  GdiFlush();
  if (target)
    BitBlt(target, 0, 0, width, height, dc, 0, 0, SRCCOPY);
  return true;
}

void ensureTimer(State *state) {
  const bool busy = state->motion.busy(nowMs());
  if (busy && !state->timer)
    state->timer =
        SetTimer(state->hwnd, kAnimationTimer, kAnimationFrameMs, nullptr) != 0;
  else if (!busy && state->timer) {
    KillTimer(state->hwnd, kAnimationTimer);
    state->timer = false;
  }
}
bool overPlate(State *state, POINT point) {
  RECT client{};
  GetClientRect(state->hwnd, &client);
  Layout layout;
  layout.build(state->memDC, state->fonts, state->dpi(), client.right,
               state->model.online,
               state->model.online ? &state->model.banner : nullptr, 1.0);
  return PtInRect(&layout.plate, point) != FALSE;
}
void setActive(State *state, bool active) {
  const double time = nowMs();
  state->motion.setActive(active, time);
  if (state->client)
    state->client->Pointer(active);
  InvalidateRect(state->hwnd, nullptr, FALSE);
  ensureTimer(state);
}
void openFor(State *state) {
  std::string url;
  if (state->client && state->client->Click(&url)) {
    const std::wstring wide = utf8Wide(url);
    if (!wide.empty()) {
      if (g_openUrl)
        g_openUrl(wide.c_str());
      else
        ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr,
                      SW_SHOWNORMAL);
    }
    return;
  }
  const std::wstring fallback = utf8Wide(kOfflineUrl);
  if (g_openUrl)
    g_openUrl(fallback.c_str());
  else
    ShellExecuteW(nullptr, L"open", fallback.c_str(), nullptr, nullptr,
                  SW_SHOWNORMAL);
}

LRESULT CALLBACK goatCamProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
  State *state = stateOf(hwnd);
  switch (message) {
  case WM_CREATE: {
    state = new State();
    state->hwnd = hwnd;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    state->client =
        std::make_unique<ob::Client>(bannerConfig(), hwnd, kModelChanged);
    state->tip = CreateWindowExW(0, TOOLTIPS_CLASSW, nullptr,
                                 WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
                                 CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                 CW_USEDEFAULT, hwnd, nullptr, g_inst, nullptr);
    if (state->tip) {
      TTTOOLINFOW info{};
      info.cbSize = sizeof(info);
      info.uFlags = TTF_SUBCLASS;
      info.hwnd = hwnd;
      info.uId = 1;
      info.lpszText = const_cast<LPWSTR>(kOfflineLabel);
      SendMessageW(state->tip, TTM_ADDTOOLW, 0,
                   reinterpret_cast<LPARAM>(&info));
    }
    SetWindowTextW(hwnd, kOfflineLabel);
    return 0;
  }
  case WM_DESTROY:
    if (state) {
      if (state->timer)
        KillTimer(hwnd, kAnimationTimer);
      if (state->tip)
        DestroyWindow(state->tip);
      delete state;
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
    }
    return 0;
  case WM_SHOWWINDOW:
    if (state && state->client) {
      if (wp)
        state->client->Shown();
      else
        state->client->Hidden();
      state->syncModel();
    }
    return 0;
  case WM_SIZE:
  case WM_DPICHANGED_AFTERPARENT:
    if (state)
      InvalidateRect(hwnd, nullptr, FALSE);
    return 0;
  case WM_TIMER:
    if (state && wp == kAnimationTimer) {
      InvalidateRect(hwnd, nullptr, FALSE);
      ensureTimer(state);
    }
    return 0;
  case WM_MOUSEMOVE:
    if (state) {
      if (!state->tracking) {
        TRACKMOUSEEVENT event{sizeof(event), TME_LEAVE, hwnd, 0};
        state->tracking = TrackMouseEvent(&event) != FALSE;
      }
      const bool hot =
          overPlate(state, POINT{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
      if (hot != state->hot) {
        state->hot = hot;
        setActive(state, hot);
      }
    }
    return 0;
  case WM_MOUSELEAVE:
    if (state) {
      state->tracking = false;
      if (state->hot) {
        state->hot = false;
        setActive(state, false);
      }
    }
    return 0;
  case WM_LBUTTONDOWN:
    if (state && overPlate(state, POINT{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)})) {
      state->pressed = true;
      SetCapture(hwnd);
      setActive(state, true);
    }
    return 0;
  case WM_LBUTTONUP:
    if (state && state->pressed) {
      state->pressed = false;
      ReleaseCapture();
      const bool inside =
          overPlate(state, POINT{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
      if (!inside && !state->hot)
        setActive(state, false);
      if (inside)
        openFor(state);
    }
    return 0;
  case WM_CAPTURECHANGED:
    if (state && state->pressed && reinterpret_cast<HWND>(lp) != hwnd) {
      state->pressed = false;
      setActive(state, state->hot);
    }
    return 0;
  case WM_GETDLGCODE: {
    LRESULT code = DLGC_WANTCHARS;
    const MSG *m = reinterpret_cast<const MSG *>(lp);
    if (m && m->message == WM_KEYDOWN &&
        (m->wParam == VK_RETURN || m->wParam == VK_SPACE))
      code |= DLGC_WANTMESSAGE;
    return code;
  }
  case WM_KEYDOWN:
    if (state && (wp == VK_RETURN || wp == VK_SPACE)) {
      if ((lp & (1 << 30)) == 0)
        openFor(state);
      return 0;
    }
    break;
  case WM_CHAR:
    if (wp == L' ' || wp == L'\r')
      return 0;
    break;
  case WM_SETFOCUS:
    if (state) {
      state->focused = true;
      if (state->client)
        state->client->Focus(true);
      setActive(state, true);
    }
    return 0;
  case WM_KILLFOCUS:
    if (state) {
      state->focused = false;
      if (state->client)
        state->client->Focus(false);
      setActive(state, state->hot);
    }
    return 0;
  case kModelChanged:
    if (state)
      state->syncModel();
    return 0;
  case WM_ERASEBKGND:
    return 1;
  case WM_PRINTCLIENT:
    if (state) {
      RECT r{};
      GetClientRect(hwnd, &r);
      if (!render(*state, reinterpret_cast<HDC>(wp), state->dpi(), r.right,
                  r.bottom, nowMs()))
        FillRect(reinterpret_cast<HDC>(wp), &r,
                 GetSysColorBrush(COLOR_BTNFACE));
    }
    return 0;
  case WM_PAINT: {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(hwnd, &paint);
    if (dc && state) {
      RECT r{};
      GetClientRect(hwnd, &r);
      render(*state, dc, state->dpi(), r.right, r.bottom, nowMs());
    }
    EndPaint(hwnd, &paint);
    return 0;
  }
  }
  return DefWindowProcW(hwnd, message, wp, lp);
}

} // namespace

void registerGoatCamClass(HINSTANCE hInst) {
  g_inst = hInst;
  if (!g_gdiplusOk) {
    gp::GdiplusStartupInput input{};
    if (gp::GdiplusStartup(&g_gdiplusToken, &input, nullptr) == gp::Ok)
      g_gdiplusOk = true;
  }
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = goatCamProc;
  wc.hInstance = hInst;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.lpszClassName = kGoatCamClass;
  RegisterClassExW(&wc);
}
SIZE goatCamPreferredSize(int dpi) {
  return SIZE{px(400.0, dpi), px(kControlH, dpi)};
}
void goatCamVisible(HWND hwnd, bool visible) {
  if (State *state = stateOf(hwnd); state && state->client) {
    if (visible)
      state->client->Shown();
    else
      state->client->Hidden();
    state->syncModel();
  }
}
void goatCamResumed(HWND hwnd) {
  if (State *state = stateOf(hwnd); state && state->client) {
    state->client->Resumed();
    state->syncModel();
  }
}
void goatCamTestConfig(const ob::Config *config) { g_testConfig = config; }
void goatCamTestSetOpenUrl(void (*openUrl)(const wchar_t *)) {
  g_openUrl = openUrl;
}
bool goatCamTestIsOnline(HWND hwnd) {
  State *state = stateOf(hwnd);
  return state && state->model.online;
}
bool goatCamTestProbeLayout(HWND hwnd, int dpi, GoatCamTestModelKind kind,
                            bool active, GoatCamTestLayoutProbe *probe) {
  if (!probe)
    return false;
  State *state = stateOf(hwnd);
  int width = px(400.0, dpi);
  if (state) {
    RECT client{};
    GetClientRect(hwnd, &client);
    if (client.right > 0)
      width = client.right;
  }
  FontCache local;
  FontCache &cache = state ? state->fonts : local;
  HDC dc = state && state->memDC ? state->memDC : GetDC(nullptr);
  ob::Model model = state ? state->model : ob::Model{};
  Layout layout;
  layout.build(dc, cache, dpi, width, kind == GoatCamTestModelKind::Online,
               kind == GoatCamTestModelKind::Online ? &model.banner : nullptr,
               active ? kStageScale : 1.0);
  if (!state && dc)
    ReleaseDC(nullptr, dc);
  probe->control = SIZE{width, layout.h};
  probe->plate = layout.plate;
  probe->bracket = layout.bracket;
  probe->textColumn = layout.textColumn;
  probe->badge = layout.badge;
  probe->iphone = layout.iphone;
  probe->qr = layout.qr;
  probe->headline = layout.headline;
  probe->textCount = std::min<int>(static_cast<int>(layout.texts.size()), 8);
  probe->kind = kind;
  probe->scale = layout.stage;
  for (int i = 0; i < probe->textCount; ++i) {
    const TextItem &item = layout.texts[static_cast<std::size_t>(i)];
    probe->text[i].box = item.box;
    probe->text[i].extent = item.extent;
    probe->text[i].role = static_cast<int>(item.role);
    probe->text[i].requestedSize = item.requestedSize;
    probe->text[i].actualSize = item.fit.size;
    probe->text[i].ellipsized = item.fit.text != item.text;
  }
  return true;
}
void goatCamTestSetActive(HWND hwnd, bool active, bool settle) {
  if (State *state = stateOf(hwnd)) {
    state->hot = active;
    if (state->client)
      state->client->Pointer(active);
    if (settle)
      state->motion.settle(active);
    else
      state->motion.setActive(active, nowMs());
    InvalidateRect(hwnd, nullptr, FALSE);
    ensureTimer(state);
  }
}
void goatCamTestSetDpi(HWND hwnd, int dpi) {
  if (State *state = stateOf(hwnd)) {
    state->dpiOverride = dpi;
    InvalidateRect(hwnd, nullptr, FALSE);
  }
}
void goatCamTestForceCheck(HWND hwnd) {
  if (State *state = stateOf(hwnd); state && state->client) {
    state->client.reset();
    state->client =
        std::make_unique<ob::Client>(bannerConfig(), hwnd, kModelChanged);
    state->client->Shown();
    state->syncModel();
  }
}

} // namespace ui
