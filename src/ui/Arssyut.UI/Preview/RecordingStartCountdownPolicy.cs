using System;
using Arssyut.UI.Interop;

namespace Arssyut.UI.Preview;

/// <summary>
/// Presentation-only timing policy for the visible recording countdown.
/// Native RecorderSession remains the sole readiness/media-clock authority.
/// </summary>
public static class RecordingStartCountdownPolicy
{
    public static readonly TimeSpan Duration =
        TimeSpan.FromSeconds(3);

    public static int NumberForElapsed(
        TimeSpan elapsed)
    {
        if (elapsed < TimeSpan.Zero)
            elapsed = TimeSpan.Zero;

        if (elapsed < TimeSpan.FromSeconds(1))
            return 3;

        if (elapsed < TimeSpan.FromSeconds(2))
            return 2;

        return 1;
    }

    public static bool CanCommit(
        TimeSpan elapsed,
        NativeRecorderState state) =>
        elapsed >= Duration &&
        state == NativeRecorderState.Armed;

    public static bool IsPreCommitState(
        NativeRecorderState state) =>
        state is
            NativeRecorderState.Preparing or
            NativeRecorderState.Armed;

    // The dim layer may clear only after native media time is authoritative.
    // This is intentionally stricter than CommitStart() returning Ok.
    public static bool CanRevealAction(
        NativeRecorderState state) =>
        state == NativeRecorderState.Recording;
}
