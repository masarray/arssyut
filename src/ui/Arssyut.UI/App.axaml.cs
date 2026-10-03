using System;
using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Markup.Xaml;

namespace Arssyut.UI;

public sealed partial class App : Application
{
    public override void Initialize() =>
        AvaloniaXamlLoader.Load(this);

    public override void OnFrameworkInitializationCompleted()
    {
        if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
        {
            var stressLongNames =
                Array.Exists(
                    desktop.Args ?? [],
                    arg => string.Equals(
                        arg,
                        "--stress-long-names",
                        StringComparison.OrdinalIgnoreCase));

            desktop.MainWindow =
                new MainWindow(
                    stressLongNames);
        }

        base.OnFrameworkInitializationCompleted();
    }
}
