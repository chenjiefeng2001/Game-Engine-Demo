using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Input;
using Nova.UI.Core.Docking;
using AvaloniaHorizontal = Avalonia.Layout.HorizontalAlignment;
using AvaloniaVertical = Avalonia.Layout.VerticalAlignment;

namespace Nova.UI.Avalonia.Docking;

public static class NovaDockClasses
{
    public const string Splitter = "nova-dock-splitter";
}

public sealed class DockSplitCommitEventArgs : EventArgs
{
    public DockSplitCommitEventArgs(double firstWeight, double secondWeight)
    {
        FirstWeight = firstWeight;
        SecondWeight = secondWeight;
    }

    public double FirstWeight { get; }

    public double SecondWeight { get; }

    public bool Accepted { get; set; }
}

/// <summary>
/// Drag handle over a split boundary. The drag changes only the preview grid
/// measurements; one commit request is raised on release and the model remains
/// unchanged unless the host accepts it.
/// </summary>
public sealed class DockSplitterThumb : Thumb
{
    private const double Thickness = 6;
    private const double MinPanePixels = 32;

    private readonly Grid _grid;
    private readonly DockOrientation _orientation;
    private readonly double _modelFirstWeight;
    private readonly double _modelSecondWeight;
    private double _totalPixels;
    private double _firstPixels;
    private bool _dragging;
    private bool _captureLost;

    public DockSplitterThumb(
        Grid grid,
        DockNodeId splitId,
        DockOrientation orientation,
        double firstWeight,
        double secondWeight)
    {
        ArgumentNullException.ThrowIfNull(grid);
        if (!double.IsFinite(firstWeight) || !double.IsFinite(secondWeight)
            || firstWeight <= 0 || secondWeight <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(firstWeight));
        }

        _grid = grid;
        SplitId = splitId;
        _orientation = orientation;
        _modelFirstWeight = firstWeight;
        _modelSecondWeight = secondWeight;

        Classes.Add(NovaDockClasses.Splitter);
        Focusable = false;
        if (orientation == DockOrientation.Horizontal)
        {
            Width = Thickness;
            HorizontalAlignment = AvaloniaHorizontal.Right;
            VerticalAlignment = AvaloniaVertical.Stretch;
        }
        else
        {
            Height = Thickness;
            HorizontalAlignment = AvaloniaHorizontal.Stretch;
            VerticalAlignment = AvaloniaVertical.Bottom;
        }

        DragStarted += (_, _) => BeginDrag();
        DragDelta += (_, e) =>
            DragBy(orientation == DockOrientation.Horizontal ? e.Vector.X : e.Vector.Y);
        DragCompleted += (_, _) =>
        {
            if (_captureLost)
            {
                _captureLost = false;
                CancelDrag();
                return;
            }

            CompleteDrag();
        };
        PointerCaptureLost += (_, _) => CancelDrag();
    }

    public DockNodeId SplitId { get; }

    public bool IsDragging => _dragging;

    public event EventHandler<DockSplitCommitEventArgs>? CommitRequested;

    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnAttachedToVisualTree(e);
        Cursor ??= new Cursor(
            _orientation == DockOrientation.Horizontal
                ? StandardCursorType.SizeWestEast
                : StandardCursorType.SizeNorthSouth);
    }

    // Thumb raises DragCompleted from OnPointerCaptureLost as well as from a
    // real release. A lost capture must roll back instead of committing.
    protected override void OnPointerCaptureLost(PointerCaptureLostEventArgs e)
    {
        if (_dragging)
            _captureLost = true;
        base.OnPointerCaptureLost(e);
    }

    public void BeginDrag()
    {
        if (_dragging) return;
        if (!TryGetPaneSizes(out var total, out var first)) return;

        _totalPixels = total;
        _firstPixels = first;
        _captureLost = false;
        _dragging = true;
    }

    public void DragBy(double delta)
    {
        if (!_dragging || !double.IsFinite(delta)) return;

        var min = Math.Min(MinPanePixels, _totalPixels / 4d);
        _firstPixels = Math.Clamp(_firstPixels + delta, min, _totalPixels - min);
        ApplyPreview(_firstPixels, _totalPixels - _firstPixels);
    }

    public bool CompleteDrag()
    {
        if (!_dragging) return false;
        _dragging = false;

        if (_totalPixels < MinPanePixels * 2)
        {
            RestoreModelWeights();
            return false;
        }

        var firstWeight = _firstPixels / _totalPixels;
        var args = new DockSplitCommitEventArgs(firstWeight, 1d - firstWeight);
        CommitRequested?.Invoke(this, args);
        if (!args.Accepted)
        {
            RestoreModelWeights();
            return false;
        }

        return true;
    }

    public void CancelDrag()
    {
        if (!_dragging) return;
        _dragging = false;
        _captureLost = false;
        RestoreModelWeights();
    }

    private bool TryGetPaneSizes(out double total, out double first)
    {
        if (_orientation == DockOrientation.Horizontal)
        {
            total = _grid.ColumnDefinitions[0].ActualWidth
                    + _grid.ColumnDefinitions[1].ActualWidth;
            first = _grid.ColumnDefinitions[0].ActualWidth;
            if (total <= 0)
                total = _grid.Bounds.Width;
        }
        else
        {
            total = _grid.RowDefinitions[0].ActualHeight
                    + _grid.RowDefinitions[1].ActualHeight;
            first = _grid.RowDefinitions[0].ActualHeight;
            if (total <= 0)
                total = _grid.Bounds.Height;
        }

        if (total <= 0)
            return false;

        var sum = _modelFirstWeight + _modelSecondWeight;
        if (first <= 0 || first >= total)
            first = total * (_modelFirstWeight / sum);

        return first > 0 && first < total;
    }

    private void ApplyPreview(double first, double second)
    {
        if (_orientation == DockOrientation.Horizontal)
        {
            _grid.ColumnDefinitions[0].Width = new GridLength(first, GridUnitType.Pixel);
            _grid.ColumnDefinitions[1].Width = new GridLength(second, GridUnitType.Pixel);
        }
        else
        {
            _grid.RowDefinitions[0].Height = new GridLength(first, GridUnitType.Pixel);
            _grid.RowDefinitions[1].Height = new GridLength(second, GridUnitType.Pixel);
        }
    }

    private void RestoreModelWeights()
    {
        var sum = _modelFirstWeight + _modelSecondWeight;
        if (sum <= 0) return;

        if (_orientation == DockOrientation.Horizontal)
        {
            _grid.ColumnDefinitions[0].Width =
                new GridLength(_modelFirstWeight / sum, GridUnitType.Star);
            _grid.ColumnDefinitions[1].Width =
                new GridLength(_modelSecondWeight / sum, GridUnitType.Star);
        }
        else
        {
            _grid.RowDefinitions[0].Height =
                new GridLength(_modelFirstWeight / sum, GridUnitType.Star);
            _grid.RowDefinitions[1].Height =
                new GridLength(_modelSecondWeight / sum, GridUnitType.Star);
        }
    }
}
