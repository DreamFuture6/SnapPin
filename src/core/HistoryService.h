#pragma once
#include "common.h"

struct HistoryItem {
    std::wstring filePath;
    std::wstring createdAt;
    int width = 0;
    int height = 0;
};

class HistoryService {
public:
    bool Initialize(const std::filesystem::path& baseDir, int limit);

    bool Add(const HistoryItem& item);
    std::vector<HistoryItem> List() const;
    bool RemoveByPath(const std::wstring& filePath, bool deleteFile);
    bool RenamePath(const std::wstring& oldPath, const std::wstring& newPath);
    bool ClearAll(bool deleteFiles);
    bool Compact();
    std::filesystem::path IndexPath() const { return indexPath_; }
    std::filesystem::path RootDir() const { return rootDir_; }

private:
    void PruneIfNeeded(std::vector<HistoryItem>& items) const;
    bool WriteIndex(const std::vector<HistoryItem>& items) const;

    std::filesystem::path rootDir_;
    std::filesystem::path indexPath_;
    int limit_ = 100;
};
