@echo off
REM Launch CARLA with nDisplay triple-screen configuration

set CARLA_ROOT=%~dp0
set NDISPLAY_CONFIG=%CARLA_ROOT%Unreal\CarlaUE4\Config\nDisplay\TripleScreen.ndisplay
set ENGINE_INI_OVERRIDE=%CARLA_ROOT%Unreal\CarlaUE4\Config\nDisplay\OverrideEngine.ini
set UE4_EDITOR="C:\UnrealEngine\Engine\Binaries\Win64\UE4Editor.exe"
set CARLA_PROJECT=%CARLA_ROOT%Unreal\CarlaUE4\CarlaUE4.uproject

REM Check if nDisplay config exists
if not exist "%NDISPLAY_CONFIG%" (
    echo Error: nDisplay config file not found: %NDISPLAY_CONFIG%
    pause
    exit /b 1
)

REM Delete old log and launch
del "%CARLA_ROOT%Unreal\CarlaUE4\Saved\Logs\CarlaUE4.log" 2>nul

REM Launch with nDisplay
echo Launching CARLA with nDisplay triple-screen setup...
echo Config: %NDISPLAY_CONFIG%
echo.

REM For UE 4.26, nDisplay with root actor in level:
REM -dc_cluster tells it to use nDisplay cluster mode
REM -dc_node specifies which node configuration to use
REM -dc_cfg specifies the config file path (required even with root actor in level)
REM CarlaGameInstance now inherits from DisplayClusterGameInstance (C++ code change required)
start "CARLA nDisplay" %UE4_EDITOR% "%CARLA_PROJECT%" -game -messaging -nohmd -dc_cluster -dc_node=node_master -dc_cfg="%NDISPLAY_CONFIG%" -nosplash -log

REM Wait for log file to be created
timeout /t 3 /nobreak

REM Monitor the log file in real-time
echo.
echo ===== CARLA Runtime Log =====
powershell -Command "Get-Content '%CARLA_ROOT%Unreal\CarlaUE4\Saved\Logs\CarlaUE4.log' -Wait -Tail 50"
