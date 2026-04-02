@echo off
setlocal enabledelayedexpansion

rem Fast packaging for C++-only changes (reuses existing cooked content)
rem Usage: PackageFast.bat [--config {Debug,Development,Shipping}] [--no-zip]

set LOCAL_PATH=%~dp0
set FILE_N=-[%~n0]:

echo %FILE_N% [Batch params]: %*

set PACKAGE_CONFIG=Development
set DO_TARBALL=true

:arg-parse
if not "%1"=="" (
    if "%1"=="--config" (
        set PACKAGE_CONFIG=%2
        shift
    )
    if "%1"=="--no-zip" (
        set DO_TARBALL=false
    )
    if "%1"=="-h" (
        goto help
    )
    if "%1"=="--help" (
        goto help
    )
    shift
    goto :arg-parse
)

rem Resolve ROOT_PATH if not provided by environment
if not defined ROOT_PATH (
    set ROOT_PATH=%LOCAL_PATH%..\..\
)

rem Default installation dir if not set
if not defined INSTALLATION_DIR (
    set INSTALLATION_DIR=%ROOT_PATH%Build\
)

rem Get Unreal Engine root path
if not defined UE4_ROOT (
    set KEY_NAME="HKEY_LOCAL_MACHINE\SOFTWARE\EpicGames\Unreal Engine"
    set VALUE_NAME=InstalledDirectory
    for /f "usebackq tokens=1,2,*" %%A in (`reg query !KEY_NAME! /s /reg:64`) do (
        if "%%A" == "!VALUE_NAME!" (
            set UE4_ROOT=%%C
        )
    )
    if not defined UE4_ROOT goto error_unreal_no_found
)
if not "%UE4_ROOT:~-1%"=="\" set UE4_ROOT=%UE4_ROOT%\

rem Set packaging paths
for /f %%i in ('git describe --tags --dirty --always') do set CARLA_VERSION=%%i
if not defined CARLA_VERSION goto error_carla_version

set BUILD_FOLDER=%INSTALLATION_DIR%UE4Carla/%CARLA_VERSION%/
set DESTINATION_ZIP=%INSTALLATION_DIR%UE4Carla/CARLA_%CARLA_VERSION%.zip
set SOURCE=!BUILD_FOLDER!WindowsNoEditor/

if not exist "!BUILD_FOLDER!" mkdir "!BUILD_FOLDER!"

rem Build game binaries (no editor, faster)
echo "%UE4_ROOT%\Engine\Build\BatchFiles\Build.bat"^
    CarlaUE4^
    Win64^
    %PACKAGE_CONFIG%^
    -WaitMutex^
    -FromMsBuild^
    "%ROOT_PATH%Unreal/CarlaUE4/CarlaUE4.uproject"
call "%UE4_ROOT%\Engine\Build\BatchFiles\Build.bat"^
    CarlaUE4^
    Win64^
    %PACKAGE_CONFIG%^
    -WaitMutex^
    -FromMsBuild^
    "%ROOT_PATH%Unreal/CarlaUE4/CarlaUE4.uproject"
if errorlevel 1 goto error_build

rem Package using existing cooked content
echo "%UE4_ROOT%\Engine\Binaries\DotNET\AutomationToolLauncher.exe"^
    BuildCookRun^
    -nocompileeditor^
    -TargetPlatform=Win64^
    -Platform=Win64^
    -installed^
    -nop4^
    -project="%ROOT_PATH%Unreal/CarlaUE4/CarlaUE4.uproject"^
    -skipcook^
    -stage^
    -pak^
    -archive^
    -archivedirectory="!BUILD_FOLDER!"^
    -package^
    -clientconfig=%PACKAGE_CONFIG%
call "%UE4_ROOT%\Engine\Binaries\DotNET\AutomationToolLauncher.exe"^
    BuildCookRun^
    -nocompileeditor^
    -TargetPlatform=Win64^
    -Platform=Win64^
    -installed^
    -nop4^
    -project="%ROOT_PATH%Unreal/CarlaUE4/CarlaUE4.uproject"^
    -skipcook^
    -stage^
    -pak^
    -archive^
    -archivedirectory="!BUILD_FOLDER!"^
    -package^
    -clientconfig=%PACKAGE_CONFIG%
if errorlevel 1 goto error_runUAT

if %DO_TARBALL%==true (
    set SRC_PATH=%SOURCE:/=\%
    if exist "!SRC_PATH!Manifest_NonUFSFiles_Win64.txt" del /Q "!SRC_PATH!Manifest_NonUFSFiles_Win64.txt"
    if exist "!SRC_PATH!Manifest_DebugFiles_Win64.txt" del /Q "!SRC_PATH!Manifest_DebugFiles_Win64.txt"
    if exist "!SRC_PATH!Manifest_UFSFiles_Win64.txt" del /Q "!SRC_PATH!Manifest_UFSFiles_Win64.txt"
    if exist "!SRC_PATH!CarlaUE4/Saved" rmdir /S /Q "!SRC_PATH!CarlaUE4/Saved"
    if exist "!SRC_PATH!Engine/Saved" rmdir /S /Q "!SRC_PATH!Engine/Saved"

    set DST_ZIP=%DESTINATION_ZIP:/=\%
    if exist "%ProgramW6432%/7-Zip/7z.exe" (
        "%ProgramW6432%/7-Zip/7z.exe" a "!DST_ZIP!" "!SRC_PATH!" -tzip -mmt -mx5
    ) else (
        pushd "!SRC_PATH!"
            powershell -command "& { Compress-Archive -Path * -CompressionLevel Fastest -DestinationPath '!DST_ZIP!' }"
        popd
    )
)

goto success

:help
    echo Fast package (reuses existing cooked content).
    echo Usage: %FILE_N% [--config {Debug,Development,Shipping}] [--no-zip]
    goto good_exit

:error_carla_version
    echo.
    echo %FILE_N% [ERROR] Carla Version is not set
    goto bad_exit

:error_unreal_no_found
    echo.
    echo %FILE_N% [ERROR] Unreal Engine not detected
    goto bad_exit

:error_build
    echo.
    echo %FILE_N% [ERROR] There was a problem building CarlaUE4.
    goto bad_exit

:error_runUAT
    echo.
    echo %FILE_N% [ERROR] There was a problem while packaging Unreal project.
    goto bad_exit

:success
    echo.
    echo %FILE_N% Carla project exported to "%BUILD_FOLDER:/=\%"!
    if %DO_TARBALL%==true echo %FILE_N% Compress carla project exported to "%DESTINATION_ZIP%"!
    goto good_exit

:good_exit
    endlocal
    exit /b 0

:bad_exit
    endlocal
    exit /b 1
