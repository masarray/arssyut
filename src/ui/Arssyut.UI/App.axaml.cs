using System;
using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Markup.Xaml;
using Arssyut.UI.Preview;

namespace Arssyut.UI;

public sealed partial class App : Application
{
    public override void Initialize() =>
        AvaloniaXamlLoader.Load(this);

    public override void OnFrameworkInitializationCompleted()
    {
        if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
        {
            var args =
                desktop.Args ?? [];

            var stressLongNames =
                Array.Exists(
                    args,
                    arg => string.Equals(
                        arg,
                        "--stress-long-names",
                        StringComparison.OrdinalIgnoreCase));

            var controllerPreview =
                Array.Exists(
                    args,
                    arg => string.Equals(
                        arg,
                        "--controller-preview",
                        StringComparison.OrdinalIgnoreCase));

            var settingsPreview =
                Array.Exists(
                    args,
                    arg => string.Equals(
                        arg,
                        "--settings-preview",
                        StringComparison.OrdinalIgnoreCase));

            var settingsStress =
                Array.Exists(
                    args,
                    arg => string.Equals(
                        arg,
                        "--settings-stress",
                        StringComparison.OrdinalIgnoreCase));

            var previewSettings =
                new SettingsPreviewState();

            desktop.MainWindow =
                settingsPreview
                    ? new SettingsWindow(
                        previewSettings,
                        settingsStress)
                    : new MainWindow(
                        previewSettings,
                        stressLongNames,
                        controllerPreview);
        }

        base.OnFrameworkInitializationCompleted();
    }
}
