@echo off

REM Batch job to configure Cimba build with MinGW-W64 on Windows
REM Sets the install directory as a Meson install prefix
REM Sets PATH entries to find the installed Cimba afterwards
REM
REM Copyright (C) Asbjørn M Bonvik 2025-26

setlocal

set "INSTALL_PREFIX=C:\Program Files\Cimba"
set "BUILD_DIR=build"
set "BUILD_TYPE=release"

echo [Cimba] Checking for MinGW gcc...
where gcc >nul 2>nul
if %errorlevel% neq 0 (
    echo [ERROR] gcc not found in PATH.
    echo.
    echo Please install MinGW-W64 and add its 'bin' to your PATH.
    pause
    exit /b 1
)

echo [Cimba] gcc found. Configuring Meson...
meson setup %BUILD_DIR% ^
    --native-file config/mingw.ini ^
    --prefix="%INSTALL_PREFIX%" ^
    --buildtype=%BUILD_TYPE% ^
    --reconfigure

    if %errorlevel% neq 0 (
        echo [ERROR] Meson setup failed.
        pause
        exit /b 1
    )

echo.
echo [Cimba] Configured in directory '%BUILD_DIR%'.
echo.

REM We will use Powershell to change the environment variables safely
where pwsh >nul 2>nul
if %errorlevel% equ 0 (
    set "PS_CMD=pwsh.exe"
) else (
    where powershell >nul 2>nul
    if %errorlevel% equ 0 (
        set "PS_CMD=powershell.exe"
    ) else (
        echo No PowerShell executable found on this system.
        echo Please adjust PATH, C_INCLUDE_PATH, and LIBRARY_PATH manually.
        exit /B 1
    )
)

REM Display the command found
ECHO PowerShell command set to: %PS_CMD%

%PS_CMD% -NoProfile -ExecutionPolicy Bypass -File ".\config\AddToEnv.ps1" -InstallPrefix "%INSTALL_PREFIX%"

echo [Cimba] Setup done.
echo To compile: meson compile -C build
echo To install: meson install -C build
echo To run unit test suite: meson test -C build
echo.
pause

endlocal
exit /b 0
