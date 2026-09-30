#pragma once

#ifdef _WIN32

#include "app/recorder_session.hpp"

#include <Windows.h>

#include <vector>

namespace arssyut::app {

[[nodiscard]] std::vector<RecorderTarget>
enumerate_recorder_targets(HWND own_window);

} // namespace arssyut::app

#endif
