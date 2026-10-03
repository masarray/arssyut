#pragma once

#ifdef _WIN32

#include <string>
#include <vector>

namespace arssyut::app {

struct DeviceChoice {
    std::wstring id;
    std::wstring name;
};

[[nodiscard]]
std::vector<DeviceChoice>
enumerate_microphones();

[[nodiscard]]
std::vector<DeviceChoice>
enumerate_cameras();

} // namespace arssyut::app

#endif
