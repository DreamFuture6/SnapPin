#pragma once
#include "common.h"

class CrashHandler {
public:
    static void Install(const std::filesystem::path& dumpDir);
};
