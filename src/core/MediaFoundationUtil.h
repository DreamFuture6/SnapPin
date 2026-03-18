#pragma once

#include "common.h"

namespace MediaFoundationUtil {

bool EnsureStartup();
void ShutdownForProcessExit();
std::wstring HResultToText(HRESULT hr);

} // namespace MediaFoundationUtil
