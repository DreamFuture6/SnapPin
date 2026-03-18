#include "core/FfmpegExporter.h"
#include "core/Logger.h"

#if defined(SNAPPIN_HAS_FFMPEG)
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/opt.h>
#include <libavutil/rational.h>
#include <libswscale/swscale.h>
}
#include <cmath>
#include <cstring>

namespace {

std::string PathToUtf8(const std::filesystem::path& path) {
    const std::u8string u8 = path.u8string();
    return std::string(u8.begin(), u8.end());
}

std::wstring AvErrorToText(int ffmpegError) {
    char buffer[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(ffmpegError, buffer, static_cast<size_t>(sizeof(buffer)));
    return Utf8ToWide(buffer);
}

struct ScopedInputFormatContext {
    ~ScopedInputFormatContext() {
        if (context) {
            avformat_close_input(&context);
        }
    }
    AVFormatContext* context = nullptr;
};

struct ScopedOutputFormatContext {
    ~ScopedOutputFormatContext() {
        if (!context) {
            return;
        }
        if ((context->oformat->flags & AVFMT_NOFILE) == 0 && context->pb) {
            avio_closep(&context->pb);
        }
        avformat_free_context(context);
    }
    AVFormatContext* context = nullptr;
};

struct ScopedCodecContext {
    ~ScopedCodecContext() {
        if (context) {
            avcodec_free_context(&context);
        }
    }
    AVCodecContext* context = nullptr;
};

struct ScopedFrame {
    ~ScopedFrame() {
        if (frame) {
            av_frame_free(&frame);
        }
    }
    AVFrame* frame = nullptr;
};

struct ScopedPacket {
    ~ScopedPacket() {
        if (packet) {
            av_packet_free(&packet);
        }
    }
    AVPacket* packet = nullptr;
};

struct ScopedSwsContext {
    ~ScopedSwsContext() {
        if (context) {
            sws_freeContext(context);
        }
    }
    SwsContext* context = nullptr;
};

bool EnsureMonotonicPts(int64_t& nextPts, int64_t& pts) {
    if (pts == AV_NOPTS_VALUE || pts < nextPts) {
        pts = nextPts;
    }
    if (pts < 0) {
        pts = 0;
    }
    nextPts = pts + 1;
    return true;
}

int AlignToEven(int value) {
    if (value <= 2) {
        return 2;
    }
    return value & ~1;
}

const char* CrfByProfile(FfmpegExporter::CompressionProfile profile) {
    switch (profile) {
    case FfmpegExporter::CompressionProfile::Light:
        return "28";
    case FfmpegExporter::CompressionProfile::Standard:
    default:
        return "23";
    }
}

uint32_t ClampExportBitrate(uint32_t requestedBitrate, FfmpegExporter::CompressionProfile profile) {
    switch (profile) {
    case FfmpegExporter::CompressionProfile::Light:
        return std::clamp<uint32_t>(requestedBitrate, 250'000U, 1'800'000U);
    case FfmpegExporter::CompressionProfile::Standard:
    default:
        return std::clamp<uint32_t>(requestedBitrate, 600'000U, 3'500'000U);
    }
}

const char* MfQualityByProfile(FfmpegExporter::CompressionProfile profile) {
    switch (profile) {
    case FfmpegExporter::CompressionProfile::Light:
        return "40";
    case FfmpegExporter::CompressionProfile::Standard:
    default:
        return "55";
    }
}

} // namespace

bool FfmpegExporter::ProbeRecordingVideoInfo(const std::filesystem::path& inputPath,
                                             VideoInfo& outInfo,
                                             std::wstring& errorMessage) {
    outInfo = {};
    errorMessage.clear();

    const std::string inputPathUtf8 = PathToUtf8(inputPath);
    ScopedInputFormatContext input;
    if (const int ret = avformat_open_input(&input.context, inputPathUtf8.c_str(), nullptr, nullptr); ret < 0) {
        errorMessage = L"FFmpeg 打开输入文件失败: " + AvErrorToText(ret);
        return false;
    }
    if (const int ret = avformat_find_stream_info(input.context, nullptr); ret < 0) {
        errorMessage = L"FFmpeg 读取输入流信息失败: " + AvErrorToText(ret);
        return false;
    }

    const int inputVideoIndex = av_find_best_stream(input.context, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (inputVideoIndex < 0) {
        errorMessage = L"FFmpeg 未找到视频流。";
        return false;
    }
    AVStream* inputVideoStream = input.context->streams[inputVideoIndex];
    if (!inputVideoStream || !inputVideoStream->codecpar) {
        errorMessage = L"FFmpeg 输入视频流无效。";
        return false;
    }

    AVRational fps = inputVideoStream->avg_frame_rate;
    if (fps.num <= 0 || fps.den <= 0) {
        fps = inputVideoStream->r_frame_rate;
    }
    if (fps.num <= 0 || fps.den <= 0) {
        fps = av_guess_frame_rate(input.context, inputVideoStream, nullptr);
    }
    if (fps.num <= 0 || fps.den <= 0) {
        fps = AVRational{24, 1};
    }

    outInfo.width = std::max(0, inputVideoStream->codecpar->width);
    outInfo.height = std::max(0, inputVideoStream->codecpar->height);
    outInfo.fps = std::max(1, static_cast<int>(std::lround(av_q2d(fps))));
    return outInfo.width > 0 && outInfo.height > 0;
}

bool FfmpegExporter::ExportRecording(const std::filesystem::path& inputPath,
                                     const std::filesystem::path& outputPath,
                                     uint32_t videoBitrate,
                                     CompressionProfile profile,
                                     std::wstring& errorMessage) {
    errorMessage.clear();

    const std::string inputPathUtf8 = PathToUtf8(inputPath);
    const std::string outputPathUtf8 = PathToUtf8(outputPath);

    std::error_code ec;
    std::filesystem::create_directories(outputPath.parent_path(), ec);

    ScopedInputFormatContext input;
    if (const int ret = avformat_open_input(&input.context, inputPathUtf8.c_str(), nullptr, nullptr); ret < 0) {
        errorMessage = L"FFmpeg 打开输入文件失败: " + AvErrorToText(ret);
        return false;
    }
    if (const int ret = avformat_find_stream_info(input.context, nullptr); ret < 0) {
        errorMessage = L"FFmpeg 读取输入流信息失败: " + AvErrorToText(ret);
        return false;
    }

    const int inputVideoIndex = av_find_best_stream(input.context, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (inputVideoIndex < 0) {
        errorMessage = L"FFmpeg 未找到视频流。";
        return false;
    }
    int inputAudioIndex = av_find_best_stream(input.context, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (inputAudioIndex < 0) {
        inputAudioIndex = -1;
    }

    AVStream* inputVideoStream = input.context->streams[inputVideoIndex];
    if (!inputVideoStream || !inputVideoStream->codecpar) {
        errorMessage = L"FFmpeg 输入视频流无效。";
        return false;
    }

    const AVCodec* decoder = avcodec_find_decoder(inputVideoStream->codecpar->codec_id);
    if (!decoder) {
        errorMessage = L"FFmpeg 未找到输入视频解码器。";
        return false;
    }

    ScopedCodecContext decoderContext;
    decoderContext.context = avcodec_alloc_context3(decoder);
    if (!decoderContext.context) {
        errorMessage = L"FFmpeg 分配解码器上下文失败。";
        return false;
    }
    if (const int ret = avcodec_parameters_to_context(decoderContext.context, inputVideoStream->codecpar); ret < 0) {
        errorMessage = L"FFmpeg 复制解码参数失败: " + AvErrorToText(ret);
        return false;
    }
    if (const int ret = avcodec_open2(decoderContext.context, decoder, nullptr); ret < 0) {
        errorMessage = L"FFmpeg 打开视频解码器失败: " + AvErrorToText(ret);
        return false;
    }

    ScopedOutputFormatContext output;
    if (const int ret = avformat_alloc_output_context2(&output.context, nullptr, nullptr, outputPathUtf8.c_str()); ret < 0 || !output.context) {
        errorMessage = L"FFmpeg 创建输出上下文失败: " + AvErrorToText(ret);
        return false;
    }

    const AVCodec* encoder = avcodec_find_encoder_by_name("libx264");
    if (!encoder) {
        encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
    }
    if (!encoder) {
        errorMessage = L"FFmpeg 未找到 H.264 编码器。";
        return false;
    }

    AVStream* outputVideoStream = avformat_new_stream(output.context, nullptr);
    if (!outputVideoStream) {
        errorMessage = L"FFmpeg 创建输出视频流失败。";
        return false;
    }

    ScopedCodecContext encoderContext;
    encoderContext.context = avcodec_alloc_context3(encoder);
    if (!encoderContext.context) {
        errorMessage = L"FFmpeg 分配编码器上下文失败。";
        return false;
    }

    const AVRational sourceFrameRate = [&]() {
        AVRational fps = inputVideoStream->avg_frame_rate;
        if (fps.num <= 0 || fps.den <= 0) {
            fps = inputVideoStream->r_frame_rate;
        }
        if (fps.num <= 0 || fps.den <= 0) {
            fps = av_guess_frame_rate(input.context, inputVideoStream, nullptr);
        }
        if (fps.num <= 0 || fps.den <= 0) {
            fps = AVRational{24, 1};
        }
        return fps;
    }();

    const uint32_t targetBitrate = ClampExportBitrate(videoBitrate, profile);

    encoderContext.context->codec_id = encoder->id;
    encoderContext.context->codec_type = AVMEDIA_TYPE_VIDEO;
    const int outputWidth = AlignToEven(decoderContext.context->width);
    const int outputHeight = AlignToEven(decoderContext.context->height);
    encoderContext.context->bit_rate = static_cast<int64_t>(targetBitrate);
    encoderContext.context->width = outputWidth;
    encoderContext.context->height = outputHeight;
    encoderContext.context->sample_aspect_ratio = AVRational{1, 1};
    encoderContext.context->pix_fmt = AV_PIX_FMT_YUV420P;
    encoderContext.context->time_base = av_inv_q(sourceFrameRate);
    if (encoderContext.context->time_base.num <= 0 || encoderContext.context->time_base.den <= 0) {
        encoderContext.context->time_base = AVRational{1, 24};
    }
    encoderContext.context->framerate = sourceFrameRate;
    const int sourceFpsRounded = std::max(1, static_cast<int>(std::lround(av_q2d(sourceFrameRate))));
    encoderContext.context->gop_size = std::max(24, sourceFpsRounded * 2);
    encoderContext.context->max_b_frames = 0;
    encoderContext.context->thread_count = std::max(1u, std::thread::hardware_concurrency());
    const uint32_t maxRate = profile == CompressionProfile::Light
                                 ? static_cast<uint32_t>((static_cast<uint64_t>(targetBitrate) * 130ULL) / 100ULL)
                                 : static_cast<uint32_t>((static_cast<uint64_t>(targetBitrate) * 120ULL) / 100ULL);
    const uint32_t minRate = profile == CompressionProfile::Light
                                 ? static_cast<uint32_t>((static_cast<uint64_t>(targetBitrate) * 60ULL) / 100ULL)
                                 : static_cast<uint32_t>((static_cast<uint64_t>(targetBitrate) * 80ULL) / 100ULL);
    const uint32_t bufferSize = profile == CompressionProfile::Light ? targetBitrate * 3U : targetBitrate * 2U;
    encoderContext.context->rc_max_rate = static_cast<int64_t>(maxRate);
    encoderContext.context->rc_min_rate = static_cast<int64_t>(minRate);
    encoderContext.context->rc_buffer_size = static_cast<int>(bufferSize);
    encoderContext.context->bit_rate_tolerance = static_cast<int>(targetBitrate / 2U);

    if ((output.context->oformat->flags & AVFMT_GLOBALHEADER) != 0) {
        encoderContext.context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    const bool useX264 = (encoder->name && std::strcmp(encoder->name, "libx264") == 0);
    const bool useMediaFoundation = (encoder->name && std::strcmp(encoder->name, "h264_mf") == 0);
    AVDictionary* encoderOptions = nullptr;
    if (useX264) {
        av_dict_set(&encoderOptions, "preset", "medium", 0);
        av_dict_set(&encoderOptions, "tune", "film", 0);
        av_dict_set(&encoderOptions, "crf", CrfByProfile(profile), 0);
    }
    if (useMediaFoundation) {
        av_dict_set(&encoderOptions, "rate_control", "cbr", 0);
        av_dict_set(&encoderOptions, "quality", MfQualityByProfile(profile), 0);
    }
    const std::string maxRateText = std::to_string(maxRate);
    const std::string minRateText = std::to_string(minRate);
    const std::string bufSizeText = std::to_string(bufferSize);
    av_dict_set(&encoderOptions, "maxrate", maxRateText.c_str(), 0);
    av_dict_set(&encoderOptions, "minrate", minRateText.c_str(), 0);
    av_dict_set(&encoderOptions, "bufsize", bufSizeText.c_str(), 0);
    if (useX264) {
        av_dict_set(&encoderOptions, "x264-params", "keyint=60:min-keyint=30:scenecut=40", 0);
    }
    const int openEncoderRet = avcodec_open2(encoderContext.context, encoder, &encoderOptions);
    av_dict_free(&encoderOptions);
    if (openEncoderRet < 0) {
        errorMessage = L"FFmpeg 打开视频编码器失败: " + AvErrorToText(openEncoderRet);
        return false;
    }

    if (const int ret = avcodec_parameters_from_context(outputVideoStream->codecpar, encoderContext.context); ret < 0) {
        errorMessage = L"FFmpeg 设置输出视频参数失败: " + AvErrorToText(ret);
        return false;
    }
    outputVideoStream->time_base = encoderContext.context->time_base;
    outputVideoStream->sample_aspect_ratio = encoderContext.context->sample_aspect_ratio;

    AVStream* outputAudioStream = nullptr;
    if (inputAudioIndex >= 0) {
        AVStream* inputAudioStream = input.context->streams[inputAudioIndex];
        if (inputAudioStream && inputAudioStream->codecpar) {
            outputAudioStream = avformat_new_stream(output.context, nullptr);
            if (!outputAudioStream) {
                errorMessage = L"FFmpeg 创建输出音频流失败。";
                return false;
            }
            if (const int ret = avcodec_parameters_copy(outputAudioStream->codecpar, inputAudioStream->codecpar); ret < 0) {
                errorMessage = L"FFmpeg 复制音频流参数失败: " + AvErrorToText(ret);
                return false;
            }
            outputAudioStream->codecpar->codec_tag = 0;
            outputAudioStream->time_base = inputAudioStream->time_base;
        }
    }

    if ((output.context->oformat->flags & AVFMT_NOFILE) == 0) {
        if (const int ret = avio_open(&output.context->pb, outputPathUtf8.c_str(), AVIO_FLAG_WRITE); ret < 0) {
            errorMessage = L"FFmpeg 打开输出文件失败: " + AvErrorToText(ret);
            return false;
        }
    }

    AVDictionary* formatOptions = nullptr;
    av_dict_set(&formatOptions, "movflags", "+faststart", 0);
    const int writeHeaderRet = avformat_write_header(output.context, &formatOptions);
    av_dict_free(&formatOptions);
    if (writeHeaderRet < 0) {
        errorMessage = L"FFmpeg 写入文件头失败: " + AvErrorToText(writeHeaderRet);
        return false;
    }

    ScopedPacket inputPacket;
    inputPacket.packet = av_packet_alloc();
    ScopedPacket encodedPacket;
    encodedPacket.packet = av_packet_alloc();
    ScopedFrame decodedFrame;
    decodedFrame.frame = av_frame_alloc();
    ScopedFrame convertedFrame;
    convertedFrame.frame = av_frame_alloc();
    if (!inputPacket.packet || !encodedPacket.packet || !decodedFrame.frame || !convertedFrame.frame) {
        errorMessage = L"FFmpeg 分配帧/包对象失败。";
        return false;
    }

    convertedFrame.frame->format = encoderContext.context->pix_fmt;
    convertedFrame.frame->width = encoderContext.context->width;
    convertedFrame.frame->height = encoderContext.context->height;
    if (const int ret = av_frame_get_buffer(convertedFrame.frame, 32); ret < 0) {
        errorMessage = L"FFmpeg 分配输出帧缓冲失败: " + AvErrorToText(ret);
        return false;
    }

    ScopedSwsContext scaler;
    scaler.context = sws_getContext(decoderContext.context->width,
                                    decoderContext.context->height,
                                    decoderContext.context->pix_fmt,
                                    outputWidth,
                                    outputHeight,
                                    encoderContext.context->pix_fmt,
                                    SWS_BICUBIC,
                                    nullptr,
                                    nullptr,
                                    nullptr);
    if (!scaler.context) {
        errorMessage = L"FFmpeg 初始化像素格式转换器失败。";
        return false;
    }

    int64_t nextVideoPts = 0;
    bool wroteAnyPacket = false;

    auto encodeAndWrite = [&](AVFrame* frame) -> bool {
        int ret = avcodec_send_frame(encoderContext.context, frame);
        if (ret < 0) {
            errorMessage = L"FFmpeg 发送编码帧失败: " + AvErrorToText(ret);
            return false;
        }

        while (ret >= 0) {
            ret = avcodec_receive_packet(encoderContext.context, encodedPacket.packet);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                return true;
            }
            if (ret < 0) {
                errorMessage = L"FFmpeg 接收编码包失败: " + AvErrorToText(ret);
                return false;
            }
            av_packet_rescale_ts(encodedPacket.packet, encoderContext.context->time_base, outputVideoStream->time_base);
            encodedPacket.packet->stream_index = outputVideoStream->index;
            ret = av_interleaved_write_frame(output.context, encodedPacket.packet);
            av_packet_unref(encodedPacket.packet);
            if (ret < 0) {
                errorMessage = L"FFmpeg 写入视频包失败: " + AvErrorToText(ret);
                return false;
            }
            wroteAnyPacket = true;
        }
        return true;
    };

    while (av_read_frame(input.context, inputPacket.packet) >= 0) {
        if (inputPacket.packet->stream_index == inputVideoIndex) {
            int ret = avcodec_send_packet(decoderContext.context, inputPacket.packet);
            av_packet_unref(inputPacket.packet);
            if (ret < 0) {
                errorMessage = L"FFmpeg 发送解码包失败: " + AvErrorToText(ret);
                return false;
            }

            while (ret >= 0) {
                ret = avcodec_receive_frame(decoderContext.context, decodedFrame.frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                }
                if (ret < 0) {
                    errorMessage = L"FFmpeg 解码视频帧失败: " + AvErrorToText(ret);
                    return false;
                }

                if (const int writableRet = av_frame_make_writable(convertedFrame.frame); writableRet < 0) {
                    errorMessage = L"FFmpeg 输出帧不可写: " + AvErrorToText(writableRet);
                    return false;
                }
                sws_scale(scaler.context,
                          decodedFrame.frame->data,
                          decodedFrame.frame->linesize,
                          0,
                          decoderContext.context->height,
                          convertedFrame.frame->data,
                          convertedFrame.frame->linesize);

                int64_t pts = decodedFrame.frame->best_effort_timestamp;
                if (pts == AV_NOPTS_VALUE) {
                    pts = decodedFrame.frame->pts;
                }
                if (pts != AV_NOPTS_VALUE) {
                    pts = av_rescale_q(pts, inputVideoStream->time_base, encoderContext.context->time_base);
                }
                EnsureMonotonicPts(nextVideoPts, pts);
                convertedFrame.frame->pts = pts;

                if (!encodeAndWrite(convertedFrame.frame)) {
                    return false;
                }
                av_frame_unref(decodedFrame.frame);
            }
        } else if (inputAudioIndex >= 0 && outputAudioStream && inputPacket.packet->stream_index == inputAudioIndex) {
            AVStream* inputAudioStream = input.context->streams[inputAudioIndex];
            av_packet_rescale_ts(inputPacket.packet, inputAudioStream->time_base, outputAudioStream->time_base);
            inputPacket.packet->stream_index = outputAudioStream->index;
            const int ret = av_interleaved_write_frame(output.context, inputPacket.packet);
            av_packet_unref(inputPacket.packet);
            if (ret < 0) {
                errorMessage = L"FFmpeg 写入音频包失败: " + AvErrorToText(ret);
                return false;
            }
            wroteAnyPacket = true;
        } else {
            av_packet_unref(inputPacket.packet);
        }
    }

    int flushDecodeRet = avcodec_send_packet(decoderContext.context, nullptr);
    if (flushDecodeRet >= 0) {
        while (flushDecodeRet >= 0) {
            flushDecodeRet = avcodec_receive_frame(decoderContext.context, decodedFrame.frame);
            if (flushDecodeRet == AVERROR(EAGAIN) || flushDecodeRet == AVERROR_EOF) {
                break;
            }
            if (flushDecodeRet < 0) {
                errorMessage = L"FFmpeg 刷新解码器失败: " + AvErrorToText(flushDecodeRet);
                return false;
            }

            if (const int writableRet = av_frame_make_writable(convertedFrame.frame); writableRet < 0) {
                errorMessage = L"FFmpeg 输出帧不可写: " + AvErrorToText(writableRet);
                return false;
            }
            sws_scale(scaler.context,
                      decodedFrame.frame->data,
                      decodedFrame.frame->linesize,
                      0,
                      decoderContext.context->height,
                      convertedFrame.frame->data,
                      convertedFrame.frame->linesize);

            int64_t pts = decodedFrame.frame->best_effort_timestamp;
            if (pts == AV_NOPTS_VALUE) {
                pts = decodedFrame.frame->pts;
            }
            if (pts != AV_NOPTS_VALUE) {
                pts = av_rescale_q(pts, inputVideoStream->time_base, encoderContext.context->time_base);
            }
            EnsureMonotonicPts(nextVideoPts, pts);
            convertedFrame.frame->pts = pts;

            if (!encodeAndWrite(convertedFrame.frame)) {
                return false;
            }
            av_frame_unref(decodedFrame.frame);
        }
    }

    if (!encodeAndWrite(nullptr)) {
        return false;
    }

    if (!wroteAnyPacket) {
        errorMessage = L"FFmpeg 未写入任何输出包。";
        return false;
    }

    if (const int ret = av_write_trailer(output.context); ret < 0) {
        errorMessage = L"FFmpeg 写入文件尾失败: " + AvErrorToText(ret);
        return false;
    }

    return true;
}

#else

bool FfmpegExporter::ProbeRecordingVideoInfo(const std::filesystem::path&,
                                             VideoInfo& outInfo,
                                             std::wstring& errorMessage) {
    outInfo = {};
    errorMessage = L"当前构建未启用 FFmpeg 导出能力，请使用包含 FFmpeg 依赖的构建重新编译 SnapPin。";
    return false;
}

bool FfmpegExporter::ExportRecording(const std::filesystem::path&,
                                     const std::filesystem::path&,
                                     uint32_t,
                                     CompressionProfile,
                                     std::wstring& errorMessage) {
    errorMessage = L"当前构建未启用 FFmpeg 导出能力，请使用包含 FFmpeg 依赖的构建重新编译 SnapPin。";
    Logger::Instance().Error(errorMessage);
    return false;
}

#endif
