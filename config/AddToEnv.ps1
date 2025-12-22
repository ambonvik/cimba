# scripts/setup_env.ps1
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
$IncDir = Join-Path $InstallPrefix "include"
$LibDir = Join-Path $InstallPrefix "lib"

function Add-To-Machine-Path {
    param ($VarName, $Value)
    
    Write-Host "Checking $VarName..." -NoNewline
    
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

Write-Host "`n[Cimba Setup] Configuring Environment for: $InstallPrefix"

# 1. Runtime Path (for DLLs)
Add-To-Machine-Path "Path" $BinDir

# 2. Compile Path (for headers)
Add-To-Machine-Path "C_INCLUDE_PATH" $IncDir

# 3. Link Path (for libs)
Add-To-Machine-Path "LIBRARY_PATH" $LibDir

Write-Host "`n[Done] You may need to restart your terminal.`n"