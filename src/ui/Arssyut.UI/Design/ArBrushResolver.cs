using System;
using Avalonia.Controls;
using Avalonia.Media;

namespace Arssyut.UI.Design;

internal static class ArBrushResolver
{
    public static IBrush Require(
        IResourceHost host,
        string key)
    {
        ArgumentNullException.ThrowIfNull(host);

        if (host.TryFindResource(
                key,
                out var value) &&
            value is IBrush brush)
        {
            return brush;
        }

        throw new InvalidOperationException(
            $"Required Arssyut semantic brush '{key}' was not found.");
    }
}
