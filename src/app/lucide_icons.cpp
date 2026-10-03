#include "app/lucide_icons.hpp"

#ifdef _WIN32

#include <algorithm>
#include <cmath>
#include <iterator>

namespace arssyut::app {

namespace {

struct Transform {
    float scale = 1.0f;
    float ox = 0.0f;
    float oy = 0.0f;
};

[[nodiscard]] Transform transform_for(
    RECT bounds) noexcept
{
    const float width =
        static_cast<float>(
            std::max(
                1L,
                bounds.right - bounds.left));
    const float height =
        static_cast<float>(
            std::max(
                1L,
                bounds.bottom - bounds.top));
    const float scale =
        std::min(width, height) / 24.0f;

    return {
        scale,
        static_cast<float>(bounds.left) +
            (width - 24.0f * scale) * 0.5f,
        static_cast<float>(bounds.top) +
            (height - 24.0f * scale) * 0.5f};
}

[[nodiscard]] POINT p(
    const Transform &t,
    float x,
    float y) noexcept
{
    return {
        static_cast<LONG>(
            std::lround(
                t.ox + x * t.scale)),
        static_cast<LONG>(
            std::lround(
                t.oy + y * t.scale))};
}

[[nodiscard]] RECT r(
    const Transform &t,
    float left,
    float top,
    float right,
    float bottom) noexcept
{
    const POINT a =
        p(t, left, top);
    const POINT b =
        p(t, right, bottom);
    return {a.x, a.y, b.x, b.y};
}

void line(
    HDC dc,
    const Transform &t,
    float x1,
    float y1,
    float x2,
    float y2)
{
    const POINT a =
        p(t, x1, y1);
    const POINT b =
        p(t, x2, y2);
    MoveToEx(
        dc,
        a.x,
        a.y,
        nullptr);
    LineTo(
        dc,
        b.x,
        b.y);
}

void polyline(
    HDC dc,
    const Transform &t,
    const POINTF *points,
    int count)
{
    if (!points || count <= 0)
        return;

    POINT mapped[16]{};
    count =
        std::min(count, 16);

    for (int i = 0;
         i < count;
         ++i) {
        mapped[i] =
            p(
                t,
                points[i].x,
                points[i].y);
    }

    Polyline(
        dc,
        mapped,
        count);
}

void draw_monitor(
    HDC dc,
    const Transform &t)
{
    RECT body =
        r(t, 2, 3, 22, 17);
    RoundRect(
        dc,
        body.left,
        body.top,
        body.right,
        body.bottom,
        std::max(2, static_cast<int>(
            std::lround(4.0f * t.scale))),
        std::max(2, static_cast<int>(
            std::lround(4.0f * t.scale))));
    line(dc, t, 12, 17, 12, 21);
    line(dc, t, 8, 21, 16, 21);
}

void draw_app_window(
    HDC dc,
    const Transform &t)
{
    RECT body =
        r(t, 2, 4, 22, 20);
    RoundRect(
        dc,
        body.left,
        body.top,
        body.right,
        body.bottom,
        std::max(2, static_cast<int>(
            std::lround(4.0f * t.scale))),
        std::max(2, static_cast<int>(
            std::lround(4.0f * t.scale))));
    line(dc, t, 2, 8, 22, 8);
    line(dc, t, 6, 4, 6, 8);
    line(dc, t, 10, 4, 10, 8);
}

void draw_region(
    HDC dc,
    const Transform &t)
{
    line(dc, t, 3, 7, 3, 5);
    line(dc, t, 3, 5, 5, 3);
    line(dc, t, 5, 3, 7, 3);

    line(dc, t, 17, 3, 19, 3);
    line(dc, t, 19, 3, 21, 5);
    line(dc, t, 21, 5, 21, 7);

    line(dc, t, 21, 17, 21, 19);
    line(dc, t, 21, 19, 19, 21);
    line(dc, t, 19, 21, 17, 21);

    line(dc, t, 7, 21, 5, 21);
    line(dc, t, 5, 21, 3, 19);
    line(dc, t, 3, 19, 3, 17);

    line(dc, t, 7, 12, 17, 12);
}

void draw_gamepad(
    HDC dc,
    const Transform &t)
{
    // Lucide gamepad-2 geometry, simplified to native GDI primitives.
    const POINTF outline[] = {
        {6.7f, 5.0f},
        {4.4f, 5.0f},
        {3.0f, 6.5f},
        {2.5f, 10.5f},
        {2.0f, 16.0f},
        {2.0f, 18.0f},
        {5.0f, 19.0f},
        {8.4f, 16.0f},
        {15.6f, 16.0f},
        {19.0f, 19.0f},
        {22.0f, 18.0f},
        {22.0f, 16.0f},
        {21.5f, 10.5f},
        {21.0f, 6.5f},
        {17.3f, 5.0f},
        {6.7f, 5.0f},
    };
    polyline(
        dc,
        t,
        outline,
        static_cast<int>(
            std::size(outline)));

    line(dc, t, 6, 11, 10, 11);
    line(dc, t, 8, 9, 8, 13);

    const RECT dot1 =
        r(t, 14.1f, 11.1f, 15.9f, 12.9f);
    const RECT dot2 =
        r(t, 17.1f, 9.1f, 18.9f, 10.9f);
    Ellipse(
        dc,
        dot1.left,
        dot1.top,
        dot1.right,
        dot1.bottom);
    Ellipse(
        dc,
        dot2.left,
        dot2.top,
        dot2.right,
        dot2.bottom);
}

void draw_volume(
    HDC dc,
    const Transform &t)
{
    const POINTF speaker[] = {
        {2, 9},
        {6, 9},
        {11, 4.7f},
        {11, 19.3f},
        {6, 15},
        {2, 15},
        {2, 9},
    };
    polyline(
        dc,
        t,
        speaker,
        static_cast<int>(
            std::size(speaker)));

    RECT arc1 =
        r(t, 12, 7, 20, 17);
    Arc(
        dc,
        arc1.left,
        arc1.top,
        arc1.right,
        arc1.bottom,
        p(t, 16, 9).x,
        p(t, 16, 9).y,
        p(t, 16, 15).x,
        p(t, 16, 15).y);

    RECT arc2 =
        r(t, 13, 3, 24, 21);
    Arc(
        dc,
        arc2.left,
        arc2.top,
        arc2.right,
        arc2.bottom,
        p(t, 19, 5).x,
        p(t, 19, 5).y,
        p(t, 19, 19).x,
        p(t, 19, 19).y);
}

void draw_mic(
    HDC dc,
    const Transform &t)
{
    RECT body =
        r(t, 9, 2, 15, 15);
    RoundRect(
        dc,
        body.left,
        body.top,
        body.right,
        body.bottom,
        std::max(2, static_cast<int>(
            std::lround(6.0f * t.scale))),
        std::max(2, static_cast<int>(
            std::lround(6.0f * t.scale))));

    RECT outer =
        r(t, 5, 8, 19, 19);
    Arc(
        dc,
        outer.left,
        outer.top,
        outer.right,
        outer.bottom,
        p(t, 5, 10).x,
        p(t, 5, 10).y,
        p(t, 19, 10).x,
        p(t, 19, 10).y);

    line(dc, t, 12, 19, 12, 22);
}

void draw_video(
    HDC dc,
    const Transform &t)
{
    RECT body =
        r(t, 2, 6, 16, 18);
    RoundRect(
        dc,
        body.left,
        body.top,
        body.right,
        body.bottom,
        std::max(2, static_cast<int>(
            std::lround(4.0f * t.scale))),
        std::max(2, static_cast<int>(
            std::lround(4.0f * t.scale))));

    const POINTF camera[] = {
        {16, 10.5f},
        {22, 7.5f},
        {22, 16.5f},
        {16, 13},
    };
    polyline(
        dc,
        t,
        camera,
        static_cast<int>(
            std::size(camera)));
}

void draw_sliders(
    HDC dc,
    const Transform &t)
{
    line(dc, t, 4, 6, 20, 6);
    line(dc, t, 4, 12, 20, 12);
    line(dc, t, 4, 18, 20, 18);

    const RECT a =
        r(t, 7, 4, 11, 8);
    const RECT b =
        r(t, 13, 10, 17, 14);
    const RECT c =
        r(t, 9, 16, 13, 20);

    Ellipse(
        dc,
        a.left,
        a.top,
        a.right,
        a.bottom);
    Ellipse(
        dc,
        b.left,
        b.top,
        b.right,
        b.bottom);
    Ellipse(
        dc,
        c.left,
        c.top,
        c.right,
        c.bottom);
}

void draw_refresh(
    HDC dc,
    const Transform &t)
{
    RECT arc =
        r(t, 3, 3, 21, 21);

    Arc(
        dc,
        arc.left,
        arc.top,
        arc.right,
        arc.bottom,
        p(t, 19, 7).x,
        p(t, 19, 7).y,
        p(t, 4, 11).x,
        p(t, 4, 11).y);

    line(dc, t, 21, 3, 21, 8);
    line(dc, t, 21, 8, 16, 8);

    Arc(
        dc,
        arc.left,
        arc.top,
        arc.right,
        arc.bottom,
        p(t, 5, 17).x,
        p(t, 5, 17).y,
        p(t, 20, 13).x,
        p(t, 20, 13).y);

    line(dc, t, 3, 21, 3, 16);
    line(dc, t, 3, 16, 8, 16);
}

void draw_pause(
    HDC dc,
    const Transform &t)
{
    RECT left =
        r(t, 5, 3, 10, 21);
    RECT right =
        r(t, 14, 3, 19, 21);

    RoundRect(
        dc,
        left.left,
        left.top,
        left.right,
        left.bottom,
        std::max(2, static_cast<int>(
            std::lround(2.0f * t.scale))),
        std::max(2, static_cast<int>(
            std::lround(2.0f * t.scale))));
    RoundRect(
        dc,
        right.left,
        right.top,
        right.right,
        right.bottom,
        std::max(2, static_cast<int>(
            std::lround(2.0f * t.scale))),
        std::max(2, static_cast<int>(
            std::lround(2.0f * t.scale))));
}

void draw_record(
    HDC dc,
    const Transform &t,
    bool filled,
    COLORREF color)
{
    RECT circle =
        r(t, 4, 4, 20, 20);

    if (filled) {
        HBRUSH brush =
            CreateSolidBrush(color);
        HGDIOBJ old =
            SelectObject(dc, brush);
        Ellipse(
            dc,
            circle.left,
            circle.top,
            circle.right,
            circle.bottom);
        SelectObject(dc, old);
        DeleteObject(brush);
        return;
    }

    Ellipse(
        dc,
        circle.left,
        circle.top,
        circle.right,
        circle.bottom);
}

void draw_stop(
    HDC dc,
    const Transform &t,
    bool filled,
    COLORREF color)
{
    RECT box =
        r(t, 5, 5, 19, 19);

    if (filled) {
        HBRUSH brush =
            CreateSolidBrush(color);
        HGDIOBJ old =
            SelectObject(dc, brush);
        RoundRect(
            dc,
            box.left,
            box.top,
            box.right,
            box.bottom,
            std::max(2, static_cast<int>(
                std::lround(3.0f * t.scale))),
            std::max(2, static_cast<int>(
                std::lround(3.0f * t.scale))));
        SelectObject(dc, old);
        DeleteObject(brush);
        return;
    }

    RoundRect(
        dc,
        box.left,
        box.top,
        box.right,
        box.bottom,
        std::max(2, static_cast<int>(
            std::lround(3.0f * t.scale))),
        std::max(2, static_cast<int>(
            std::lround(3.0f * t.scale))));
}

void draw_folder(
    HDC dc,
    const Transform &t)
{
    const POINTF folder[] = {
        {3, 5},
        {9, 5},
        {11, 8},
        {21, 8},
        {21, 19},
        {3, 19},
        {3, 5},
    };
    polyline(
        dc,
        t,
        folder,
        static_cast<int>(
            std::size(folder)));
}

} // namespace

void draw_lucide_icon(
    HDC dc,
    LucideIcon icon,
    RECT bounds,
    COLORREF color,
    int stroke_width,
    bool filled)
{
    if (!dc)
        return;

    const Transform t =
        transform_for(bounds);

    LOGBRUSH brush{};
    brush.lbStyle = BS_SOLID;
    brush.lbColor = color;

    HPEN pen =
        ExtCreatePen(
            PS_GEOMETRIC |
                PS_SOLID |
                PS_ENDCAP_ROUND |
                PS_JOIN_ROUND,
            std::max(
                1,
                static_cast<int>(
                    std::lround(
                        static_cast<float>(
                            stroke_width) *
                        std::max(
                            0.75f,
                            t.scale)))),
            &brush,
            0,
            nullptr);

    HGDIOBJ old_pen =
        SelectObject(dc, pen);
    HGDIOBJ old_brush =
        SelectObject(
            dc,
            GetStockObject(HOLLOW_BRUSH));

    switch (icon) {
    case LucideIcon::Monitor:
        draw_monitor(dc, t);
        break;
    case LucideIcon::AppWindow:
        draw_app_window(dc, t);
        break;
    case LucideIcon::Region:
        draw_region(dc, t);
        break;
    case LucideIcon::Gamepad:
        draw_gamepad(dc, t);
        break;
    case LucideIcon::Volume:
        draw_volume(dc, t);
        break;
    case LucideIcon::Mic:
        draw_mic(dc, t);
        break;
    case LucideIcon::Video:
        draw_video(dc, t);
        break;
    case LucideIcon::Sliders:
        draw_sliders(dc, t);
        break;
    case LucideIcon::Refresh:
        draw_refresh(dc, t);
        break;
    case LucideIcon::Pause:
        draw_pause(dc, t);
        break;
    case LucideIcon::Record:
        draw_record(
            dc,
            t,
            filled,
            color);
        break;
    case LucideIcon::Stop:
        draw_stop(
            dc,
            t,
            filled,
            color);
        break;
    case LucideIcon::Folder:
        draw_folder(dc, t);
        break;
    }

    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(pen);
}

} // namespace arssyut::app

#endif
