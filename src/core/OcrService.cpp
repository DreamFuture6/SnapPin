#include "core/OcrService.h"
#include "core/ImageCodecUtil.h"
#include "core/Logger.h"

#include <wincrypt.h>
#include <winhttp.h>

namespace {
constexpr DWORD kConnectTimeoutMs = 10000;
constexpr DWORD kSendTimeoutMs = 15000;
constexpr DWORD kReceiveTimeoutMs = 45000;

struct OcrTextSegment {
    std::wstring text;
    RECT box{};
    double score = -1.0;
};

class ScopedWinHttpHandle {
public:
    ScopedWinHttpHandle() = default;
    explicit ScopedWinHttpHandle(HINTERNET handle) : handle_(handle) {}
    ~ScopedWinHttpHandle() { Reset(); }
    ScopedWinHttpHandle(const ScopedWinHttpHandle&) = delete;
    ScopedWinHttpHandle& operator=(const ScopedWinHttpHandle&) = delete;
    ScopedWinHttpHandle(ScopedWinHttpHandle&& other) noexcept : handle_(other.handle_) { other.handle_ = nullptr; }
    ScopedWinHttpHandle& operator=(ScopedWinHttpHandle&& other) noexcept {
        if (this != &other) {
            Reset();
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }
    HINTERNET Get() const { return handle_; }
    explicit operator bool() const { return handle_ != nullptr; }
    void Reset(HINTERNET next = nullptr) {
        if (handle_) {
            WinHttpCloseHandle(handle_);
        }
        handle_ = next;
    }
private:
    HINTERNET handle_ = nullptr;
};

std::wstring Trim(std::wstring value) {
    const size_t begin = value.find_first_not_of(L" \t\r\n");
    if (begin == std::wstring::npos) {
        return {};
    }
    const size_t end = value.find_last_not_of(L" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

std::string WideToUtf8(const std::wstring& s) {
    if (s.empty()) {
        return {};
    }
    const int len = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), len, nullptr, nullptr);
    return out;
}

bool ContainsNoCase(const std::wstring& text, const std::wstring& needle) {
    if (needle.empty()) {
        return true;
    }
    std::wstring haystack = text;
    std::wstring target = needle;
    std::transform(haystack.begin(), haystack.end(), haystack.begin(), ::towlower);
    std::transform(target.begin(), target.end(), target.begin(), ::towlower);
    return haystack.find(target) != std::wstring::npos;
}

bool EndsWithNoCase(const std::wstring& text, const std::wstring& suffix) {
    if (suffix.size() > text.size()) {
        return false;
    }
    return _wcsicmp(text.c_str() + (text.size() - suffix.size()), suffix.c_str()) == 0;
}

std::wstring NormalizeApiUrl(const std::wstring& rawUrl) {
    std::wstring url = Trim(rawUrl);
    if (url.empty()) {
        return {};
    }
    const size_t queryPos = url.find(L'?');
    std::wstring pathPart = queryPos == std::wstring::npos ? url : url.substr(0, queryPos);
    const std::wstring queryPart = queryPos == std::wstring::npos ? L"" : url.substr(queryPos);
    while (!pathPart.empty() && pathPart.back() == L'/') {
        pathPart.pop_back();
    }
    if (!EndsWithNoCase(pathPart, L"/ocr")) {
        pathPart += L"/ocr";
    }
    return pathPart + queryPart;
}

bool Base64Encode(const std::vector<uint8_t>& bytes, std::string& out) {
    out.clear();
    if (bytes.empty() || bytes.size() > static_cast<size_t>((std::numeric_limits<DWORD>::max)())) {
        return false;
    }
    DWORD required = 0;
    if (!CryptBinaryToStringA(bytes.data(), static_cast<DWORD>(bytes.size()), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &required)) {
        return false;
    }
    std::string encoded(static_cast<size_t>(required), '\0');
    if (!CryptBinaryToStringA(bytes.data(), static_cast<DWORD>(bytes.size()), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, encoded.data(), &required)) {
        return false;
    }
    if (!encoded.empty() && encoded.back() == '\0') {
        encoded.pop_back();
    }
    out = std::move(encoded);
    return true;
}

bool IsInlineWhitespace(wchar_t ch) {
    return ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n' || ch == 0x3000;
}

bool HasVisibleText(const std::wstring& text) {
    for (wchar_t ch : text) {
        if (!IsInlineWhitespace(ch)) {
            return true;
        }
    }
    return false;
}

int CountVisibleChars(const std::wstring& text) {
    int count = 0;
    for (wchar_t ch : text) {
        if (!IsInlineWhitespace(ch)) {
            ++count;
        }
    }
    return count;
}

Image FlattenImageToWhite(const Image& image) {
    if (!image.IsValid()) {
        return {};
    }
    Image out = Image::Create(image.width, image.height);
    if (!out.IsValid()) {
        return {};
    }
    const size_t pixelCount = static_cast<size_t>(image.width) * static_cast<size_t>(image.height);
    for (size_t i = 0; i < pixelCount; ++i) {
        const size_t idx = i * 4;
        const uint32_t alpha = image.bgra[idx + 3];
        out.bgra[idx + 0] = static_cast<uint8_t>((static_cast<uint32_t>(image.bgra[idx + 0]) * alpha + 255u * (255u - alpha)) / 255u);
        out.bgra[idx + 1] = static_cast<uint8_t>((static_cast<uint32_t>(image.bgra[idx + 1]) * alpha + 255u * (255u - alpha)) / 255u);
        out.bgra[idx + 2] = static_cast<uint8_t>((static_cast<uint32_t>(image.bgra[idx + 2]) * alpha + 255u * (255u - alpha)) / 255u);
        out.bgra[idx + 3] = 255;
    }
    return out;
}

bool ScaleImageBicubic(const Image& src, int dstW, int dstH, Image& out) {
    if (!src.IsValid() || dstW <= 0 || dstH <= 0 || !ImageCodecUtil::EnsureGdiplus()) {
        return false;
    }
    auto srcBitmap = ImageCodecUtil::CreateBitmapFromImage(src);
    if (!srcBitmap) {
        return false;
    }
    Gdiplus::Bitmap dstBitmap(dstW, dstH, PixelFormat32bppARGB);
    if (dstBitmap.GetLastStatus() != Gdiplus::Ok) {
        return false;
    }
    Gdiplus::Graphics g(&dstBitmap);
    g.Clear(Gdiplus::Color(255, 255, 255, 255));
    g.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
    g.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    Gdiplus::ImageAttributes attrs;
    attrs.SetWrapMode(Gdiplus::WrapModeTileFlipXY);
    if (g.DrawImage(srcBitmap.get(), Gdiplus::Rect(0, 0, dstW, dstH), 0, 0, src.width, src.height, Gdiplus::UnitPixel, &attrs) != Gdiplus::Ok) {
        return false;
    }
    return ImageCodecUtil::CopyBitmapToImage(dstBitmap, out);
}

Image AddWhiteBorder(const Image& image, int border) {
    if (!image.IsValid() || border <= 0) {
        return image;
    }
    Image out = Image::Create(image.width + border * 2, image.height + border * 2);
    if (!out.IsValid()) {
        return image;
    }
    std::fill(out.bgra.begin(), out.bgra.end(), static_cast<uint8_t>(255));
    for (int y = 0; y < image.height; ++y) {
        const uint8_t* src = image.bgra.data() + static_cast<size_t>(y) * static_cast<size_t>(image.width) * 4;
        uint8_t* dst = out.bgra.data() + static_cast<size_t>(y + border) * static_cast<size_t>(out.width) * 4 + static_cast<size_t>(border) * 4;
        memcpy(dst, src, static_cast<size_t>(image.width) * 4);
    }
    return out;
}

Image PrepareImageForOcr(const Image& image) {
    Image flattened = FlattenImageToWhite(image);
    if (!flattened.IsValid()) {
        return {};
    }
    const int shortSide = (std::min)(flattened.width, flattened.height);
    const int longSide = (std::max)(flattened.width, flattened.height);
    double scale = 1.0;
    if (shortSide > 0 && longSide > 0) {
        scale = (std::max)(scale, 960.0 / static_cast<double>(shortSide));
        scale = (std::min)(scale, 2.5);
        if (static_cast<double>(longSide) * scale > 2560.0) {
            scale = 2560.0 / static_cast<double>(longSide);
        }
        scale = (std::max)(1.0, scale);
    }
    Image scaled = flattened;
    if (scale > 1.05) {
        const int dstW = (std::max)(1, static_cast<int>(std::lround(static_cast<double>(flattened.width) * scale)));
        const int dstH = (std::max)(1, static_cast<int>(std::lround(static_cast<double>(flattened.height) * scale)));
        Image resized;
        if (ScaleImageBicubic(flattened, dstW, dstH, resized) && resized.IsValid()) {
            scaled = std::move(resized);
        }
    }
    const int border = std::clamp((std::min)(scaled.width, scaled.height) / 12, 18, 72);
    return AddWhiteBorder(scaled, border);
}

double ComputeAverageLuma(const Image& image) {
    if (!image.IsValid()) {
        return 255.0;
    }
    double sum = 0.0;
    const size_t pixelCount = static_cast<size_t>(image.width) * static_cast<size_t>(image.height);
    for (size_t i = 0; i < pixelCount; ++i) {
        const size_t idx = i * 4;
        const double b = static_cast<double>(image.bgra[idx + 0]);
        const double g = static_cast<double>(image.bgra[idx + 1]);
        const double r = static_cast<double>(image.bgra[idx + 2]);
        sum += 0.114 * b + 0.587 * g + 0.299 * r;
    }
    return sum / static_cast<double>(pixelCount);
}

double ComputeDarkPixelRatio(const Image& image) {
    if (!image.IsValid()) {
        return 0.0;
    }
    size_t darkPixels = 0;
    const size_t pixelCount = static_cast<size_t>(image.width) * static_cast<size_t>(image.height);
    for (size_t i = 0; i < pixelCount; ++i) {
        const size_t idx = i * 4;
        const int luma = static_cast<int>(std::lround(
            0.114 * static_cast<double>(image.bgra[idx + 0]) +
            0.587 * static_cast<double>(image.bgra[idx + 1]) +
            0.299 * static_cast<double>(image.bgra[idx + 2])));
        if (luma < 96) {
            ++darkPixels;
        }
    }
    return static_cast<double>(darkPixels) / static_cast<double>(pixelCount);
}

bool LooksLikeDarkThemeTextImage(const Image& image) {
    return ComputeAverageLuma(image) < 150.0 && ComputeDarkPixelRatio(image) > 0.40;
}

Image PrepareHighContrastOcrImage(const Image& image, bool invertForDarkTheme) {
    Image flattened = FlattenImageToWhite(image);
    if (!flattened.IsValid()) {
        return {};
    }

    Image enhanced = Image::Create(flattened.width, flattened.height);
    if (!enhanced.IsValid()) {
        return {};
    }

    for (size_t i = 0; i < flattened.bgra.size(); i += 4) {
        int luma = static_cast<int>(std::lround(
            0.114 * static_cast<double>(flattened.bgra[i + 0]) +
            0.587 * static_cast<double>(flattened.bgra[i + 1]) +
            0.299 * static_cast<double>(flattened.bgra[i + 2])));
        if (invertForDarkTheme) {
            luma = 255 - luma;
        }

        // Mild contrast stretch keeps punctuation edges while avoiding hard threshold artifacts.
        luma = std::clamp((luma - 24) * 255 / 200, 0, 255);
        const uint8_t v = static_cast<uint8_t>(luma);
        enhanced.bgra[i + 0] = v;
        enhanced.bgra[i + 1] = v;
        enhanced.bgra[i + 2] = v;
        enhanced.bgra[i + 3] = 255;
    }

    double scale = 1.0;
    const int shortSide = (std::min)(enhanced.width, enhanced.height);
    const int longSide = (std::max)(enhanced.width, enhanced.height);
    if (shortSide > 0 && longSide > 0) {
        scale = (std::max)(scale, 1100.0 / static_cast<double>(shortSide));
        scale = (std::min)(scale, 3.0);
        if (static_cast<double>(longSide) * scale > 2800.0) {
            scale = 2800.0 / static_cast<double>(longSide);
        }
        scale = (std::max)(1.0, scale);
    }

    Image scaled = enhanced;
    if (scale > 1.05) {
        const int dstW = (std::max)(1, static_cast<int>(std::lround(static_cast<double>(enhanced.width) * scale)));
        const int dstH = (std::max)(1, static_cast<int>(std::lround(static_cast<double>(enhanced.height) * scale)));
        Image resized = enhanced.ResizeNearest(dstW, dstH);
        if (resized.IsValid()) {
            scaled = std::move(resized);
        }
    }

    const int border = std::clamp((std::min)(scaled.width, scaled.height) / 10, 20, 80);
    return AddWhiteBorder(scaled, border);
}

struct ParsedUrl {
    std::wstring host;
    std::wstring path;
    INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;
    bool secure = true;
};

bool ParseUrl(const std::wstring& url, ParsedUrl& parsed, std::wstring& error) {
    URL_COMPONENTSW components{};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = static_cast<DWORD>(-1);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &components)) {
        error = L"无法解析 PaddleOCR 服务 URL。";
        return false;
    }
    if (components.nScheme != INTERNET_SCHEME_HTTP && components.nScheme != INTERNET_SCHEME_HTTPS) {
        error = L"PaddleOCR 服务 URL 仅支持 HTTP 或 HTTPS。";
        return false;
    }
    parsed.host.assign(components.lpszHostName, components.dwHostNameLength);
    parsed.path.assign(components.lpszUrlPath, components.dwUrlPathLength);
    if (components.dwExtraInfoLength > 0) {
        parsed.path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    }
    if (parsed.path.empty()) {
        parsed.path = L"/";
    }
    parsed.port = components.nPort;
    parsed.secure = components.nScheme == INTERNET_SCHEME_HTTPS;
    return true;
}

size_t SkipJsonWhitespace(const std::string& text, size_t pos) {
    while (pos < text.size()) {
        const unsigned char ch = static_cast<unsigned char>(text[pos]);
        if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') {
            break;
        }
        ++pos;
    }
    return pos;
}

bool AppendUtf8CodePoint(std::string& out, uint32_t codePoint) {
    if (codePoint <= 0x7F) {
        out.push_back(static_cast<char>(codePoint));
        return true;
    }
    if (codePoint <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        return true;
    }
    if (codePoint <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        return true;
    }
    if (codePoint <= 0x10FFFF) {
        out.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        return true;
    }
    return false;
}

bool ParseHex4(const std::string& text, size_t pos, uint32_t& value) {
    if (pos + 4 > text.size()) {
        return false;
    }
    value = 0;
    for (size_t i = 0; i < 4; ++i) {
        const char ch = text[pos + i];
        value <<= 4;
        if (ch >= '0' && ch <= '9') {
            value |= static_cast<uint32_t>(ch - '0');
        } else if (ch >= 'a' && ch <= 'f') {
            value |= static_cast<uint32_t>(ch - 'a' + 10);
        } else if (ch >= 'A' && ch <= 'F') {
            value |= static_cast<uint32_t>(ch - 'A' + 10);
        } else {
            return false;
        }
    }
    return true;
}

bool ParseJsonStringAt(const std::string& text, size_t pos, std::string& value, size_t& nextPos) {
    if (pos >= text.size() || text[pos] != '"') {
        return false;
    }
    value.clear();
    ++pos;
    while (pos < text.size()) {
        const char ch = text[pos++];
        if (ch == '"') {
            nextPos = pos;
            return true;
        }
        if (ch != '\\') {
            value.push_back(ch);
            continue;
        }
        if (pos >= text.size()) {
            return false;
        }
        const char escaped = text[pos++];
        switch (escaped) {
        case '"': value.push_back('"'); break;
        case '\\': value.push_back('\\'); break;
        case '/': value.push_back('/'); break;
        case 'b': value.push_back('\b'); break;
        case 'f': value.push_back('\f'); break;
        case 'n': value.push_back('\n'); break;
        case 'r': value.push_back('\r'); break;
        case 't': value.push_back('\t'); break;
        case 'u': {
            uint32_t codeUnit = 0;
            if (!ParseHex4(text, pos, codeUnit)) {
                return false;
            }
            pos += 4;
            uint32_t codePoint = codeUnit;
            if (codeUnit >= 0xD800 && codeUnit <= 0xDBFF) {
                if (pos + 6 > text.size() || text[pos] != '\\' || text[pos + 1] != 'u') {
                    return false;
                }
                uint32_t low = 0;
                if (!ParseHex4(text, pos + 2, low) || low < 0xDC00 || low > 0xDFFF) {
                    return false;
                }
                pos += 6;
                codePoint = 0x10000 + (((codeUnit - 0xD800) << 10) | (low - 0xDC00));
            }
            if (!AppendUtf8CodePoint(value, codePoint)) {
                return false;
            }
            break;
        }
        default:
            return false;
        }
    }
    return false;
}

bool ParseJsonNumberAt(const std::string& text, size_t pos, double& value, size_t& nextPos) {
    pos = SkipJsonWhitespace(text, pos);
    size_t end = pos;
    if (end < text.size() && (text[end] == '-' || text[end] == '+')) {
        ++end;
    }
    bool hasDigit = false;
    while (end < text.size() && text[end] >= '0' && text[end] <= '9') {
        hasDigit = true;
        ++end;
    }
    if (end < text.size() && text[end] == '.') {
        ++end;
        while (end < text.size() && text[end] >= '0' && text[end] <= '9') {
            hasDigit = true;
            ++end;
        }
    }
    if (end < text.size() && (text[end] == 'e' || text[end] == 'E')) {
        ++end;
        if (end < text.size() && (text[end] == '-' || text[end] == '+')) {
            ++end;
        }
        while (end < text.size() && text[end] >= '0' && text[end] <= '9') {
            hasDigit = true;
            ++end;
        }
    }
    if (!hasDigit) {
        return false;
    }
    try {
        value = std::stod(text.substr(pos, end - pos));
    } catch (...) {
        return false;
    }
    nextPos = end;
    return true;
}

bool FindJsonValueForKey(const std::string& text, const char* key, size_t startPos, size_t& valuePos) {
    const std::string token = "\"" + std::string(key) + "\"";
    size_t pos = startPos;
    while ((pos = text.find(token, pos)) != std::string::npos) {
        size_t cursor = SkipJsonWhitespace(text, pos + token.size());
        if (cursor >= text.size() || text[cursor] != ':') {
            pos += token.size();
            continue;
        }
        valuePos = SkipJsonWhitespace(text, cursor + 1);
        return true;
    }
    return false;
}

bool FindCompositeEnd(const std::string& text, size_t startPos, char openChar, char closeChar, size_t& endPos) {
    if (startPos >= text.size() || text[startPos] != openChar) {
        return false;
    }
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (size_t i = startPos; i < text.size(); ++i) {
        const char ch = text[i];
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if (ch == '"') {
                inString = false;
            }
            continue;
        }
        if (ch == '"') {
            inString = true;
            continue;
        }
        if (ch == openChar) {
            ++depth;
        } else if (ch == closeChar) {
            --depth;
            if (depth == 0) {
                endPos = i;
                return true;
            }
        }
    }
    return false;
}

bool ExtractJsonObjectForKey(const std::string& text, const char* key, std::string& objectText) {
    size_t objectPos = 0;
    if (!FindJsonValueForKey(text, key, 0, objectPos) || objectPos >= text.size() || text[objectPos] != '{') {
        return false;
    }
    size_t objectEnd = 0;
    if (!FindCompositeEnd(text, objectPos, '{', '}', objectEnd)) {
        return false;
    }
    objectText.assign(text.begin() + static_cast<std::ptrdiff_t>(objectPos), text.begin() + static_cast<std::ptrdiff_t>(objectEnd + 1));
    return true;
}

bool TryGetJsonInt(const std::string& text, const char* key, int& value) {
    size_t pos = 0;
    if (!FindJsonValueForKey(text, key, 0, pos)) {
        return false;
    }
    double parsed = 0.0;
    size_t nextPos = pos;
    if (!ParseJsonNumberAt(text, pos, parsed, nextPos)) {
        return false;
    }
    value = static_cast<int>(std::lround(parsed));
    return true;
}

bool TryGetJsonString(const std::string& text, const char* key, std::string& value) {
    size_t pos = 0;
    if (!FindJsonValueForKey(text, key, 0, pos)) {
        return false;
    }
    size_t nextPos = pos;
    return ParseJsonStringAt(text, pos, value, nextPos);
}

void CollectJsonStringArrayValues(const std::string& text, const char* key, std::vector<std::string>& values) {
    size_t arrayPos = 0;
    if (!FindJsonValueForKey(text, key, 0, arrayPos) || arrayPos >= text.size() || text[arrayPos] != '[') {
        return;
    }
    size_t pos = arrayPos + 1;
    while (pos < text.size()) {
        pos = SkipJsonWhitespace(text, pos);
        if (pos >= text.size() || text[pos] == ']') {
            return;
        }
        std::string value;
        size_t nextPos = pos;
        if (!ParseJsonStringAt(text, pos, value, nextPos)) {
            return;
        }
        values.push_back(std::move(value));
        pos = SkipJsonWhitespace(text, nextPos);
        if (pos < text.size() && text[pos] == ',') {
            ++pos;
        }
    }
}

void CollectJsonNumberArrayValues(const std::string& text, const char* key, std::vector<double>& values) {
    size_t arrayPos = 0;
    if (!FindJsonValueForKey(text, key, 0, arrayPos) || arrayPos >= text.size() || text[arrayPos] != '[') {
        return;
    }
    size_t pos = arrayPos + 1;
    while (pos < text.size()) {
        pos = SkipJsonWhitespace(text, pos);
        if (pos >= text.size() || text[pos] == ']') {
            return;
        }
        double value = 0.0;
        size_t nextPos = pos;
        if (!ParseJsonNumberAt(text, pos, value, nextPos)) {
            return;
        }
        values.push_back(value);
        pos = SkipJsonWhitespace(text, nextPos);
        if (pos < text.size() && text[pos] == ',') {
            ++pos;
        }
    }
}

void CollectJsonNumberMatrixRows(const std::string& text, const char* key, std::vector<std::array<int, 4>>& rows) {
    size_t arrayPos = 0;
    if (!FindJsonValueForKey(text, key, 0, arrayPos) || arrayPos >= text.size() || text[arrayPos] != '[') {
        return;
    }
    size_t pos = arrayPos + 1;
    while (pos < text.size()) {
        pos = SkipJsonWhitespace(text, pos);
        if (pos >= text.size() || text[pos] == ']') {
            return;
        }
        if (text[pos] != '[') {
            return;
        }
        std::array<int, 4> row{};
        size_t rowIndex = 0;
        ++pos;
        while (pos < text.size()) {
            pos = SkipJsonWhitespace(text, pos);
            if (pos >= text.size()) {
                return;
            }
            if (text[pos] == ']') {
                ++pos;
                break;
            }
            double value = 0.0;
            size_t nextPos = pos;
            if (!ParseJsonNumberAt(text, pos, value, nextPos)) {
                return;
            }
            if (rowIndex < row.size()) {
                row[rowIndex] = static_cast<int>(std::lround(value));
            }
            ++rowIndex;
            pos = SkipJsonWhitespace(text, nextPos);
            if (pos < text.size() && text[pos] == ',') {
                ++pos;
            }
        }
        if (rowIndex >= 4) {
            rows.push_back(row);
        }
        pos = SkipJsonWhitespace(text, pos);
        if (pos < text.size() && text[pos] == ',') {
            ++pos;
        }
    }
}

std::wstring BuildResponseSnippet(const std::string& utf8Text) {
    std::wstring text = Trim(Utf8ToWide(utf8Text));
    std::wstring snippet;
    snippet.reserve((std::min)(text.size(), static_cast<size_t>(160)));
    bool lastWasSpace = false;
    for (wchar_t ch : text) {
        if (ch == L'\r' || ch == L'\n' || ch == L'\t') {
            ch = L' ';
        }
        if (ch == L' ') {
            if (snippet.empty() || lastWasSpace) {
                continue;
            }
            lastWasSpace = true;
        } else {
            lastWasSpace = false;
        }
        if (snippet.size() >= 160) {
            break;
        }
        snippet.push_back(ch);
    }
    snippet = Trim(snippet);
    if (text.size() > snippet.size()) {
        snippet += L"...";
    }
    return snippet;
}

bool IsSameVisualLine(const OcrTextSegment& previous, const OcrTextSegment& current) {
    const int prevHeight = (std::max)(1, RectHeight(previous.box));
    const int currHeight = (std::max)(1, RectHeight(current.box));
    const int prevCenterY = previous.box.top + prevHeight / 2;
    const int currCenterY = current.box.top + currHeight / 2;
    const int centerDelta = std::abs(currCenterY - prevCenterY);
    const int overlapTop = (std::max)(previous.box.top, current.box.top);
    const int overlapBottom = (std::min)(previous.box.bottom, current.box.bottom);
    const int overlap = (std::max)(0, overlapBottom - overlapTop);
    const int minHeight = (std::min)(prevHeight, currHeight);
    return centerDelta <= (std::max)(6, static_cast<int>(std::lround(static_cast<double>((std::max)(prevHeight, currHeight)) * 0.55))) ||
        overlap >= static_cast<int>(std::lround(static_cast<double>(minHeight) * 0.35));
}

bool ShouldInsertSpaceBetween(const OcrTextSegment& previous, const OcrTextSegment& current) {
    if (previous.text.empty() || current.text.empty()) {
        return false;
    }
    if (IsInlineWhitespace(previous.text.back()) || IsInlineWhitespace(current.text.front())) {
        return false;
    }
    const int gap = current.box.left - previous.box.right;
    const int prevHeight = (std::max)(1, RectHeight(previous.box));
    const int currHeight = (std::max)(1, RectHeight(current.box));
    const int gapThreshold = (std::max)(3, static_cast<int>(std::lround(static_cast<double>((std::min)(prevHeight, currHeight)) * 0.22)));
    return gap > gapThreshold;
}

void SortSegmentsByLayout(std::vector<OcrTextSegment>& segments) {
    if (segments.empty()) {
        return;
    }

    std::stable_sort(segments.begin(), segments.end(), [](const OcrTextSegment& a, const OcrTextSegment& b) {
        if (IsSameVisualLine(a, b)) {
            if (a.box.left != b.box.left) {
                return a.box.left < b.box.left;
            }
            return a.box.top < b.box.top;
        }

        const int aHeight = (std::max)(1, RectHeight(a.box));
        const int bHeight = (std::max)(1, RectHeight(b.box));
        const int aCenterY = a.box.top + aHeight / 2;
        const int bCenterY = b.box.top + bHeight / 2;
        if (aCenterY != bCenterY) {
            return aCenterY < bCenterY;
        }
        if (a.box.top != b.box.top) {
            return a.box.top < b.box.top;
        }
        return a.box.left < b.box.left;
    });
}

std::wstring JoinRecognizedText(const std::vector<std::string>& textsUtf8, const std::vector<double>& scores, const std::vector<std::array<int, 4>>& boxes, int& confidence) {
    std::vector<OcrTextSegment> segments;
    segments.reserve(textsUtf8.size());
    double scoreSum = 0.0;
    int scoreCount = 0;
    for (size_t i = 0; i < textsUtf8.size(); ++i) {
        std::wstring text = Utf8ToWide(textsUtf8[i]);
        if (!HasVisibleText(text)) {
            continue;
        }
        OcrTextSegment segment{};
        segment.text = std::move(text);
        if (i < boxes.size()) {
            segment.box.left = boxes[i][0];
            segment.box.top = boxes[i][1];
            segment.box.right = boxes[i][2];
            segment.box.bottom = boxes[i][3];
        } else {
            segment.box.top = static_cast<LONG>(segments.size()) * 100;
            segment.box.bottom = segment.box.top + 100;
        }
        if (i < scores.size()) {
            segment.score = scores[i];
            scoreSum += segment.score;
            ++scoreCount;
        }
        segments.push_back(std::move(segment));
    }
    confidence = scoreCount > 0 ? static_cast<int>(std::lround((scoreSum / static_cast<double>(scoreCount)) * 100.0)) : -1;
    if (segments.empty()) {
        return {};
    }

    if (!boxes.empty()) {
        SortSegmentsByLayout(segments);
    }

    std::wstring combined;
    combined.reserve(segments.size() * 16);
    combined += segments.front().text;
    for (size_t i = 1; i < segments.size(); ++i) {
        const OcrTextSegment& previous = segments[i - 1];
        const OcrTextSegment& current = segments[i];
        if (IsSameVisualLine(previous, current)) {
            if (ShouldInsertSpaceBetween(previous, current)) {
                combined.push_back(L' ');
            }
        } else {
            combined += L"\r\n";
        }
        combined += current.text;
    }
    return combined;
}

bool PostJson(const std::wstring& url, const std::wstring& token, const std::string& jsonBody, DWORD& statusCode, std::string& responseBody, std::wstring& error) {
    ParsedUrl parsed{};
    if (!ParseUrl(url, parsed, error)) {
        return false;
    }
    ScopedWinHttpHandle session(WinHttpOpen(L"SnapPin/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) {
        error = L"无法创建 HTTP 会话。";
        return false;
    }
    WinHttpSetTimeouts(session.Get(), static_cast<int>(kConnectTimeoutMs), static_cast<int>(kConnectTimeoutMs), static_cast<int>(kSendTimeoutMs), static_cast<int>(kReceiveTimeoutMs));
    ScopedWinHttpHandle connect(WinHttpConnect(session.Get(), parsed.host.c_str(), parsed.port, 0));
    if (!connect) {
        error = L"无法连接 PaddleOCR 服务。";
        return false;
    }
    const DWORD flags = parsed.secure ? WINHTTP_FLAG_SECURE : 0;
    ScopedWinHttpHandle request(WinHttpOpenRequest(connect.Get(), L"POST", parsed.path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
    if (!request) {
        error = L"无法创建 PaddleOCR 请求。";
        return false;
    }
    std::wstring headers = L"Content-Type: application/json; charset=utf-8\r\nAccept: application/json\r\n";
    if (!token.empty()) {
        headers += L"Authorization: token " + token + L"\r\n";
    }
    if (!WinHttpAddRequestHeaders(request.Get(), headers.c_str(), static_cast<DWORD>(-1), WINHTTP_ADDREQ_FLAG_ADD)) {
        error = L"无法设置 PaddleOCR 请求头。";
        return false;
    }
    if (jsonBody.size() > static_cast<size_t>((std::numeric_limits<DWORD>::max)())) {
        error = L"OCR 请求体过大。";
        return false;
    }
    const DWORD bodySize = static_cast<DWORD>(jsonBody.size());
    if (!WinHttpSendRequest(request.Get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, const_cast<char*>(jsonBody.data()), bodySize, bodySize, 0)) {
        error = L"PaddleOCR 请求发送失败。";
        return false;
    }
    if (!WinHttpReceiveResponse(request.Get(), nullptr)) {
        error = L"PaddleOCR 响应接收失败。";
        return false;
    }
    DWORD querySize = sizeof(statusCode);
    if (!WinHttpQueryHeaders(request.Get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &querySize, WINHTTP_NO_HEADER_INDEX)) {
        statusCode = 0;
    }
    responseBody.clear();
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.Get(), &available)) {
            error = L"读取 PaddleOCR 响应失败。";
            return false;
        }
        if (available == 0) {
            break;
        }
        const size_t oldSize = responseBody.size();
        responseBody.resize(oldSize + available);
        DWORD bytesRead = 0;
        if (!WinHttpReadData(request.Get(), responseBody.data() + oldSize, available, &bytesRead)) {
            error = L"读取 PaddleOCR 响应失败。";
            return false;
        }
        responseBody.resize(oldSize + bytesRead);
    }
    return true;
}

} // namespace

OcrResult OcrService::Recognize(const Image& image, const OcrRequestConfig& config) const {
    OcrResult result;
    result.title = L"OCR 识别失败";
    if (!image.IsValid()) {
        result.text = L"输入图像无效，无法执行 OCR。";
        return result;
    }
    const std::wstring apiUrl = NormalizeApiUrl(config.apiUrl);
    const std::wstring accessToken = Trim(config.accessToken);
    if (apiUrl.empty()) {
        result.title = L"OCR 未配置";
        result.text = L"请先在设置中填写 PaddleOCR 服务 URL。";
        return result;
    }
    if (ContainsNoCase(apiUrl, L"aistudio") && accessToken.empty()) {
        result.title = L"OCR 未配置";
        result.text = L"AI Studio PaddleOCR 服务需要填写 Access Token。";
        return result;
    }

    struct AttemptResultV2 {
        bool success = false;
        std::wstring text;
        int confidence = -1;
        std::wstring errorTitle;
        std::wstring errorText;
        int visibleChars = 0;
        int preparedWidth = 0;
        int preparedHeight = 0;
    };

    const auto runAttemptV2 = [&](const Image& preparedImage) -> AttemptResultV2 {
        AttemptResultV2 attempt;
        attempt.preparedWidth = preparedImage.width;
        attempt.preparedHeight = preparedImage.height;
        if (!preparedImage.IsValid()) {
            attempt.errorTitle = L"OCR 识别失败";
            attempt.errorText = L"OCR 图像预处理失败。";
            return attempt;
        }

        std::vector<uint8_t> pngBytesV2;
        if (!ImageCodecUtil::EncodeImageToBytes(preparedImage, L"image/png", pngBytesV2)) {
            attempt.errorTitle = L"OCR 识别失败";
            attempt.errorText = L"截图编码失败，无法发送到 PaddleOCR 服务。";
            return attempt;
        }

        std::string imageBase64V2;
        if (!Base64Encode(pngBytesV2, imageBase64V2)) {
            attempt.errorTitle = L"OCR 识别失败";
            attempt.errorText = L"截图 Base64 编码失败。";
            return attempt;
        }

        const std::string requestBodyV2 =
            "{\"file\":\"" + imageBase64V2 +
            "\",\"fileType\":1,\"useDocOrientationClassify\":false,\"useDocUnwarping\":false,\"useTextlineOrientation\":false}";

        DWORD statusCodeV2 = 0;
        std::string responseBodyV2;
        std::wstring requestErrorV2;
        if (!PostJson(apiUrl, accessToken, requestBodyV2, statusCodeV2, responseBodyV2, requestErrorV2)) {
            attempt.errorTitle = L"OCR 服务调用失败";
            attempt.errorText = requestErrorV2;
            return attempt;
        }

        int errorCodeV2 = 0;
        TryGetJsonInt(responseBodyV2, "errorCode", errorCodeV2);
        std::string errorMsgUtf8V2;
        const bool hasErrorMsgV2 = TryGetJsonString(responseBodyV2, "errorMsg", errorMsgUtf8V2);
        if ((statusCodeV2 != 0 && statusCodeV2 != 200) || errorCodeV2 != 0) {
            attempt.errorTitle = L"OCR 服务调用失败";
            if (hasErrorMsgV2 && !errorMsgUtf8V2.empty()) {
                attempt.errorText = L"服务返回错误：" + Utf8ToWide(errorMsgUtf8V2);
            } else {
                attempt.errorText = L"HTTP 状态码：" + std::to_wstring(statusCodeV2);
                const std::wstring snippetV2 = BuildResponseSnippet(responseBodyV2);
                if (!snippetV2.empty()) {
                    attempt.errorText += L"；响应：" + snippetV2;
                }
            }
            return attempt;
        }

        std::string prunedResultBodyV2;
        const std::string& responseScopeV2 = ExtractJsonObjectForKey(responseBodyV2, "prunedResult", prunedResultBodyV2)
            ? prunedResultBodyV2
            : responseBodyV2;

        std::vector<std::string> recTextsUtf8V2;
        std::vector<double> recScoresV2;
        std::vector<std::array<int, 4>> recBoxesV2;
        CollectJsonStringArrayValues(responseScopeV2, "rec_texts", recTextsUtf8V2);
        CollectJsonNumberArrayValues(responseScopeV2, "rec_scores", recScoresV2);
        CollectJsonNumberMatrixRows(responseScopeV2, "rec_boxes", recBoxesV2);

        int confidenceV2 = -1;
        const std::wstring recognizedV2 = JoinRecognizedText(recTextsUtf8V2, recScoresV2, recBoxesV2, confidenceV2);
        if (recognizedV2.empty()) {
            attempt.errorTitle = L"OCR 未识别到文字";
            attempt.errorText = L"图像中没有检测到可识别的文字内容。";
            return attempt;
        }

        attempt.success = true;
        attempt.text = recognizedV2;
        attempt.confidence = confidenceV2;
        attempt.visibleChars = CountVisibleChars(recognizedV2);
        return attempt;
    };

    const auto attemptScoreV2 = [](const AttemptResultV2& attempt) -> double {
        if (!attempt.success) {
            return -1.0;
        }
        const double confidencePart = attempt.confidence >= 0 ? static_cast<double>(attempt.confidence) * 12.0 : 0.0;
        return confidencePart + static_cast<double>(attempt.visibleChars) * 0.15;
    };

    const bool darkThemeLikeV2 = LooksLikeDarkThemeTextImage(image);
    AttemptResultV2 bestAttemptV2 = runAttemptV2(PrepareImageForOcr(image));
    const bool needAlternateAttemptV2 =
        darkThemeLikeV2 ||
        !bestAttemptV2.success ||
        (bestAttemptV2.confidence >= 0 && bestAttemptV2.confidence < 88);

    if (needAlternateAttemptV2) {
        AttemptResultV2 alternateAttemptV2 = runAttemptV2(PrepareHighContrastOcrImage(image, darkThemeLikeV2));
        if (attemptScoreV2(alternateAttemptV2) > attemptScoreV2(bestAttemptV2)) {
            bestAttemptV2 = std::move(alternateAttemptV2);
        }
    }

    if (!bestAttemptV2.success) {
        result.title = bestAttemptV2.errorTitle.empty() ? L"OCR 识别失败" : bestAttemptV2.errorTitle;
        result.text = bestAttemptV2.errorText.empty() ? L"OCR 识别失败。" : bestAttemptV2.errorText;
        return result;
    }

    result.success = true;
    result.title = L"OCR 识别完成";
    result.text = bestAttemptV2.text;
    result.confidence = bestAttemptV2.confidence;
    Logger::Instance().Info(
        L"OCR completed via PaddleOCR API. conf=" + std::to_wstring(bestAttemptV2.confidence) +
        L" url=" + apiUrl +
        L" prepared=" + std::to_wstring(bestAttemptV2.preparedWidth) + L"x" + std::to_wstring(bestAttemptV2.preparedHeight) +
        L" dark_theme=" + std::to_wstring(darkThemeLikeV2 ? 1 : 0)
    );
    return result;

    Image prepared = PrepareImageForOcr(image);
    if (!prepared.IsValid()) {
        result.text = L"OCR 图像预处理失败。";
        return result;
    }

    std::vector<uint8_t> pngBytes;
    if (!ImageCodecUtil::EncodeImageToBytes(prepared, L"image/png", pngBytes)) {
        result.text = L"截图编码失败，无法发送到 PaddleOCR 服务。";
        return result;
    }
    std::string imageBase64;
    if (!Base64Encode(pngBytes, imageBase64)) {
        result.text = L"截图 Base64 编码失败。";
        return result;
    }

    const std::string requestBody =
        "{\"file\":\"" + imageBase64 +
        "\",\"fileType\":1,\"useDocOrientationClassify\":false,\"useDocUnwarping\":false,\"useTextlineOrientation\":false}";

    DWORD statusCode = 0;
    std::string responseBody;
    std::wstring requestError;
    if (!PostJson(apiUrl, accessToken, requestBody, statusCode, responseBody, requestError)) {
        result.title = L"OCR 服务调用失败";
        result.text = requestError;
        return result;
    }

    int errorCode = 0;
    TryGetJsonInt(responseBody, "errorCode", errorCode);
    std::string errorMsgUtf8;
    const bool hasErrorMsg = TryGetJsonString(responseBody, "errorMsg", errorMsgUtf8);
    if ((statusCode != 0 && statusCode != 200) || errorCode != 0) {
        result.title = L"OCR 服务调用失败";
        if (hasErrorMsg && !errorMsgUtf8.empty()) {
            result.text = L"服务返回错误：" + Utf8ToWide(errorMsgUtf8);
        } else {
            result.text = L"HTTP 状态码：" + std::to_wstring(statusCode);
            const std::wstring snippet = BuildResponseSnippet(responseBody);
            if (!snippet.empty()) {
                result.text += L"；响应：" + snippet;
            }
        }
        return result;
    }

    std::string prunedResultBody;
    const std::string& responseScope = ExtractJsonObjectForKey(responseBody, "prunedResult", prunedResultBody) ? prunedResultBody : responseBody;

    std::vector<std::string> recTextsUtf8;
    std::vector<double> recScores;
    std::vector<std::array<int, 4>> recBoxes;
    CollectJsonStringArrayValues(responseScope, "rec_texts", recTextsUtf8);
    CollectJsonNumberArrayValues(responseScope, "rec_scores", recScores);
    CollectJsonNumberMatrixRows(responseScope, "rec_boxes", recBoxes);

    int confidence = -1;
    const std::wstring recognized = JoinRecognizedText(recTextsUtf8, recScores, recBoxes, confidence);
    if (recognized.empty()) {
        result.title = L"OCR 未识别到文字";
        result.text = L"图像中没有检测到可识别的文字内容。";
        return result;
    }

    result.success = true;
    result.title = L"OCR 识别完成";
    result.text = recognized;
    result.confidence = confidence;
    Logger::Instance().Info(
        L"OCR completed via PaddleOCR API. conf=" + std::to_wstring(confidence) +
        L" url=" + apiUrl +
        L" prepared=" + std::to_wstring(prepared.width) + L"x" + std::to_wstring(prepared.height)
    );
    return result;
}
