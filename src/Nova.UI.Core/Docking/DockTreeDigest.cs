using System.Globalization;
using System.Security.Cryptography;
using System.Text;

namespace Nova.UI.Core.Docking;

internal static class DockTreeDigest
{
    public static string Compute(DockNode? root)
    {
        var builder = new StringBuilder();
        AppendNode(builder, root);
        return Hash(builder.ToString());
    }

    private static void AppendNode(StringBuilder builder, DockNode? node)
    {
        switch (node)
        {
            case null:
                builder.Append("null");
                break;

            case DockTabGroupNode group:
                builder.Append('T')
                    .Append(group.Id.ToString())
                    .Append('[')
                    .Append(string.Join(',', group.Panels.Select(panel => panel.ToString())))
                    .Append("|h:")
                    .Append(string.Join(',', group.HiddenPanels.Select(panel => panel.ToString())))
                    .Append('|')
                    .Append(group.ActivePanel?.ToString())
                    .Append(']');
                break;

            case DockSplitNode split:
                builder.Append('S')
                    .Append(split.Id.ToString())
                    .Append('(')
                    .Append(split.Orientation == DockOrientation.Horizontal ? 'H' : 'V')
                    .Append(',')
                    .Append(Format(split.FirstWeight))
                    .Append(',')
                    .Append(Format(split.SecondWeight))
                    .Append(")[");
                AppendNode(builder, split.First);
                builder.Append(',');
                AppendNode(builder, split.Second);
                builder.Append(']');
                break;

            default:
                builder.Append('?').Append(node.Id.ToString());
                break;
        }
    }

    private static string Format(double value)
        => value.ToString("R", CultureInfo.InvariantCulture);

    private static string Hash(string canonical)
    {
        var bytes = SHA256.HashData(Encoding.UTF8.GetBytes(canonical));
        return Convert.ToHexString(bytes);
    }
}
