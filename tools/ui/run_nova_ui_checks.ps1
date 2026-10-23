#!/usr/bin/env pwsh
# Repeatable local/CI checks for the managed Nova UI v0.2 slice.
$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..\..')
Push-Location $root
try {
    dotnet restore Nova.sln
    dotnet build Nova.sln -c Release --no-restore
    dotnet test Nova.sln -c Release --no-build --logger 'console;verbosity=minimal'
    dotnet format Nova.sln --verify-no-changes --no-restore
}
finally {
    Pop-Location
}
