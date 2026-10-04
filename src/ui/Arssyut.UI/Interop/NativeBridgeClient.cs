using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using Avalonia;
using Arssyut.UI.Preview;

namespace Arssyut.UI.Interop;

public enum NativeBridgeAvailability
{
    Available,
    MissingLibrary,
    IncompatibleAbi,
    InitializationFailed
}

public enum NativeDeviceKind : uint
{
    Microphone = 0,
    Camera = 1
}

public sealed record NativeDeviceItem(
    ulong Token,
    NativeDeviceKind Kind,
    string Name);

public sealed record NativeDeviceSnapshot(
    IReadOnlyList<NativeDeviceItem> Microphones,
    IReadOnlyList<NativeDeviceItem> Cameras);

public sealed record NativeRecorderSnapshot(
    uint State,
    long ElapsedTicks,
    bool WorkerFinished,
    float CameraCenterX,
    float CameraCenterY,
    float CameraZoom,
    ulong CaptureReceived,
    ulong VideoRendered,
    ulong EncoderSubmitted,
    ulong EncoderBackpressure);

public sealed class NativeBridgeClient : IDisposable
{
    private const string LibraryName =
        "arssyut_native_bridge";
    private const uint ExpectedAbi = 1;

    private IntPtr _handle;

    private NativeBridgeClient(
        IntPtr handle)
    {
        _handle = handle;
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
            return NativeBridgeAvailability.MissingLibrary;
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
            snapshot.State,
            snapshot.ElapsedTicks,
            snapshot.WorkerFinished != 0,
            snapshot.CameraCenterX,
            snapshot.CameraCenterY,
            snapshot.CameraZoom,
            snapshot.CaptureReceived,
            snapshot.VideoRendered,
            snapshot.EncoderSubmitted,
            snapshot.EncoderBackpressure);
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
            EntryPoint = "arssyut_bridge_recorder_snapshot",
            CallingConvention = CallingConvention.Cdecl)]
        public static extern int RecorderSnapshot(
            IntPtr handle,
            ref NativeRecorderSnapshotV1 snapshot);
    }
}
