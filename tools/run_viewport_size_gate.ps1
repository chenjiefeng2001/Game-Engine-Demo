param(
    [string]$Config = "Debug",
    [int]$TimeoutSec = 180
)
$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$exe = Join-Path $repoRoot "editor_avalonia\bin\$Config\net9.0\AvaloniaEditor.exe"
$nativeDll = Join-Path $repoRoot "build\bridge\$Config\EditorBridge.dll"
if (-not (Test-Path $exe)) { throw "managed host not found: $exe" }
if (-not (Test-Path $nativeDll)) { throw "native bridge not found: $nativeDll" }
$psi = [System.Diagnostics.ProcessStartInfo]::new()
$psi.FileName = $exe
$psi.WorkingDirectory = $repoRoot
$psi.UseShellExecute = $false
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true
$psi.ArgumentList.Add('--viewportsize')
$proc = [System.Diagnostics.Process]::Start($psi)
$stdoutTask = $proc.StandardOutput.ReadToEndAsync()
$stderrTask = $proc.StandardError.ReadToEndAsync()
$exited = $proc.WaitForExit($TimeoutSec * 1000)
$timedOut = -not $exited
if ($timedOut) { try { $proc.Kill($true) } catch { }; $proc.WaitForExit(5000) | Out-Null; $exitCode = -1 } else { $exitCode = $proc.ExitCode }
$stdout = $stdoutTask.GetAwaiter().GetResult()
$stderr = $stderrTask.GetAwaiter().GetResult()
$marker = '[VIEWPORT-SIZE GATE] ALL GREEN'
$green = (-not $timedOut) -and ($exitCode -eq 0) -and ($stdout -match [regex]::Escape($marker))
foreach ($line in ($stdout -split "`r?`n")) { if ($line -match '^\[VIEWPORT-SIZE') { Write-Host "    $($line.Trim())" } }
if ($green) { Write-Host ("[viewportsize] GREEN  exit={0}" -f $exitCode) -ForegroundColor Green; exit 0 }
$detail = if ($timedOut) { "timeout after ${TimeoutSec}s" } else { "exit=$exitCode" }
Write-Host "[viewportsize] FAIL  $detail" -ForegroundColor Red
foreach ($line in (($stderr -split "`r?`n") + ($stdout -split "`r?`n"))) { if ($line -match 'FAIL|Unhandled|Exception') { Write-Host "  $($line.Trim())" -ForegroundColor Red } }
exit 1
