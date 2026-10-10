using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using Avalonia;
using Arssyut.UI.Preview;

namespace Arssyut.UI.Interop;

public enum NativeBridgeAvailability
{
    Available,
    MissingLibrary,
    LibraryLoadFailed,
    IncompatibleAbi,
    InitializationFailed
}

public enum NativeDeviceKind : uint
{
    Microphone = 0,
    Camera = 1
}

public enum NativeBridgeStatus
{
    Ok = 0,
    InvalidArgument = 1,
    OutOfRange = 2,
    InternalError = 3,
    Busy = 4,
    Unsupported = 5,
    StaleToken = 6,
    StartFailed = 7,
    InvalidState = 8
}

public enum NativeRecorderState : uint
{
    Idle = 0,
    Preparing = 1,
    Armed = 2,
    Recording = 3,
    Stopping = 4,
    Finalizing = 5,
    Ready = 6,
    Failed = 7
}

public readonly record struct NativeHotkeyChord(
    NativeHotkeyModifiers Modifiers,
    uint VirtualKey)
{
    public static NativeHotkeyChord None =>
        new(
            NativeHotkeyModifiers.None,
            0);
}

public enum NativeHotkeyAction : uint
{
    ToggleRecord = 0,
    TogglePause = 1,
    ToggleMicrophone = 2,
    ToggleCamera = 3,
    ToggleZoom = 4,
    ZoomIn = 5,
    ZoomOut = 6,
    ResetFullFrame = 7,
    FreezeCamera = 8
}

[Flags]
public enum NativeHotkeyModifiers : uint
{
    None = 0,
    Control = 1U << 0,
    Shift = 1U << 1,
    Alt = 1U << 2,
    Win = 1U << 3
}

[Flags]
public enum NativeHotkeyEvents : uint
{
    None = 0,
    ToggleRecord = 1U << 0,
    TogglePause = 1U << 1,
    ToggleMicrophone = 1U << 2,
    ToggleCamera = 1U << 3
}

public enum NativePresenterCommand : uint
{
    ToggleZoom = 0,
    ZoomIn = 1,
    ZoomOut = 2,
    ResetFullFrame = 3,
    ToggleFreezeCamera = 4
}

public enum NativeVisualMode : uint
{
    PixelAccurate = 0,
    CleanScreen = 1,
    VividPresentation = 2
}

[Flags]
public enum NativeStartFlags : uint
{
    None = 0,
    SystemAudio = 1U << 0,
    Microphone = 1U << 1,
    Camera = 1U << 2,
    SmartZoom = 1U << 3,
    ClickVisual = 1U << 4,
    ShortcutKeys = 1U << 5,
    PresenterControls = 1U << 6,
    Spotlight = 1U << 7,
    SpotlightLinkToZoom = 1U << 8,
    ArmedStart = 1U << 9
}

public enum NativeSpotlightSize : uint
{
    Compact = 0,
    Balanced = 1,
    Wide = 2
}

public enum NativeSpotlightMotion : uint
{
    Smooth = 0,
    Balanced = 1,
    Snappy = 2
}

public sealed record NativeDeviceItem(
    ulong Token,
    NativeDeviceKind Kind,
    string Name);

public sealed record NativeDeviceSnapshot(
    IReadOnlyList<NativeDeviceItem> Microphones,
    IReadOnlyList<NativeDeviceItem> Cameras);

public sealed record NativeRecorderSnapshot(
    NativeRecorderState State,
    long ElapsedTicks,
    bool WorkerFinished,
    float CameraCenterX,
    float CameraCenterY,
    float CameraZoom,
    ulong CaptureReceived,
    ulong VideoRendered,
    ulong EncoderSubmitted,
    ulong EncoderBackpressure,
    uint ErrorCode,
    uint ErrorDetail);

// Endpoint preview telemetry, independent of the recorded stereo PCM.
public sealed record NativeAudioMeter(
    uint SystemChannels,
    uint MicrophoneChannels,
    float SystemLeft,
    float SystemRight,
    float MicrophoneLeft,
    float MicrophoneRight);

public sealed record NativeRecorderResult(
    string OutputPath,
    string DiagnosticsPath);

public sealed record NativeStartRequest(
    PreviewCaptureMode CaptureMode,
    ulong SourceToken,
    uint FrameRate,
    NativeVisualMode VisualMode,
    NativeStartFlags Flags,
    ulong MicrophoneDeviceToken,
    ulong CameraDeviceToken,
    string OutputFolder,
    NativeHotkeyChord HoldZoomHotkey,
    NativeHotkeyChord OverviewPeekHotkey,
    float PresenterZoom,
    NativeSpotlightSize SpotlightSize,
    NativeSpotlightMotion SpotlightMotion,
    float SpotlightDimStrength,
    float SystemGain = 1.0f,
    float MicrophoneGain = 1.0f,
    bool SystemMuted = false,
    bool MicrophoneMuted = false);

public sealed class NativeBridgeClient : IDisposable
{
    private const string LibraryName =
        "arssyut_native_bridge";
    private const string EmbeddedBridgeResource =
        "Arssyut.Native.arssyut_native_bridge.dll";
    private const uint ExpectedAbi = 11;

    private static readonly object NativeLoadGate =
        new();
    private static IntPtr _bundledLibraryHandle;
    private static string? _bundledLoadError;

    private IntPtr _handle;

    static NativeBridgeClient()
    {
        NativeLibrary.SetDllImportResolver(
            typeof(NativeBridgeClient).Assembly,
            ResolveNativeLibrary);
    }

    private NativeBridgeClient(
        IntPtr handle)
    {
        _handle = handle;
    }


    private static IntPtr ResolveNativeLibrary(
        string libraryName,
        Assembly assembly,
        DllImportSearchPath? searchPath)
    {
        if (!OperatingSystem.IsWindows() ||
            !string.Equals(
                libraryName,
                LibraryName,
                StringComparison.OrdinalIgnoreCase))
        {
            return IntPtr.Zero;
        }

        lock (NativeLoadGate)
        {
            if (_bundledLibraryHandle !=
                IntPtr.Zero)
            {
                return _bundledLibraryHandle;
            }

            try
            {
                using var resource =
                    assembly.GetManifestResourceStream(
                        EmbeddedBridgeResource);

                if (resource is null)
                {
                    // Developer builds may still carry the bridge beside the
                    // executable. Returning zero preserves normal P/Invoke
                    // resolution for that case.
                    return IntPtr.Zero;
                }

                using var buffer =
                    new MemoryStream();

                resource.CopyTo(
                    buffer);

                var bytes =
                    buffer.ToArray();

                var digest =
                    SHA256.HashData(
                        bytes);
                var digestText =
                    Convert.ToHexString(
                        digest);

                var root =
                    Path.Combine(
                        Environment.GetFolderPath(
                            Environment.SpecialFolder.
                                LocalApplicationData),
                        "Arssyut",
                        "Native",
                        digestText[..16]);

                Directory.CreateDirectory(
                    root);

                var path =
                    Path.Combine(
                        root,
                        "arssyut_native_bridge.dll");

                if (!File.Exists(path) ||
                    !FileMatchesHash(
                        path,
                        digest))
                {
                    var temporary =
                        path +
                        "." +
                        Environment.ProcessId +
                        ".tmp";

                    File.WriteAllBytes(
                        temporary,
                        bytes);

                    try
                    {
                        File.Move(
                            temporary,
                            path,
                            overwrite: true);
                    }
                    finally
                    {
                        if (File.Exists(
                                temporary))
                        {
                            File.Delete(
                                temporary);
                        }
                    }
                }

                _bundledLibraryHandle =
                    NativeLibrary.Load(
                        path);

                _bundledLoadError =
                    null;

                return
                    _bundledLibraryHandle;
            }
            catch (Exception error)
            {
                _bundledLoadError =
                    error.GetType().Name +
                    ": " +
                    error.Message;

                return IntPtr.Zero;
            }
        }
    }

    private static bool FileMatchesHash(
        string path,
        byte[] expected)
    {
        try
        {
            using var file =
                File.OpenRead(
                    path);

            var actual =
                SHA256.HashData(
                    file);

            return CryptographicOperations.
                FixedTimeEquals(
                    actual,
                    expected);
        }
        catch
        {
            return false;
        }
    }

    private static bool HasEmbeddedBridge()
    {
        try
        {
            return typeof(NativeBridgeClient).
                Assembly.
                GetManifestResourceInfo(
                    EmbeddedBridgeResource) is
                    not null;
        }
        catch
        {
            return false;
        }
    }

    public static NativeBridgeAvailability TryCreate(
        out NativeBridgeClient? bridge)
    {
        bridge = null;

        if (!OperatingSystem.IsWindows())
            return NativeBridgeAvailability.MissingLibrary;

        try
        {
            if (NativeMethods.AbiVersion() !=
                ExpectedAbi)
            {
                return
                    NativeBridgeAvailability.
                        IncompatibleAbi;
            }

            var handle =
                NativeMethods.Create();

            if (handle == IntPtr.Zero)
            {
                return
                    NativeBridgeAvailability.
                        InitializationFailed;
            }

            bridge =
                new NativeBridgeClient(
                    handle);

            return NativeBridgeAvailability.Available;
        }
        catch (DllNotFoundException)
        {
            var deployedBridge =
                Path.Combine(
                    AppContext.BaseDirectory,
                    "arssyut_native_bridge.dll");

            return File.Exists(
                       deployedBridge) ||
                   HasEmbeddedBridge() ||
                   !string.IsNullOrWhiteSpace(
                       _bundledLoadError)
                ? NativeBridgeAvailability.
                    LibraryLoadFailed
                : NativeBridgeAvailability.
                    MissingLibrary;
        }
        catch (EntryPointNotFoundException)
        {
            return NativeBridgeAvailability.IncompatibleAbi;
        }
        catch (BadImageFormatException)
        {
            return NativeBridgeAvailability.IncompatibleAbi;
        }
    }

    public IReadOnlyList<PreviewSourceItem> RefreshSources(
        PreviewCaptureMode mode,
        IntPtr ownWindow)
    {
        ThrowIfDisposed();

        EnsureOk(
            NativeMethods.RefreshSources(
                _handle,
                unchecked(
                    (nuint)ownWindow.ToInt64())));

        EnsureOk(
            NativeMethods.SourceCount(
                _handle,
                out var count));

        var result =
            new List<PreviewSourceItem>(
                checked((int)count));

        for (uint index = 0;
             index < count;
             ++index)
        {
            var source =
                new NativeSourceV1
                {
                    StructSize =
                        checked((uint)
                            Marshal.SizeOf<
                                NativeSourceV1>())
                };

            EnsureOk(
                NativeMethods.SourceAt(
                    _handle,
                    index,
                    ref source));

            var isMonitor =
                source.Kind == 0;

            var include =
                mode switch
                {
                    PreviewCaptureMode.Display =>
                        isMonitor,
                    PreviewCaptureMode.Region =>
                        isMonitor,
                    PreviewCaptureMode.Window =>
                        !isMonitor,
                    PreviewCaptureMode.Game =>
                        !isMonitor,
                    _ =>
                        false
                };

            if (!include)
                continue;

            var width =
                Math.Max(
                    0,
                    source.ScreenRect.Right -
                    source.ScreenRect.Left);
            var height =
                Math.Max(
                    0,
                    source.ScreenRect.Bottom -
                    source.ScreenRect.Top);

            var label =
                string.IsNullOrWhiteSpace(
                    source.Label)
                    ? isMonitor
                        ? "Display"
                        : "Window"
                    : source.Label.Trim();

            var title =
                mode switch
                {
                    PreviewCaptureMode.Region =>
                        $"Region on {label}",
                    PreviewCaptureMode.Game =>
                        label.Replace(
                            "Window  —  ",
                            string.Empty,
                            StringComparison.Ordinal),
                    _ =>
                        label
                };

            var subtitle =
                mode switch
                {
                    PreviewCaptureMode.Display =>
                        $"{width} × {height} · native monitor source",
                    PreviewCaptureMode.Region =>
                        $"{width} × {height} · native Region base display",
                    PreviewCaptureMode.Window =>
                        $"{width} × {height} · native window source",
                    PreviewCaptureMode.Game =>
                        $"{width} × {height} · application candidate; game backend pending",
                    _ =>
                        $"{width} × {height}"
                };

            result.Add(
                new PreviewSourceItem(
                    $"native:{source.Token:X16}",
                    mode,
                    title,
                    subtitle,
                    new PixelRect(
                        source.ScreenRect.Left,
                        source.ScreenRect.Top,
                        width,
                        height),
                    label.Contains(
                        "Primary",
                        StringComparison.OrdinalIgnoreCase),
                    source.Token));
        }

        return result;
    }

    public NativeBridgeStatus SetOverlayTarget(
        PreviewCaptureMode mode,
        ulong sourceToken)
    {
        ThrowIfDisposed();

        var captureMode =
            mode switch
            {
                PreviewCaptureMode.Window => 1U,
                PreviewCaptureMode.Region => 2U,
                PreviewCaptureMode.Game => 3U,
                _ => 0U
            };

        return (NativeBridgeStatus)
            NativeMethods.OverlaySetTarget(
                _handle,
                captureMode,
                sourceToken);
    }

    public void HideOverlay()
    {
        ThrowIfDisposed();

        EnsureOk(
            NativeMethods.OverlayHide(
                _handle));
    }

    public PixelRect CountdownBounds(
        PreviewCaptureMode mode,
        PixelRect fallback)
    {
        ThrowIfDisposed();

        var snapshot =
            new NativeOverlaySnapshotV1
            {
                StructSize =
                    checked((uint)
                        Marshal.SizeOf<
                            NativeOverlaySnapshotV1>())
            };

        if (NativeMethods.OverlaySnapshot(
                _handle,
                ref snapshot) != 0)
            return fallback;

        var rect =
            mode == PreviewCaptureMode.Region &&
            snapshot.RegionValid != 0
                ? snapshot.RegionRect
                : snapshot.BoundaryRect;

        var width =
            Math.Max(
                0,
                rect.Right - rect.Left);
        var height =
            Math.Max(
                0,
                rect.Bottom - rect.Top);

        return width > 0 && height > 0
            ? new PixelRect(
                rect.Left,
                rect.Top,
                width,
                height)
            : fallback;
    }

    public NativeBridgeStatus RegisterHotkey(
        NativeHotkeyAction action,
        NativeHotkeyModifiers modifiers,
        uint virtualKey)
    {
        ThrowIfDisposed();

        return (NativeBridgeStatus)
            NativeMethods.HotkeyRegister(
                _handle,
                (uint)action,
                (uint)modifiers,
                virtualKey);
    }

    public NativeBridgeStatus ProbeHotkey(
        NativeHotkeyModifiers modifiers,
        uint virtualKey)
    {
        ThrowIfDisposed();

        return (NativeBridgeStatus)
            NativeMethods.HotkeyProbe(
                _handle,
                (uint)modifiers,
                virtualKey);
    }

    public NativeBridgeStatus UnregisterHotkey(
        NativeHotkeyAction action)
    {
        ThrowIfDisposed();

        return (NativeBridgeStatus)
            NativeMethods.HotkeyUnregister(
                _handle,
                (uint)action);
    }

    public NativeHotkeyEvents TakeHotkeyEvents()
    {
        ThrowIfDisposed();

        EnsureOk(
            NativeMethods.HotkeyTakeEvents(
                _handle,
                out var events));

        return (NativeHotkeyEvents)events;
    }

    // Does not invalidate tokens already owned by the main recorder.
    public NativeDeviceSnapshot SnapshotDevices()
    {
        ThrowIfDisposed();
        return new NativeDeviceSnapshot(
            ReadDevices(NativeDeviceKind.Microphone),
            ReadDevices(NativeDeviceKind.Camera));
    }

    public NativeDeviceSnapshot RefreshDevices()
    {
        ThrowIfDisposed();

        EnsureOk(
            NativeMethods.RefreshDevices(
                _handle));

        return new NativeDeviceSnapshot(
            ReadDevices(
                NativeDeviceKind.Microphone),
            ReadDevices(
                NativeDeviceKind.Camera));
    }

    private IReadOnlyList<NativeDeviceItem> ReadDevices(
        NativeDeviceKind kind)
    {
        EnsureOk(
            NativeMethods.DeviceCount(
                _handle,
                (uint)kind,
                out var count));

        var result =
            new List<NativeDeviceItem>(
                checked((int)count));

        for (uint index = 0;
             index < count;
             ++index)
        {
            var device =
                new NativeDeviceV1
                {
                    StructSize =
                        checked((uint)
                            Marshal.SizeOf<
                                NativeDeviceV1>())
                };

            EnsureOk(
                NativeMethods.DeviceAt(
                    _handle,
                    (uint)kind,
                    index,
                    ref device));

            result.Add(
                new NativeDeviceItem(
                    device.Token,
                    kind,
                    device.Name?.Trim() ??
                        string.Empty));
        }

        return result;
    }

    public NativeRecorderSnapshot Snapshot()
    {
        ThrowIfDisposed();

        var snapshot =
            new NativeRecorderSnapshotV1
            {
                StructSize =
                    checked((uint)
                        Marshal.SizeOf<
                            NativeRecorderSnapshotV1>())
            };

        EnsureOk(
            NativeMethods.RecorderSnapshot(
                _handle,
                ref snapshot));

        return new NativeRecorderSnapshot(
            (NativeRecorderState)snapshot.State,
            snapshot.ElapsedTicks,
            snapshot.WorkerFinished != 0,
            snapshot.CameraCenterX,
            snapshot.CameraCenterY,
            snapshot.CameraZoom,
            snapshot.CaptureReceived,
            snapshot.VideoRendered,
            snapshot.EncoderSubmitted,
            snapshot.EncoderBackpressure,
            snapshot.ErrorCode,
            snapshot.ErrorDetail);
    }

    public NativeAudioMeter AudioMeter(
        ulong microphoneDeviceToken,
        bool systemAudio,
        bool microphone)
    {
        ThrowIfDisposed();

        var meter = new NativeAudioMeterV1
        {
            StructSize = checked((uint)Marshal.SizeOf<NativeAudioMeterV1>())
        };
        var flags = (systemAudio ? 1U : 0U) |
                    (microphone ? 2U : 0U);
        EnsureOk(NativeMethods.AudioMeter(
            _handle, microphoneDeviceToken, flags, ref meter));
        return new NativeAudioMeter(
            meter.SystemChannels, meter.MicrophoneChannels,
            meter.SystemLeft, meter.SystemRight,
            meter.MicrophoneLeft, meter.MicrophoneRight);
    }

    public NativeBridgeStatus StartRecording(
        NativeStartRequest request)
    {
        ThrowIfDisposed();

        var native =
            new NativeStartRequestV1
            {
                StructSize =
                    checked((uint)
                        Marshal.SizeOf<
                            NativeStartRequestV1>()),
                CaptureMode =
                    request.CaptureMode switch
                    {
                        PreviewCaptureMode.Window => 1,
                        PreviewCaptureMode.Region => 2,
                        PreviewCaptureMode.Game => 3,
                        _ => 0
                    },
                SourceToken =
                    request.SourceToken,
                FrameRate =
                    request.FrameRate,
                VisualMode =
                    (uint)request.VisualMode,
                Flags =
                    (uint)request.Flags,
                Reserved0 = 0,
                MicrophoneDeviceToken =
                    request.MicrophoneDeviceToken,
                CameraDeviceToken =
                    request.CameraDeviceToken,
                OutputFolder =
                    request.OutputFolder ?? string.Empty,
                HoldZoomModifiers =
                    (uint)request.HoldZoomHotkey.Modifiers,
                HoldZoomVirtualKey =
                    request.HoldZoomHotkey.VirtualKey,
                OverviewPeekModifiers =
                    (uint)request.OverviewPeekHotkey.Modifiers,
                OverviewPeekVirtualKey =
                    request.OverviewPeekHotkey.VirtualKey,
                PresenterZoom =
                    request.PresenterZoom,
                Reserved1 = 0,
                SpotlightSize =
                    (uint)request.SpotlightSize,
                SpotlightMotion =
                    (uint)request.SpotlightMotion,
                SpotlightDimStrength =
                    request.SpotlightDimStrength,
                Reserved2 = 0,
                SystemGain = request.SystemGain,
                MicrophoneGain = request.MicrophoneGain,
                SystemMuted = request.SystemMuted ? 1U : 0U,
                MicrophoneMuted = request.MicrophoneMuted ? 1U : 0U
            };

        return (NativeBridgeStatus)
            NativeMethods.RecorderStart(
                _handle,
                ref native);
    }

    public NativeBridgeStatus PresenterCommand(
        NativePresenterCommand command)
    {
        ThrowIfDisposed();

        return (NativeBridgeStatus)
            NativeMethods.RecorderPresenterCommand(
                _handle,
                (uint)command);
    }

    public NativeBridgeStatus CommitStart()
    {
        ThrowIfDisposed();

        return (NativeBridgeStatus)
            NativeMethods.RecorderCommitStart(
                _handle);
    }

    public NativeBridgeStatus StopRecording()
    {
        ThrowIfDisposed();

        return (NativeBridgeStatus)
            NativeMethods.RecorderStop(
                _handle);
    }

    public NativeRecorderResult Result()
    {
        ThrowIfDisposed();

        var result =
            new NativeRecorderResultV1
            {
                StructSize =
                    checked((uint)
                        Marshal.SizeOf<
                            NativeRecorderResultV1>())
            };

        EnsureOk(
            NativeMethods.RecorderResult(
                _handle,
                ref result));

        return new NativeRecorderResult(
            result.OutputPath?.Trim() ??
                string.Empty,
            result.DiagnosticsPath?.Trim() ??
                string.Empty);
    }

    public void Dispose()
    {
        var handle =
            _handle;

        if (handle == IntPtr.Zero)
            return;

        _handle = IntPtr.Zero;
        NativeMethods.Destroy(
            handle);

        GC.SuppressFinalize(this);
    }

    ~NativeBridgeClient()
    {
        if (_handle != IntPtr.Zero)
        {
            NativeMethods.Destroy(
                _handle);
        }
    }

    private void ThrowIfDisposed()
    {
        ObjectDisposedException.ThrowIf(
            _handle == IntPtr.Zero,
            this);
    }

    private static void EnsureOk(
        int status)
    {
        if (status != 0)
        {
            throw new InvalidOperationException(
                $"Native bridge returned status {status}.");
        }
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct NativeRectV1
    {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct NativeOverlaySnapshotV1
    {
        public uint StructSize;
        public uint CaptureMode;
        public ulong SourceToken;
        public NativeRectV1 BoundaryRect;
        public NativeRectV1 RegionRect;
        public byte Visible;
        public byte Editable;
        public byte RegionValid;
        public byte Reserved0;
    }

    [StructLayout(
        LayoutKind.Sequential,
        CharSet = CharSet.Unicode)]
    private struct NativeSourceV1
    {
        public uint StructSize;
        public uint Kind;
        public ulong Token;
        public NativeRectV1 ScreenRect;

        [MarshalAs(
            UnmanagedType.ByValTStr,
            SizeConst = 256)]
        public string Label;
    }

    [StructLayout(
        LayoutKind.Sequential,
        CharSet = CharSet.Unicode)]
    private struct NativeDeviceV1
    {
        public uint StructSize;
        public uint Kind;
        public ulong Token;

        [MarshalAs(
            UnmanagedType.ByValTStr,
            SizeConst = 256)]
        public string Name;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct NativeAudioMeterV1
    {
        public uint StructSize;
        public uint SystemChannels;
        public uint MicrophoneChannels;
        public uint Reserved;
        public float SystemLeft;
        public float SystemRight;
        public float MicrophoneLeft;
        public float MicrophoneRight;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct NativeRecorderSnapshotV1
    {
        public uint StructSize;
        public uint State;
        public long ElapsedTicks;
        public byte WorkerFinished;
        public byte Reserved0;
        public byte Reserved1;
        public byte Reserved2;
        public float CameraCenterX;
        public float CameraCenterY;
        public float CameraZoom;
        public ulong CaptureReceived;
        public ulong VideoRendered;
        public ulong EncoderSubmitted;
        public ulong EncoderBackpressure;
        public uint ErrorCode;
        public uint ErrorDetail;
    }

    [StructLayout(
        LayoutKind.Sequential,
        CharSet = CharSet.Unicode)]
    private struct NativeStartRequestV1
    {
        public uint StructSize;
        public uint CaptureMode;
        public ulong SourceToken;
        public uint FrameRate;
        public uint VisualMode;
        public uint Flags;
        public uint Reserved0;
        public ulong MicrophoneDeviceToken;
        public ulong CameraDeviceToken;

        [MarshalAs(
            UnmanagedType.ByValTStr,
            SizeConst = 512)]
        public string OutputFolder;

        public uint HoldZoomModifiers;
        public uint HoldZoomVirtualKey;
        public uint OverviewPeekModifiers;
        public uint OverviewPeekVirtualKey;
        public float PresenterZoom;
        public uint Reserved1;
        public uint SpotlightSize;
        public uint SpotlightMotion;
        public float SpotlightDimStrength;
        public uint Reserved2;
        public float SystemGain;
        public float MicrophoneGain;
        public uint SystemMuted;
        public uint MicrophoneMuted;
    }

    [StructLayout(
        LayoutKind.Sequential,
        CharSet = CharSet.Unicode)]
    private struct NativeRecorderResultV1
    {
        public uint StructSize;
        public uint Reserved0;

        [MarshalAs(
            UnmanagedType.ByValTStr,
            SizeConst = 512)]
        public string OutputPath;

        [MarshalAs(
            UnmanagedType.ByValTStr,
            SizeConst = 512)]
        public string DiagnosticsPath;
    }

    private static class NativeMethods
    {
        [DllImport(
            LibraryName,
            EntryPoint = "arssyut_bridge_abi_version",
            CallingConvention = CallingConvention.Cdecl)]
        public static extern uint AbiVersion();

        [DllImport(
            LibraryName,
            EntryPoint = "arssyut_bridge_create",
            CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr Create();

        [DllImport(
            LibraryName,
            EntryPoint = "arssyut_bridge_destroy",
            CallingConvention = CallingConvention.Cdecl)]
        public static extern void Destroy(
            IntPtr handle);

        [DllImport(
            LibraryName,
            EntryPoint = "arssyut_bridge_refresh_sources",
            CallingConvention = CallingConvention.Cdecl)]
        public static extern int RefreshSources(
            IntPtr handle,
            nuint ownWindow);

        [DllImport(
            LibraryName,
            EntryPoint = "arssyut_bridge_source_count",
            CallingConvention = CallingConvention.Cdecl)]
        public static extern int SourceCount(
            IntPtr handle,
            out uint count);

        [DllImport(
            LibraryName,
            EntryPoint = "arssyut_bridge_source_at",
            CallingConvention = CallingConvention.Cdecl,
            CharSet = CharSet.Unicode)]
        public static extern int SourceAt(
            IntPtr handle,
            uint index,
            ref NativeSourceV1 source);

        [DllImport(
            LibraryName,
            EntryPoint = "arssyut_bridge_overlay_set_target",
            CallingConvention = CallingConvention.Cdecl)]
        public static extern int OverlaySetTarget(
            IntPtr handle,
            uint captureMode,
            ulong sourceToken);

        [DllImport(
            LibraryName,
            EntryPoint = "arssyut_bridge_overlay_hide",
            CallingConvention = CallingConvention.Cdecl)]
        public static extern int OverlayHide(
            IntPtr handle);

        [DllImport(
            LibraryName,
            EntryPoint = "arssyut_bridge_overlay_snapshot",
            CallingConvention = CallingConvention.Cdecl)]
        public static extern int OverlaySnapshot(
            IntPtr handle,
            ref NativeOverlaySnapshotV1 snapshot);

        [DllImport(
            LibraryName,
            EntryPoint = "arssyut_bridge_hotkey_register",
            CallingConvention = CallingConvention.Cdecl)]
        public static extern int HotkeyRegister(
            IntPtr handle,
            uint action,
            uint modifiers,
            uint virtualKey);

        [DllImport(
            LibraryName,
            EntryPoint = "arssyut_bridge_hotkey_probe",
            CallingConvention = CallingConvention.Cdecl)]
        public static extern int HotkeyProbe(
            IntPtr handle,
            uint modifiers,
            uint virtualKey);

        [DllImport(
            LibraryName,
            EntryPoint = "arssyut_bridge_hotkey_unregister",
            CallingConvention = CallingConvention.Cdecl)]
        public static extern int HotkeyUnregister(
            IntPtr handle,
            uint action);

        [DllImport(
            LibraryName,
            EntryPoint = "arssyut_bridge_hotkey_take_events",
            CallingConvention = CallingConvention.Cdecl)]
        public static extern int HotkeyTakeEvents(
            IntPtr handle,
            out uint events);

        [DllImport(
            LibraryName,
            EntryPoint = "arssyut_bridge_refresh_devices",
            CallingConvention = CallingConvention.Cdecl)]
        public static extern int RefreshDevices(
            IntPtr handle);

        [DllImport(
            LibraryName,
            EntryPoint = "arssyut_bridge_device_count",
            CallingConvention = CallingConvention.Cdecl)]
        public static extern int DeviceCount(
            IntPtr handle,
            uint kind,
            out uint count);

        [DllImport(
            LibraryName,
            EntryPoint = "arssyut_bridge_device_at",
            CallingConvention = CallingConvention.Cdecl,
            CharSet = CharSet.Unicode)]
        public static extern int DeviceAt(
            IntPtr handle,
            uint kind,
            uint index,
            ref NativeDeviceV1 device);

        [DllImport(
            LibraryName,
            EntryPoint = "arssyut_bridge_recorder_start",
            CallingConvention = CallingConvention.Cdecl,
            CharSet = CharSet.Unicode)]
        public static extern int RecorderStart(
            IntPtr handle,
            ref NativeStartRequestV1 request);

        [DllImport(
            LibraryName,
            EntryPoint = "arssyut_bridge_recorder_presenter_command",
            CallingConvention = CallingConvention.Cdecl)]
        public static extern int RecorderPresenterCommand(
            IntPtr handle,
            uint command);

        [DllImport(
            LibraryName,
            EntryPoint = "arssyut_bridge_recorder_commit_start",
            CallingConvention = CallingConvention.Cdecl)]
        public static extern int RecorderCommitStart(
            IntPtr handle);

        [DllImport(
            LibraryName,
            EntryPoint = "arssyut_bridge_recorder_stop",
            CallingConvention = CallingConvention.Cdecl)]
        public static extern int RecorderStop(
            IntPtr handle);

        [DllImport(
            LibraryName,
            EntryPoint = "arssyut_bridge_recorder_snapshot",
            CallingConvention = CallingConvention.Cdecl)]
        public static extern int RecorderSnapshot(
            IntPtr handle,
            ref NativeRecorderSnapshotV1 snapshot);

        [DllImport(
            LibraryName,
            EntryPoint = "arssyut_bridge_audio_meter",
            CallingConvention = CallingConvention.Cdecl)]
        public static extern int AudioMeter(
            IntPtr handle,
            ulong microphoneDeviceToken,
            uint flags,
            ref NativeAudioMeterV1 meter);

        [DllImport(
            LibraryName,
            EntryPoint = "arssyut_bridge_recorder_result",
            CallingConvention = CallingConvention.Cdecl,
            CharSet = CharSet.Unicode)]
        public static extern int RecorderResult(
            IntPtr handle,
            ref NativeRecorderResultV1 result);
    }
}
