#include "core/HistoryService.h"

namespace {
bool ParseHistoryLine(const std::wstring& line, HistoryItem& outItem) {
    if (line.empty()) {
        return false;
    }

    std::wistringstream ss(line);
    std::wstring token;
    HistoryItem item;
    if (!std::getline(ss, token, L'|')) {
        return false;
    }
    item.createdAt = token;
    if (!std::getline(ss, token, L'|')) {
        return false;
    }
    item.width = _wtoi(token.c_str());
    if (!std::getline(ss, token, L'|')) {
        return false;
    }
    item.height = _wtoi(token.c_str());
    if (!std::getline(ss, token)) {
        return false;
    }
    item.filePath = token;
    if (item.filePath.empty()) {
        return false;
    }

    outItem = std::move(item);
    return true;
}
} // namespace

bool HistoryService::Initialize(const std::filesystem::path& baseDir, int limit) {
    rootDir_ = baseDir;
    indexPath_ = rootDir_ / L"history_index.txt";
    limit_ = std::clamp(limit, 10, 100);
    std::error_code ec;
    std::filesystem::create_directories(rootDir_, ec);
    if (!std::filesystem::exists(indexPath_)) {
        std::wofstream out(indexPath_);
    }
    return true;
}

bool HistoryService::Add(const HistoryItem& item) {
    auto items = List();
    items.insert(items.begin(), item);
    PruneIfNeeded(items);
    return WriteIndex(items);
}

std::vector<HistoryItem> HistoryService::List() const {
    std::vector<HistoryItem> out;
    std::wifstream in(indexPath_);
    if (!in.is_open()) {
        return out;
    }

    std::wstring line;
    while (std::getline(in, line)) {
        HistoryItem item;
        if (!ParseHistoryLine(line, item)) {
            continue;
        }

        std::error_code ec;
        const std::filesystem::path filePath(item.filePath);
        if (!std::filesystem::exists(filePath, ec) || ec) {
            continue;
        }

        out.push_back(std::move(item));
    }
    return out;
}

bool HistoryService::RemoveByPath(const std::wstring& filePath, bool deleteFile) {
    auto items = List();
    bool removed = false;
    items.erase(std::remove_if(items.begin(), items.end(), [&](const HistoryItem& it) {
        if (_wcsicmp(it.filePath.c_str(), filePath.c_str()) == 0) {
            removed = true;
            if (deleteFile) {
                std::error_code ec;
                std::filesystem::remove(it.filePath, ec);
            }
            return true;
        }
        return false;
    }), items.end());

    if (!removed) {
        return false;
    }
    return WriteIndex(items);
}

bool HistoryService::RenamePath(const std::wstring& oldPath, const std::wstring& newPath) {
    if (oldPath.empty() || newPath.empty()) {
        return false;
    }

    std::vector<HistoryItem> items;
    std::wifstream in(indexPath_);
    if (!in.is_open()) {
        return false;
    }

    std::wstring line;
    while (std::getline(in, line)) {
        HistoryItem item;
        if (!ParseHistoryLine(line, item)) {
            continue;
        }
        items.push_back(std::move(item));
    }

    bool renamed = false;
    for (auto& item : items) {
        if (_wcsicmp(item.filePath.c_str(), oldPath.c_str()) == 0) {
            item.filePath = newPath;
            renamed = true;
            break;
        }
    }

    if (!renamed) {
        return false;
    }
    return WriteIndex(items);
}

bool HistoryService::ClearAll(bool deleteFiles) {
    auto items = List();
    if (deleteFiles) {
        for (const auto& it : items) {
            std::error_code ec;
            std::filesystem::remove(it.filePath, ec);
        }
    }
    items.clear();
    return WriteIndex(items);
}

bool HistoryService::Compact() {
    auto items = List();
    return WriteIndex(items);
}

void HistoryService::PruneIfNeeded(std::vector<HistoryItem>& items) const {
    if (static_cast<int>(items.size()) <= limit_) {
        return;
    }
    for (size_t i = static_cast<size_t>(limit_); i < items.size(); ++i) {
        std::error_code ec;
        std::filesystem::remove(items[i].filePath, ec);
    }
    items.resize(static_cast<size_t>(limit_));
}

bool HistoryService::WriteIndex(const std::vector<HistoryItem>& items) const {
    std::wofstream out(indexPath_, std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }
    for (const auto& it : items) {
        out << it.createdAt << L"|" << it.width << L"|" << it.height << L"|" << it.filePath << L"\n";
    }
    return true;
}
