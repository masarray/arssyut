#pragma once

#ifdef _WIN32

#include "core/result/result.hpp"
#include "core/result/status.hpp"

#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace arssyut::windows {

struct CursorShapeView {
    ID3D11ShaderResourceView *srv = nullptr;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t hotspot_x = 0;
    std::uint32_t hotspot_y = 0;

    [[nodiscard]] bool valid() const noexcept
    {
        return srv != nullptr &&
            width > 0 &&
            height > 0;
    }
};

class CursorShapeCache final {
public:
    static constexpr std::size_t slot_count = 8;
    static constexpr UINT canvas_size = 256;

    CursorShapeCache(const CursorShapeCache &) = delete;
    CursorShapeCache &operator=(const CursorShapeCache &) = delete;
    ~CursorShapeCache();

    [[nodiscard]]
    static arssyut::core::Result<std::unique_ptr<CursorShapeCache>>
    create(ID3D11Device *device) noexcept;

    [[nodiscard]] arssyut::core::Status select(
        ID3D11DeviceContext *context,
        HCURSOR cursor) noexcept;

    [[nodiscard]] CursorShapeView active() const noexcept;

private:
    CursorShapeCache() = default;

    struct Slot {
        HCURSOR handle = nullptr;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t hotspot_x = 0;
        std::uint32_t hotspot_y = 0;
        std::uint64_t last_used = 0;
        bool valid = false;
    };

    [[nodiscard]] arssyut::core::Status initialize(
        ID3D11Device *device) noexcept;

    [[nodiscard]] arssyut::core::Status rasterize(
        ID3D11DeviceContext *context,
        HCURSOR cursor,
        Slot &slot) noexcept;

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    std::array<Slot, slot_count> slots_{};
    std::size_t active_slot_ = slot_count;
    std::uint64_t use_generation_ = 0;

    HDC dc_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ old_bitmap_ = nullptr;
    void *bits_ = nullptr;
    std::unique_ptr<std::uint32_t[]> scratch_;
};

} // namespace arssyut::windows

#endif
