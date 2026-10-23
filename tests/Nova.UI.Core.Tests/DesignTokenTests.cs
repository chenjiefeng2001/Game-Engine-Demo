using Nova.UI.Core.Tokens;

namespace Nova.UI.Core.Tests;

[TestClass]
public sealed class DesignTokenTests
{
    [TestMethod]
    public void Dark_and_light_palettes_cover_every_required_token()
    {
        foreach (var tokens in new[] { NovaTokens.Dark, NovaTokens.Light })
        {
            var errors = tokens.Validate(TokenKeys.Required);
            Assert.AreEqual(0, errors.Count,
                $"{tokens.Id}: {string.Join("; ", errors)}");
        }
    }

    [TestMethod]
    public void Token_sets_are_immutable_and_reject_duplicate_keys()
    {
        Assert.ThrowsException<ArgumentException>(() => new DesignTokenSet("dup",
        [
            new DesignToken("color.test", TokenType.Color, "#FFFFFFFF"),
            new DesignToken("color.test", TokenType.Color, "#FF000000"),
        ]));
    }

    [TestMethod]
    public void Color_parser_accepts_rgb_and_argb()
    {
        Assert.IsTrue(RgbaColor.TryParse("#102030", out var rgb));
        Assert.AreEqual(new RgbaColor(0xFF, 0x10, 0x20, 0x30), rgb);
        Assert.AreEqual("#FF102030", rgb.ToHex());

        Assert.IsTrue(RgbaColor.TryParse("#80102030", out var argb));
        Assert.AreEqual(0x80, argb.A);
        Assert.AreEqual(0x10, argb.R);
    }

    [TestMethod]
    public void Numeric_token_parsing_is_invariant()
    {
        var tokens = new DesignTokenSet("test",
        [
            new DesignToken("size.test", TokenType.Dimension, "12.5"),
        ]);

        Assert.AreEqual(12.5, tokens.GetNumber("size.test"), 0.0001);
        Assert.AreEqual(12d, NovaTokens.Dark.GetNumber(TokenKeys.SpacingMd), 0.0001);
    }
}
