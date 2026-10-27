using Nova.UI.Avalonia.Docking;
using Nova.UI.Core.Docking;

namespace Nova.UI.Controls.Tests;

/// <summary>
/// D10-C C-1/C-3 policy gates (ADR-011 §十一·三). Every rule is pure and
/// screen data is injected explicitly; the model layer never appears here.
/// </summary>
[TestClass]
public sealed class DockScreenPlacementTests
{
    private static readonly DockScreenArea Primary = new(
        "primary",
        new DockPixelRect(0, 0, 2560, 1600),
        new DockPixelRect(0, 0, 2560, 1552),
        1d,
        Primary: true);

    private static readonly DockScreenArea Secondary = new(
        "secondary",
        new DockPixelRect(2560, 0, 1920, 1080),
        new DockPixelRect(2560, 0, 1920, 1040),
        1d,
        Primary: false);

    private static readonly DockScreenArea HiDpi = new(
        "hidpi",
        new DockPixelRect(0, 0, 5120, 2880),
        new DockPixelRect(0, 0, 5120, 2800),
        2d,
        Primary: true);

    // PL-01 — O-2.1: a rectangle fully inside a non-primary work area stays put;
    // the DM-RP-02 regression where off-primary floats were never re-placed.
    [TestMethod]
    public void Pl01_a_rectangle_inside_a_non_primary_work_area_is_kept()
    {
        var placement = DockScreenGeometry.PlaceWindowBounds(
            new DockBounds(2660d, 100d, 640d, 480d),
            DockWindowFrame.None,
            new[] { Primary, Secondary });

        Assert.AreEqual(DockPlacementReason.InsideWorkArea, placement.Reason);
        Assert.AreEqual(Secondary, placement.Screen);
        Assert.AreEqual(new DockBounds(2660d, 100d, 640d, 480d), placement.Bounds);
        Assert.IsFalse(placement.Changed);
    }

    // PL-02 — O-2.2: a partial overlap clamps into the work area with the
    // largest overlap, not the primary and not the first screen.
    [TestMethod]
    public void Pl02_a_partial_overlap_uses_the_largest_overlap_and_clamps()
    {
        var placement = DockScreenGeometry.PlaceWindowBounds(
            new DockBounds(2400d, 100d, 640d, 480d),
            DockWindowFrame.None,
            new[] { Primary, Secondary });

        Assert.AreEqual(DockPlacementReason.Clamped, placement.Reason);
        Assert.AreEqual(Secondary, placement.Screen);
        Assert.AreEqual(new DockBounds(2560d, 100d, 640d, 480d), placement.Bounds);
        Assert.IsTrue(placement.Moved);
        Assert.IsFalse(placement.Resized);
    }

    // PL-03 — O-2.3: no overlap at all falls back to the primary work area.
    [TestMethod]
    public void Pl03_no_overlap_falls_back_to_the_primary_work_area()
    {
        var placement = DockScreenGeometry.PlaceWindowBounds(
            new DockBounds(20000d, 20000d, 640d, 480d),
            DockWindowFrame.None,
            new[] { Primary, Secondary });

        Assert.AreEqual(DockPlacementReason.PrimaryFallback, placement.Reason);
        Assert.AreEqual(Primary, placement.Screen);
        Assert.AreEqual(new DockBounds(1920d, 1072d, 640d, 480d), placement.Bounds);
    }

    [TestMethod]
    public void Pl03b_no_overlap_and_no_primary_falls_back_deterministically()
    {
        var secondaryOnly = Secondary with { Primary = false };
        var other = Primary with { Primary = false, Device = "other" };

        var first = DockScreenGeometry.PlaceWindowBounds(
            new DockBounds(20000d, 20000d, 640d, 480d),
            DockWindowFrame.None,
            new[] { secondaryOnly, other });
        var second = DockScreenGeometry.PlaceWindowBounds(
            new DockBounds(20000d, 20000d, 640d, 480d),
            DockWindowFrame.None,
            new[] { other, secondaryOnly });

        Assert.AreEqual(DockPlacementReason.PrimaryFallback, first.Reason);
        Assert.AreEqual(first, second);
        Assert.AreEqual("other", first.Screen!.Value.Device);
    }

    // PL-04 — O-2.4: a window larger than the work area is shrunk first, then
    // positioned; this is the DM-RP-03 model (2960x1852 on a 2560x1552 area).
    [TestMethod]
    public void Pl04_an_oversized_window_is_shrunk_before_it_is_clamped()
    {
        var placement = DockScreenGeometry.PlaceWindowBounds(
            new DockBounds(100d, 100d, 2960d, 1852d),
            DockWindowFrame.None,
            new[] { Primary, Secondary });

        Assert.AreEqual(DockPlacementReason.Clamped, placement.Reason);
        Assert.AreEqual(Primary, placement.Screen);
        Assert.AreEqual(new DockBounds(0d, 0d, 2560d, 1552d), placement.Bounds);
        Assert.IsTrue(placement.Resized);
        Assert.IsTrue(placement.Moved);
    }

    // PL-05 — O-3.1: the pointer monitor wins over the largest overlap, so a
    // tear-off that began inside the main window lands where the pointer is.
    [TestMethod]
    public void Pl05_a_preferred_screen_wins_over_the_largest_overlap()
    {
        var placement = DockScreenGeometry.PlaceWindowBounds(
            new DockBounds(100d, 100d, 640d, 480d),
            DockWindowFrame.None,
            new[] { Primary, Secondary },
            preferredScreen: Secondary);

        Assert.AreEqual(DockPlacementReason.Clamped, placement.Reason);
        Assert.AreEqual(Secondary, placement.Screen);
        Assert.AreEqual(new DockBounds(2560d, 100d, 640d, 480d), placement.Bounds);
    }

    // PL-06 — O-2.5: the non-client frame is part of the visibility rule. The
    // resulting outer rectangle must touch, never cross, the work area edges.
    [TestMethod]
    public void Pl06_the_outer_frame_is_kept_inside_the_work_area()
    {
        var frame = new DockWindowFrame(8d, 31d, 8d, 8d);

        var placement = DockScreenGeometry.PlaceWindowBounds(
            new DockBounds(2000d, 1200d, 640d, 480d),
            frame,
            new[] { Primary });

        Assert.AreEqual(DockPlacementReason.Clamped, placement.Reason);
        Assert.AreEqual(new DockBounds(1904d, 1033d, 640d, 480d), placement.Bounds);
        Assert.IsTrue(placement.Moved);
        Assert.IsFalse(placement.Resized);

        var outerX = placement.Bounds.X;
        var outerY = placement.Bounds.Y;
        var outerWidth = placement.Bounds.Width + frame.Horizontal;
        var outerHeight = placement.Bounds.Height + frame.Vertical;
        Assert.AreEqual(1904d, outerX);
        Assert.AreEqual(1033d, outerY);
        Assert.AreEqual(2560d, outerX + outerWidth);
        Assert.AreEqual(1552d, outerY + outerHeight);
    }

    // PL-07 — O-2/O-5: placement is deterministic and idempotent, which is what
    // replaces any fixed delay in the host's two-pass show sequence.
    [TestMethod]
    public void Pl07_placement_is_deterministic_and_idempotent()
    {
        var frame = new DockWindowFrame(8d, 31d, 8d, 8d);
        var input = new DockBounds(2000d, 1200d, 640d, 480d);

        var first = DockScreenGeometry.PlaceWindowBounds(input, frame, new[] { Primary, Secondary });
        var second = DockScreenGeometry.PlaceWindowBounds(input, frame, new[] { Primary, Secondary });
        var again = DockScreenGeometry.PlaceWindowBounds(first.Bounds, frame, new[] { Primary, Secondary });

        Assert.AreEqual(first, second);
        Assert.AreEqual(DockPlacementReason.InsideWorkArea, again.Reason);
        Assert.AreEqual(first.Bounds, again.Bounds);
        Assert.IsFalse(again.Changed);
    }

    // PL-08 — no screen data is a declared no-op, not a guess. The model keeps
    // whatever it was handed and the host reports why it could not place.
    [TestMethod]
    public void Pl08_no_screen_information_returns_the_input_unchanged()
    {
        var bounds = new DockBounds(20000d, 20000d, 640d, 480d);

        var none = DockScreenGeometry.PlaceWindowBounds(bounds, DockWindowFrame.None, null);
        var empty = DockScreenGeometry.PlaceWindowBounds(
            bounds,
            DockWindowFrame.None,
            Array.Empty<DockScreenArea>());

        Assert.AreEqual(DockPlacementReason.NoScreens, none.Reason);
        Assert.AreEqual(bounds, none.Bounds);
        Assert.IsNull(none.Screen);
        Assert.AreEqual(none, empty);
    }

    // PL-09 — an invalid model rectangle is reported, never silently repaired.
    [TestMethod]
    public void Pl09_an_invalid_model_rectangle_is_not_placed()
    {
        var bounds = new DockBounds(-40000d, 0d, 640d, 480d);

        var placement = DockScreenGeometry.PlaceWindowBounds(
            bounds,
            DockWindowFrame.None,
            new[] { Primary });

        Assert.AreEqual(DockPlacementReason.NotPlaceable, placement.Reason);
        Assert.AreEqual(bounds, placement.Bounds);
        Assert.IsNull(placement.Screen);
    }

    // PL-10 — O-3.1: pointer resolution prefers a work area, then a monitor
    // bounds, and never invents a screen for an off-desktop point.
    [TestMethod]
    public void Pl10_screen_from_point_uses_work_area_then_monitor_bounds()
    {
        Assert.AreEqual(
            Secondary,
            DockScreenGeometry.ScreenFromPoint(new[] { Primary, Secondary }, 2600, 100));
        Assert.AreEqual(
            Secondary,
            DockScreenGeometry.ScreenFromPoint(new[] { Primary, Secondary }, 2600, 1060));
        Assert.IsNull(
            DockScreenGeometry.ScreenFromPoint(new[] { Primary, Secondary }, 9000, 9000));
        Assert.IsNull(DockScreenGeometry.ScreenFromPoint(null, 0, 0));
    }

    // PL-11 — O-3.2: a DIP rectangle is converted with the target monitor's
    // own scaling, never with another window's scaling.
    [TestMethod]
    public void Pl11_placement_converts_with_the_target_screen_scaling()
    {
        var placement = DockScreenGeometry.PlaceWindowBounds(
            new DockBounds(2000d, 200d, 640d, 480d),
            DockWindowFrame.None,
            new[] { HiDpi });

        Assert.AreEqual(2d, placement.Screen!.Value.EffectiveScaling);
        Assert.AreEqual(new DockBounds(1920d, 200d, 640d, 480d), placement.Bounds);
        Assert.IsTrue(placement.Moved);
    }

    // PL-12 — C-3 capture semantics: the model position is the outer origin in
    // DIP and the model size is the real client area, so the platform frame
    // pixels (656x519 in the D10-B measurement) never become a model extent.
    [TestMethod]
    public void Pl12_capture_uses_the_client_size_and_not_the_outer_frame()
    {
        var captured = DockScreenGeometry.CaptureClientBounds(
            new DockPixelRect(100, 50, 656, 519),
            new DockPixelRect(108, 81, 640, 480),
            1d);

        Assert.AreEqual(new DockBounds(100d, 50d, 640d, 480d), captured);
        Assert.AreNotEqual(656d, captured.Width);
        Assert.AreNotEqual(519d, captured.Height);
    }

    [TestMethod]
    public void Pl12b_capture_and_frame_use_the_capturing_window_scaling()
    {
        var outer = new DockPixelRect(200, 100, 1312, 1038);
        var client = new DockPixelRect(216, 162, 1280, 960);

        var captured = DockScreenGeometry.CaptureClientBounds(outer, client, 2d);
        var frame = DockScreenGeometry.FrameFromPlatformRects(outer, client, 2d);

        Assert.AreEqual(new DockBounds(100d, 50d, 640d, 480d), captured);
        Assert.AreEqual(new DockWindowFrame(8d, 31d, 8d, 8d), frame);
    }

    [TestMethod]
    public void Pl12c_a_degenerate_platform_rectangle_yields_no_frame()
    {
        var frame = DockScreenGeometry.FrameFromPlatformRects(
            new DockPixelRect(100, 50, 656, 519),
            new DockPixelRect(108, 81, 0, 480),
            1d);

        Assert.AreEqual(DockWindowFrame.None, frame);
    }
}
