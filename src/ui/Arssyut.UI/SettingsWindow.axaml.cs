using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;

namespace Arssyut.UI;

public sealed partial class SettingsWindow : Window
{
    private readonly Button[] _navButtons;
    private readonly Control[] _pages;

    public SettingsWindow()
    {
        InitializeComponent();

        TransparencyLevelHint =
        [
            WindowTransparencyLevel.Mica,
            WindowTransparencyLevel.AcrylicBlur,
            WindowTransparencyLevel.None
        ];

        _navButtons =
        [
            NavGeneral,
            NavRecording,
            NavOutput,
            NavAudio,
            NavCamera,
            NavMouse,
            NavHotkeys,
            NavAdvanced
        ];

        _pages =
        [
            PageGeneral,
            PageRecording,
            PageOutput,
            PageAudio,
            PageCamera,
            PageMouse,
            PageHotkeys,
            PageAdvanced
        ];
    }

    private void TitleBar_OnPointerPressed(
        object? sender,
        PointerPressedEventArgs e)
    {
        if (e.GetCurrentPoint(this).Properties.IsLeftButtonPressed)
            BeginMoveDrag(e);
    }

    private void Close_OnClick(
        object? sender,
        RoutedEventArgs e) =>
        Close();

    private void Nav_OnClick(
        object? sender,
        RoutedEventArgs e)
    {
        if (sender is not Button selected)
            return;

        var tag = selected.Tag?.ToString() ?? "Recording";

        for (var i = 0; i < _navButtons.Length; ++i)
        {
            var active = ReferenceEquals(_navButtons[i], selected);

            if (active)
                _navButtons[i].Classes.Add("selected");
            else
                _navButtons[i].Classes.Remove("selected");

            _pages[i].IsVisible = active;
        }

        PageTitle.Text =
            tag == "Mouse"
                ? "Mouse & Keystroke"
                : tag;

        PageSubtitle.Text = tag switch
        {
            "General" => "Application behavior and recorder workspace preferences.",
            "Recording" => "Quality, visual presentation, and recording behavior.",
            "Output" => "File destination and naming policy.",
            "Audio" => "System audio and narration devices.",
            "Camera" => "Webcam source and picture-in-picture defaults.",
            "Mouse" => "Pointer, click, and keyboard visualization.",
            "Hotkeys" => "Recorder transport shortcuts.",
            "Advanced" => "Diagnostics and low-level product behavior.",
            _ => string.Empty
        };
    }
}
