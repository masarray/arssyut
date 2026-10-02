#include "presentation/shortcut_visualizer.hpp"

#include <iostream>
#include <string>

namespace {

struct TestContext {
    int checks = 0;
    int failures = 0;

    void expect(bool condition, const char *message)
    {
        ++checks;
        if (!condition) {
            ++failures;
            std::cerr << "FAIL: " << message << '\n';
        }
    }
};

std::wstring label(
    const arssyut::presentation::KeycapFrame &keycap)
{
    return std::wstring(keycap.label.data());
}

void test_privacy_policy(TestContext &test)
{
    using namespace arssyut::presentation;

    test.expect(
        !should_visualize_shortcut(
            {ShortcutKey::A, 0}),
        "Unmodified letter is hidden");

    test.expect(
        !should_visualize_shortcut(
            {ShortcutKey::Digit1, 0}),
        "Unmodified digit is hidden");

    test.expect(
        !should_visualize_shortcut(
            {ShortcutKey::Space, 0}),
        "Unmodified space is hidden");

    test.expect(
        should_visualize_shortcut(
            {ShortcutKey::C, ShortcutCtrl}),
        "Modified letter action is visible");

    test.expect(
        should_visualize_shortcut(
            {ShortcutKey::Digit1, ShortcutCtrl}),
        "Modified digit action is visible");

    test.expect(
        should_visualize_shortcut(
            {ShortcutKey::F12, 0}),
        "Function key is visible");

    test.expect(
        should_visualize_shortcut(
            {ShortcutKey::PrintScreen, 0}),
        "System action key is visible");
}

void test_canonical_keycaps(TestContext &test)
{
    using namespace arssyut::presentation;

    const auto frame =
        build_keyboard_overlay(
            {
                ShortcutKey::S,
                static_cast<std::uint8_t>(
                    ShortcutCtrl |
                    ShortcutShift |
                    ShortcutWin)
            },
            7);

    test.expect(
        frame.generation == 7 &&
        frame.keycap_count == 4,
        "Chord becomes bounded structured keycaps");

    test.expect(
        label(frame.keycaps[0]) == L"Ctrl" &&
        label(frame.keycaps[1]) == L"Shift" &&
        frame.keycaps[2].glyph == KeycapGlyph::WindowsLogo &&
        label(frame.keycaps[2]).empty() &&
        label(frame.keycaps[3]) == L"S",
        "Modifier order is canonical and Windows uses a logo glyph");

    bool all_text_or_windows_logo = true;
    for (std::size_t i = 0; i < frame.keycap_count; ++i) {
        all_text_or_windows_logo =
            all_text_or_windows_logo &&
            (frame.keycaps[i].glyph == KeycapGlyph::Text ||
             frame.keycaps[i].glyph == KeycapGlyph::WindowsLogo);
    }
    test.expect(
        all_text_or_windows_logo,
        "Keycaps use one uniform light surface language");

    const auto maximum =
        build_keyboard_overlay(
            {
                ShortcutKey::Delete,
                static_cast<std::uint8_t>(
                    ShortcutCtrl |
                    ShortcutShift |
                    ShortcutAlt |
                    ShortcutWin)
            },
            8);

    test.expect(
        maximum.keycap_count ==
            kMaxShortcutKeycaps,
        "Four modifiers plus action remain fixed-capacity");
}

void test_labels(TestContext &test)
{
    using namespace arssyut::presentation;

    wchar_t buffer[16]{};

    test.expect(
        shortcut_key_label(
            ShortcutKey::F24,
            buffer,
            16) &&
        std::wstring(buffer) == L"F24",
        "F24 label is deterministic");

    test.expect(
        shortcut_key_label(
            ShortcutKey::Backspace,
            buffer,
            16) &&
        std::wstring(buffer) == L"Backspace",
        "Wide key label is deterministic");

    test.expect(
        shortcut_key_label(
            ShortcutKey::MediaPlayPause,
            buffer,
            16) &&
        std::wstring(buffer) == L"Play",
        "Media key label is bounded");

    test.expect(
        !shortcut_key_label(
            ShortcutKey::Unknown,
            buffer,
            16),
        "Unknown key has no display label");
}

void test_identity(TestContext &test)
{
    using namespace arssyut::presentation;

    const ShortcutChord a{
        ShortcutKey::C,
        ShortcutCtrl
    };
    const ShortcutChord b{
        ShortcutKey::C,
        ShortcutCtrl
    };
    const ShortcutChord c{
        ShortcutKey::V,
        ShortcutCtrl
    };

    test.expect(
        same_shortcut(a, b),
        "Equivalent semantic chord compares equal");
    test.expect(
        !same_shortcut(a, c),
        "Different action chord compares unequal");
}

} // namespace

int main()
{
    TestContext test;

    test_privacy_policy(test);
    test_canonical_keycaps(test);
    test_labels(test);
    test_identity(test);

    if (test.failures != 0) {
        std::cerr
            << test.failures
            << " of "
            << test.checks
            << " shortcut checks failed\n";
        return 1;
    }

    std::cout
        << "PASS: "
        << test.checks
        << " shortcut checks\n";
    return 0;
}
