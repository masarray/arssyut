# P6UI.6A.1 — Hotkey Product Hardening

**Baseline:** P6UI.6C `5e9c858e2a51cda2594e823792f83a61c3fe601a`

## Why this milestone exists

Earlier UI work exposed hotkeys as display strings. That was sufficient for
F9-style preview work but not for a product hotkey subsystem: OEM keys such as
grave/backtick, `+`-bearing Numpad labels, duplicate aliases and persistence
all become ambiguous when string parsing is the authority.

P6UI.6A.1 closes that debt without reopening P6UI.6C presenter camera/input
ownership.

## Canonical model

```text
Avalonia KeyEvent
      |
      v
HotkeyChord
  modifiers: Ctrl / Shift / Alt / Win bit mask
  virtualKey: Windows VK
      |
      +--> derived display label
      +--> duplicate identity
      +--> persistent JSON
      +--> RegisterHotKey
      +--> Raw Input recorder-start binding
```

Strings are display/compatibility input only.

## Key vocabulary

The map covers:
- A-Z / 0-9;
- F1-F24;
- Home/End/PageUp/PageDown/arrows/Insert/Delete/PrintScreen/etc.;
- Numpad0-9, Numpad+, Numpad-, Numpad*, Numpad/, decimal/separator;
- Windows OEM punctuation: semicolon, equal, comma, minus, period, slash,
  grave/backtick, open bracket, backslash, close bracket, quote and OEM8.

Avalonia/WPF aliases such as `OemTilde`, `Oem3`, `Add`, `Prior` and
`Snapshot` map to the same Windows VK identity.

## Conflict validation

Bridge ABI v7 adds `arssyut_bridge_hotkey_probe`.

The probe uses the existing hidden bridge HWND to temporarily call
`RegisterHotKey`, then immediately unregisters it. It has no runtime action
id and never owns Hold/Peek semantics.

Settings sequence:
1. capture KeyEvent -> HotkeyChord;
2. probe Windows conflict for native-facing actions;
3. reject Busy/error without changing settings;
4. run canonical duplicate check;
5. commit state;
6. HotkeysChanged persists the complete snapshot.

## Persistence

Normal product mode stores:
`%LOCALAPPDATA%\Arssyut\settings.json`.

The schema stores modifier mask + VK for every known action. Save uses:
1. same-directory unique temp file;
2. write-through file stream;
3. flush-to-disk;
4. replace/move over the canonical path.

Load is all-or-nothing. Invalid schema, missing actions, unsupported keys,
invalid modifiers, duplicate chords or malformed JSON reject the whole
snapshot. Defaults remain active and the invalid file is best-effort renamed
to `settings.json.corrupt`.

Preview/stress CLI modes intentionally skip persistent load/save.

## Layout

Settings reserves an 18px right content gutter inside the ScrollViewer. The
scrollbar therefore owns its own visual lane and cannot overlap right-column
keycap CTAs. No per-button collision margins are used.

## Acceptance matrix

Automated:
- Ctrl+`
- Ctrl+Shift+`
- Ctrl+=
- Ctrl+-
- Alt+[
- Ctrl+Shift+F9
- Win+Alt+F12
- Numpad+
- Ctrl+]
- Ctrl+\
- Alt+;
- Shift+F12
- Ctrl+Numpad+
- Ctrl+Numpad-
- Home
- PageDown
- Insert
- Avalonia OemTilde -> VK_OEM_3
- Avalonia Add -> VK_ADD
- canonical alias duplicate detection
- persistence save/load restore
- corrupt JSON quarantine/fallback
- native conflict probe Busy -> OK after release

Real Windows acceptance must verify capture/display labels, restart restore,
Windows-reserved conflict feedback and scrollbar/keycap separation.
