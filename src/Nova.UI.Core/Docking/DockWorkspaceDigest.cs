using System.Globalization;
using System.Security.Cryptography;
using System.Text;

namespace Nova.UI.Core.Docking;

internal static class DockWorkspaceDigest
{
    public static string Compute(DockTree mainRoot, IReadOnlyList<DockFloatRoot> floatingRoots)
    {
        var builder = new StringBuilder();
        builder.Append("W3|main=");
        AppendString(builder, mainRoot.Digest);
        builder.Append("|floats=").Append(floatingRoots.Count.ToString(CultureInfo.InvariantCulture));

        foreach (var root in floatingRoots)
        {
            builder.Append('|');
            AppendString(builder, root.Id.ToString());
            builder.Append(':');
            AppendString(builder, root.Root.Digest);
            builder.Append(':')
                .Append(Format(root.RestoreBounds.X)).Append(',')
                .Append(Format(root.RestoreBounds.Y)).Append(',')
                .Append(Format(root.RestoreBounds.Width)).Append(',')
                .Append(Format(root.RestoreBounds.Height))
                .Append(':')
                .Append(root.Maximized ? '1' : '0')
                .Append(':');
            AppendString(builder, root.ReturnTarget.GroupId.ToString());
            builder.Append(',')
                .Append(root.ReturnTarget.Index.ToString(CultureInfo.InvariantCulture));
        }

        return Hash(builder.ToString());
    }

    private static void AppendString(StringBuilder builder, string value)
    {
        builder.Append(value.Length.ToString(CultureInfo.InvariantCulture))
            .Append(':')
            .Append(value);
    }

    private static string Format(double value)
    {
        var normalized = value == 0d ? 0d : value;
        return normalized.ToString("R", CultureInfo.InvariantCulture);
    }

    private static string Hash(string canonical)
    {
        var bytes = SHA256.HashData(Encoding.UTF8.GetBytes(canonical));
        return Convert.ToHexString(bytes);
    }
}
