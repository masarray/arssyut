using System;
using Avalonia;

namespace Arssyut.UI;

internal static class Program
{
    [STAThread]
    public static void Main(string[] args)
    {
#if ARSSYUT_INTERNAL_AUDIO_PREVIEW
        // Product-ON acceptance binary is specifically compiled for
        // native hardware A/V testing. No CMD launcher or global machine
        // environment setting is required. Public builds omit this symbol
        // and their native bridges still reject audio flags.
        Environment.SetEnvironmentVariable(
            "ARSSYUT_AUDIO_PREVIEW", "1",
            EnvironmentVariableTarget.Process);
#endif
        BuildAvaloniaApp()
            .StartWithClassicDesktopLifetime(args);
    }

    public static AppBuilder BuildAvaloniaApp() =>
        AppBuilder
            .Configure<App>()
            .UsePlatformDetect()
            .WithInterFont()
            .LogToTrace();
}
