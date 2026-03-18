#pragma once

#include "common.h"

namespace FfmpegExporter {

struct VideoInfo {
    int width = 0;
    int height = 0;
    int fps = 0;
};

enum class CompressionProfile {
    Light,
    Standard,
};

bool ProbeRecordingVideoInfo(const std::filesystem::path& inputPath,
                             VideoInfo& outInfo,
                             std::wstring& errorMessage);

bool ExportRecording(const std::filesystem::path& inputPath,
                     const std::filesystem::path& outputPath,
                     uint32_t videoBitrate,
                     CompressionProfile profile,
                     std::wstring& errorMessage);

} // namespace FfmpegExporter
