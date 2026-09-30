#include "platform/windows/graphics/d3d11_device.hpp"

#include <iostream>

int main()
{
    using arssyut::windows::D3D11Device;
    using arssyut::windows::D3D11DevicePreference;

    auto result = D3D11Device::create(
        D3D11DevicePreference::WarpForTesting,
        false);

    if (!result) {
        std::cerr << "D3D11 WARP device creation failed; HRESULT detail=0x"
                  << std::hex << result.status().detail << '\n';
        return 1;
    }

    const auto &device = result.value();
    if (!device || !device->valid()) {
        std::cerr << "D3D11 owner returned invalid resources\n";
        return 1;
    }

    if (device->feature_level() < D3D_FEATURE_LEVEL_10_0) {
        std::cerr << "Unexpected D3D11 feature level\n";
        return 1;
    }

    std::cout << "PASS: deterministic D3D11 WARP ownership test\n";
    return 0;
}
