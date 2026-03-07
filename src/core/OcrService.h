#pragma once
#include "common.h"
#include "core/Image.h"

struct OcrResult {
    bool success = false;
    std::wstring title;
    std::wstring text;
    int confidence = -1;
};

struct OcrRequestConfig {
    std::wstring apiUrl;
    std::wstring accessToken;
};

class OcrService {
public:
    OcrResult Recognize(const Image& image, const OcrRequestConfig& config) const;
};
