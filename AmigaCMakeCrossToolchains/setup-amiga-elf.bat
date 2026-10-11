@echo off
setlocal enabledelayedexpansion

echo Setting up amiga-elf.cmake for CMake...

:: Check if CMake is installed
where cmake >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo Error: CMake not found. Please make sure CMake is installed and in your PATH.
    exit /b 1
)

:: Get CMake version
for /f "tokens=3" %%i in ('cmake --version ^| findstr /B /C:"cmake version"') do (
    set "CMAKE_FULL_VERSION=%%i"
)

if "!CMAKE_FULL_VERSION!"=="" (
    echo Error: Could not determine CMake version.
    exit /b 1
)

:: Extract only major.minor version (e.g., 3.28 from 3.28.4)
for /f "tokens=1,2 delims=." %%a in ("!CMAKE_FULL_VERSION!") do (
    set "CMAKE_VERSION=%%a.%%b"
)

echo Detected CMake version: !CMAKE_VERSION!

:: Determine CMake platform directory
set "CMAKE_PLATFORM_DIR=C:\mingw64\share\cmake-!CMAKE_VERSION!\Modules\Platform"

:: Check if the directory exists
if not exist "!CMAKE_PLATFORM_DIR!" (
    :: Try alternate location - CMake might be installed in Program Files (x86)
    set "CMAKE_PLATFORM_DIR=C:\Program Files\CMake\share\cmake-!CMAKE_VERSION!\Modules\Platform"
    
    if not exist "!CMAKE_PLATFORM_DIR!" (
        echo Error: Could not find CMake platform directory.
        echo Please manually copy Generic.cmake to amiga-elf.cmake in your CMake platform directory.
        exit /b 1
    )
)

echo Found CMake platform directory: !CMAKE_PLATFORM_DIR!

:: Check if Generic.cmake exists
set "GENERIC_CMAKE_FILE=!CMAKE_PLATFORM_DIR!\Generic.cmake"

if not exist "!GENERIC_CMAKE_FILE!" (
    echo Error: Generic.cmake not found at: !GENERIC_CMAKE_FILE!
    exit /b 1
)

:: Create amiga-elf.cmake
set "AMIGA_ELF_CMAKE_FILE=!CMAKE_PLATFORM_DIR!\amiga-elf.cmake"

:: Try to copy the file
:: Note: This will require administrator privileges if installed in Program Files
echo Creating !AMIGA_ELF_CMAKE_FILE!
echo This may require administrator privileges if CMake is installed in Program Files.

copy "!GENERIC_CMAKE_FILE!" "!AMIGA_ELF_CMAKE_FILE!" >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo.
    echo Copy failed. Administrator privileges required.
    echo Please run this script as administrator or manually copy:
    echo   !GENERIC_CMAKE_FILE!
    echo to:
    echo   !AMIGA_ELF_CMAKE_FILE!
    exit /b 1
) else (
    echo Created: !AMIGA_ELF_CMAKE_FILE!
    echo Setup complete! You can now use the m68k-bartman.cmake toolchain with amiga-elf.
)

endlocal
