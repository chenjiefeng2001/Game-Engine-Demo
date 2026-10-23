#!/usr/bin/env pwsh
# Manual screenshot capture for the Playground / Editor shell.
# Requires an interactive Windows desktop; this is NOT a CI gate.
param(
    [string]$Project = 'samples/Nova.UI.Playground/Nova.UI.Playground.csproj',
    [string]$OutputDirectory = 'artifacts/screenshots',
    [int]$WaitMilliseconds = 3500
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class NovaNative {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdcBlt, uint flags);
}
"@

$root = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$projectPath = Resolve-Path (Join-Path $root $Project)
$outputPath = Join-Path $root $OutputDirectory
New-Item -ItemType Directory -Force -Path $outputPath | Out-Null

dotnet build $projectPath -c Release --nologo | Out-Host
$assemblyName = [System.IO.Path]::GetFileNameWithoutExtension($projectPath)
$exe = Join-Path (Split-Path $projectPath) "bin\Release\net9.0\$assemblyName.exe"
if (-not (Test-Path $exe)) { throw "Built executable not found: $exe" }

$process = Start-Process -FilePath $exe -PassThru
try {
    $deadline = (Get-Date).AddSeconds(20)
    while ($process.MainWindowHandle -eq 0 -and (Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 250
        $process.Refresh()
    }
    if ($process.MainWindowHandle -eq 0) { throw 'Window handle was not available.' }

    [NovaNative]::SetForegroundWindow($process.MainWindowHandle) | Out-Null
    Start-Sleep -Milliseconds $WaitMilliseconds

    $rect = New-Object NovaNative+RECT
    if (-not [NovaNative]::GetWindowRect($process.MainWindowHandle, [ref]$rect)) {
        throw 'GetWindowRect failed.'
    }

    $width = $rect.Right - $rect.Left
    $height = $rect.Bottom - $rect.Top
    $bitmap = New-Object System.Drawing.Bitmap($width, $height)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        # PrintWindow captures the target window even when another window is on top.
        $hdc = $graphics.GetHdc()
        try {
            $printed = [NovaNative]::PrintWindow($process.MainWindowHandle, $hdc, 2)
        }
        finally {
            $graphics.ReleaseHdc($hdc)
        }

        if (-not $printed) {
            $graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0,
                (New-Object System.Drawing.Size($width, $height)))
        }

        $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
        $file = Join-Path $outputPath "$assemblyName-$stamp.png"
        $bitmap.Save($file, [System.Drawing.Imaging.ImageFormat]::Png)
        Write-Host "Saved $file"
    }
    finally {
        $graphics.Dispose()
        $bitmap.Dispose()
    }
}
finally {
    if (-not $process.HasExited) { $process.Kill() }
}
