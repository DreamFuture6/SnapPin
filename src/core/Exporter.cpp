#include "core/Exporter.h"
#include "core/AnnotationRenderer.h"
#include "core/ImageCodecUtil.h"
#include "core/Logger.h"

Image Exporter::Compose(const Image& fullImage, const RECT& selectionInFull, const std::vector<AnnotationShape>& shapes) const {
    if (!fullImage.IsValid()) {
        return {};
    }

    RECT crop = NormalizeRect(selectionInFull);
    crop.left = std::clamp(crop.left, 0L, static_cast<LONG>(fullImage.width));
    crop.top = std::clamp(crop.top, 0L, static_cast<LONG>(fullImage.height));
    crop.right = std::clamp(crop.right, 0L, static_cast<LONG>(fullImage.width));
    crop.bottom = std::clamp(crop.bottom, 0L, static_cast<LONG>(fullImage.height));

    Image out = fullImage.Crop(crop);
    if (!out.IsValid()) {
        return {};
    }

    for (const auto& shape : shapes) {
        if (shape.type == ToolType::Mosaic) {
            RECT rc = NormalizeRect(shape.rect);
            out = PixelateRect(out, rc, std::max(4, static_cast<int>(shape.stroke * 3.0f)));
        }
    }

    auto bmp = ImageCodecUtil::CreateBitmapFromImage(out);
    if (!bmp) {
        return out;
    }

    Gdiplus::Graphics graphics(bmp.get());
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);

    for (const auto& shape : shapes) {
        if (shape.type != ToolType::Mosaic) {
            AnnotationRenderer::DrawShape(graphics, shape);
        }
    }

    if (!ImageCodecUtil::CopyBitmapToImage(*bmp, out)) {
        return out;
    }

    return out;
}

bool Exporter::SaveImage(const Image& image, const std::filesystem::path& path, bool jpeg) const {
    if (!image.IsValid()) {
        return false;
    }

    auto bmp = ImageCodecUtil::CreateBitmapFromImage(image);
    if (!bmp) {
        return false;
    }

    CLSID clsid{};
    const wchar_t* mime = jpeg ? L"image/jpeg" : L"image/png";
    if (!ImageCodecUtil::FindEncoderClsid(mime, clsid)) {
        Logger::Instance().Error(L"No image encoder found.");
        return false;
    }

    const auto wpath = path.wstring();
    Gdiplus::Status status = Gdiplus::Ok;
    if (jpeg) {
        Gdiplus::EncoderParameters params{};
        params.Count = 1;
        params.Parameter[0].Guid = Gdiplus::EncoderQuality;
        params.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
        params.Parameter[0].NumberOfValues = 1;
        ULONG quality = 90;
        params.Parameter[0].Value = &quality;
        status = bmp->Save(wpath.c_str(), &clsid, &params);
    } else {
        status = bmp->Save(wpath.c_str(), &clsid, nullptr);
    }

    return status == Gdiplus::Ok;
}
