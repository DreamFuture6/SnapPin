#pragma once
#include "common.h"
#include "core/Image.h"
#include "core/Annotation.h"

class Exporter {
public:
    Exporter() = default;
    ~Exporter() = default;

    Image Compose(const Image& fullImage, const RECT& selectionInFull, const std::vector<AnnotationShape>& shapes) const;
    bool SaveImage(const Image& image, const std::filesystem::path& path, bool jpeg) const;

};
