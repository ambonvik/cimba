# config/AddToEnv.ps1
# Windows specific PowerShell script to set environment variables
# Copyright (C) Asbjørn M Bonvik 2025-26
#
param (
    [string]$InstallPrefix
)

if (-not $InstallPrefix) {
    # Fallback if variable is missing (e.g. running manually)
    $InstallPrefix = $env:MESON_INSTALL_PREFIX
}

if (-not $InstallPrefix) {
    Write-Host "Error: No install prefix found. Run via Meson or provide path." -ForegroundColor Red
    exit 1
}

$BinDir = Join-Path $InstallPrefix "bin"
$IncDir = Join-Path $InstallPrefix "include\cimba"
$IncIntDir = Join-Path $IncDir "internal"
$LibDir = Join-Path $InstallPrefix "lib"

function Add-To-Machine-Path {
    param ($VarName, $Value)

    Write-Host " Checking $VarName..." -NoNewline

    $Current = [Environment]::GetEnvironmentVariable($VarName, 'Machine')
    if ($null -eq $Current) { $Current = "" }

    if ($Current -notlike "*$Value*") {
        Write-Host " Adding to System Environment." -ForegroundColor Green
        # Handle semi-colon logic
        if ($Current.Length -gt 0) { $NewVal = $Current + ";" + $Value } else { $NewVal = $Value }

        [Environment]::SetEnvironmentVariable($VarName, $NewVal, 'Machine')
    } else {
        Write-Host " Already configured." -ForegroundColor Gray
    }
}

Write-Host "`n Configuring Environment for $InstallPrefix"

Add-To-Machine-Path "Path" $BinDir
Add-To-Machine-Path "C_INCLUDE_PATH" $IncDir
Add-To-Machine-Path "C_INCLUDE_PATH" $IncIntDir
Add-To-Machine-Path "LIBRARY_PATH" $LibDir

Write-Host "`n Done. You may need to restart your computer to make changes take effect.`n"