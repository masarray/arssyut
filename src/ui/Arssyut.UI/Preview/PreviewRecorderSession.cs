using System;
using System.Diagnostics;

namespace Arssyut.UI.Preview;

public enum PreviewRecordingPhase
{
    Ready,
    Recording,
    Paused,
    Saved
}

public sealed class PreviewRecorderSession
{
    private readonly Stopwatch _stopwatch = new();

    public PreviewRecordingPhase Phase { get; private set; } =
        PreviewRecordingPhase.Ready;

    public bool MicrophoneEnabled { get; set; }

    public bool CameraEnabled { get; set; }

    public TimeSpan Elapsed => _stopwatch.Elapsed;

    public string SavedFileName { get; private set; } =
        string.Empty;

    public event EventHandler? Changed;

    public void Start()
    {
        if (Phase is PreviewRecordingPhase.Recording or PreviewRecordingPhase.Paused)
            return;

        SavedFileName = string.Empty;
        _stopwatch.Restart();
        Phase = PreviewRecordingPhase.Recording;
        Changed?.Invoke(this, EventArgs.Empty);
    }

    public void TogglePause()
    {
        if (Phase == PreviewRecordingPhase.Recording)
        {
            _stopwatch.Stop();
            Phase = PreviewRecordingPhase.Paused;
        }
        else if (Phase == PreviewRecordingPhase.Paused)
        {
            _stopwatch.Start();
            Phase = PreviewRecordingPhase.Recording;
        }
        else
        {
            return;
        }

        Changed?.Invoke(this, EventArgs.Empty);
    }

    public void Stop()
    {
        if (Phase is not (PreviewRecordingPhase.Recording or PreviewRecordingPhase.Paused))
            return;

        _stopwatch.Stop();
        SavedFileName =
            $"Arssyut-preview-{DateTime.Now:yyyyMMdd-HHmmss}.mp4";
        Phase = PreviewRecordingPhase.Saved;
        Changed?.Invoke(this, EventArgs.Empty);
    }

    public void Reset()
    {
        _stopwatch.Reset();
        SavedFileName = string.Empty;
        Phase = PreviewRecordingPhase.Ready;
        Changed?.Invoke(this, EventArgs.Empty);
    }
}
