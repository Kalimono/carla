@echo off
REM Launch CARLA with nDisplay triple-screen configuration

set CARLA_ROOT=%~dp0
set NDISPLAY_CONFIG=%CARLA_ROOT%Unreal\CarlaUE4\Config\nDisplay\TripleScreen.ndisplay
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

start "CARLA nDisplay" %UE4_EDITOR% "%CARLA_PROJECT%" -game -messaging -dc_cluster -dc_node=node_master -dc_cfg="%NDISPLAY_CONFIG%" -nosplash -windowed -WinX=0 -WinY=0 -ResX=5760 -ResY=1080 -ForceRes -log

REM Wait for log file to be created
timeout /t 3 /nobreak

REM Monitor the log file in real-time
echo.
echo ===== CARLA Runtime Log =====
powershell -Command "Get-Content '%CARLA_ROOT%Unreal\CarlaUE4\Saved\Logs\CarlaUE4.log' -Wait -Tail 50"
