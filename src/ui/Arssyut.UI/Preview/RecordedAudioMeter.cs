using System;

namespace Arssyut.UI.Preview;

/// <summary>
/// Per-source post-fader level for the recorded 48 kHz stereo program.
///
/// NativeAudioMeter reads the endpoint's PRE-fader input peaks. The recorder
/// mixer applies the same bounded linear gain/mute to each canonical source.
/// Map each channel independently to the resulting post-fader peak; do not
/// modify WASAPI levels, insert a filter, or pretend to meter combined AAC.
/// </summary>
public static class RecordedAudioMeter
{
    public static float PostFaderPeak(
        float inputPeak, bool sourceEnabled, int gainPercent, bool muted)
    {
        if (!sourceEnabled || muted || !float.IsFinite(inputPeak) ||
            inputPeak <= 0 || gainPercent <= 0)
            return 0f;

        var gain = Math.Clamp(gainPercent, 0, 100) / 100f;
        return Math.Clamp(inputPeak * gain, 0f, 1f);
    }

    public static double DisplayPercent(float postFaderPeak)
    {
        if (!float.IsFinite(postFaderPeak) || postFaderPeak <= 0)
            return 0;

        var db = 20.0 * Math.Log10(Math.Clamp(postFaderPeak, 0.000001f, 1f));
        return Math.Clamp((db + 60.0) * (100.0 / 60.0), 0.0, 100.0);
    }

    public static string DbLabel(float postFaderPeak)
    {
        if (!float.IsFinite(postFaderPeak) || postFaderPeak <= 0)
            return "−∞ dB";

        var db = 20.0 * Math.Log10(Math.Clamp(postFaderPeak, 0.000001f, 1f));
        return $"{Math.Clamp(db, -60.0, 0.0):0} dB";
    }
}
