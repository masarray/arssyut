#pragma once

#include "arzoom-math.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace arzoom {

enum class ClickType : uint8_t {
    None = 0,
    Left = 1,
    Right = 2,
    Middle = 3,
};

struct ClickEvent {
    Vec2 content_position{0.5f, 0.5f};
    float age_seconds = 0.0f;
    ClickType type = ClickType::None;
    uint32_t generation = 0;

    bool active() const { return type != ClickType::None; }
};

inline float click_lifetime_seconds(ClickType type)
{
    switch (type) {
    case ClickType::Right:
        return 0.50f;
    case ClickType::Middle:
        return 0.32f;
    case ClickType::Left:
        return 0.44f;
    case ClickType::None:
    default:
        return 0.0f;
    }
}

/*
 * Fixed-size, allocation-free click history. Four slots are enough to preserve
 * overlapping human click feedback without turning the hot path into a
 * particle system. New events reuse an inactive slot first, otherwise the
 * oldest generation.
 */
class ClickVisualState {
public:
    static constexpr size_t kSlotCount = 4;

    void clear()
    {
        for (auto &event : events_)
            event = {};
    }

    void advance(float dt)
    {
        const float safe_dt = std::clamp(dt, 0.0f, 0.10f);
        for (auto &event : events_) {
            if (!event.active())
                continue;
            event.age_seconds += safe_dt;
            if (event.age_seconds >= click_lifetime_seconds(event.type))
                event = {};
        }
    }

    void push(ClickType type, Vec2 content_position)
    {
        if (type == ClickType::None)
            return;

        content_position.x = std::clamp(content_position.x, 0.0f, 1.0f);
        content_position.y = std::clamp(content_position.y, 0.0f, 1.0f);

        size_t slot = kSlotCount;
        for (size_t i = 0; i < kSlotCount; ++i) {
            if (!events_[i].active()) {
                slot = i;
                break;
            }
        }

        if (slot == kSlotCount) {
            slot = 0;
            for (size_t i = 1; i < kSlotCount; ++i) {
                if (events_[i].generation < events_[slot].generation)
                    slot = i;
            }
        }

        ++generation_;
        if (generation_ == 0)
            generation_ = 1;
        events_[slot] = {content_position, 0.0f, type, generation_};
    }

    bool has_active() const
    {
        for (const auto &event : events_) {
            if (event.active())
                return true;
        }
        return false;
    }

    size_t active_count() const
    {
        size_t count = 0;
        for (const auto &event : events_)
            count += event.active() ? 1u : 0u;
        return count;
    }

    const ClickEvent &slot(size_t index) const { return events_[index]; }

private:
    std::array<ClickEvent, kSlotCount> events_{};
    uint32_t generation_ = 0;
};

/*
 * A click is stored in source/content coordinates. The same camera transform
 * used by the shader projects it into output space every frame, so the pulse
 * stays attached to content while zoom/pan is moving.
 */
inline Vec2 project_content_to_output(Vec2 content_position,
                                      Vec2 camera_center,
                                      float zoom)
{
    const float safe_zoom = std::max(zoom, 1.0f);
    return {
        0.5f + (content_position.x - camera_center.x) * safe_zoom,
        0.5f + (content_position.y - camera_center.y) * safe_zoom,
    };
}

} // namespace arzoom
