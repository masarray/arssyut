using System;
using System.Diagnostics;
using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Markup.Xaml;
using Arssyut.UI.Interop;
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

            var explicitUiPreview =
                Array.Exists(
                    args,
                    arg => string.Equals(
                        arg,
                        "--ui-preview",
                        StringComparison.OrdinalIgnoreCase));

            var allowInteractionPreview =
                explicitUiPreview ||
                controllerPreview ||
                stressLongNames ||
                settingsPreview ||
                settingsStress;

            var previewSettings =
                new SettingsPreviewState();

            if (!allowInteractionPreview)
            {
                var hotkeyStore =
                    ProductSettingsStore.CreateDefault();

                if (hotkeyStore.TryLoad(
                        out var persistedSettings,
                        out var loadError))
                {
                    if (!previewSettings.TryRestorePersistentSettings(
                            persistedSettings,
                            out var restoreError))
                    {
                        Debug.WriteLine(
                            "Arssyut product settings restore rejected: " +
                            restoreError);
                    }
                }
                else if (!string.IsNullOrWhiteSpace(
                             loadError))
                {
                    Debug.WriteLine(
                        loadError);
                }

                previewSettings.PersistentSettingsChanged +=
                    (_, _) =>
                    {
                        if (!hotkeyStore.TrySave(
                                previewSettings,
                                out var saveError))
                        {
                            Debug.WriteLine(
                                saveError);
                        }
                    };
            }

            var bridgeRequired =
                Array.Exists(
                    args,
                    arg => string.Equals(
                        arg,
                        "--bridge-required",
                        StringComparison.OrdinalIgnoreCase));

            var bridgeAvailability =
                NativeBridgeClient.TryCreate(
                    out var nativeBridge);

            if (bridgeRequired &&
                bridgeAvailability !=
                    NativeBridgeAvailability.Available)
            {
                throw new InvalidOperationException(
                    $"Native bridge is required but reported {bridgeAvailability}.");
            }

            desktop.Exit +=
                (_, _) =>
                    nativeBridge?.Dispose();

            desktop.MainWindow =
                settingsPreview
                    ? new SettingsWindow(
                        previewSettings,
                        nativeBridge,
                        settingsStress)
                    : new MainWindow(
                        previewSettings,
                        nativeBridge,
                        bridgeAvailability,
                        stressLongNames,
                        controllerPreview,
                        allowInteractionPreview);
        }

        base.OnFrameworkInitializationCompleted();
    }
}
