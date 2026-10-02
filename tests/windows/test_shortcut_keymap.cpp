#include "platform/windows/input/shortcut_keymap.hpp"

#include <Windows.h>

#include <iostream>

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

} // namespace

int main()
{
    using arssyut::presentation::ShortcutKey;
    using namespace arssyut::windows;

    TestContext test;

    test.expect(
        shortcut_key_from_virtual_key('C') ==
            ShortcutKey::C,
        "Windows letter maps to semantic action key");

    test.expect(
        shortcut_key_from_virtual_key(VK_F24) ==
            ShortcutKey::F24,
        "Windows F24 maps to semantic function key");

    test.expect(
        shortcut_key_from_virtual_key(VK_OEM_PLUS) ==
            ShortcutKey::Plus,
        "Windows OEM plus maps without reconstructing typed text");

    test.expect(
        shortcut_key_from_virtual_key(VK_VOLUME_UP) ==
            ShortcutKey::VolumeUp,
        "Windows media key maps to semantic action");

    test.expect(
        shortcut_key_from_virtual_key(VK_CONTROL) ==
            ShortcutKey::Unknown,
        "Modifier alone never becomes primary action key");

    test.expect(
        is_modifier_virtual_key(VK_LCONTROL) &&
        is_modifier_virtual_key(VK_RSHIFT) &&
        is_modifier_virtual_key(VK_LWIN),
        "Left/right Windows modifiers are recognized");

    test.expect(
        !is_modifier_virtual_key('A'),
        "Ordinary key is not classified as modifier");

    if (test.failures != 0) {
        std::cerr
            << test.failures
            << " of "
            << test.checks
            << " Windows keymap checks failed\n";
        return 1;
    }

    std::cout
        << "PASS: "
        << test.checks
        << " Windows keymap checks\n";
    return 0;
}
