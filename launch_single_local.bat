@echo off
REM Launch a single nDisplay node for testing.
REM Default node is node_2 (master + CARLA server, Python API on port 2000).
REM Usage: launch_single_local.bat [node_id]
REM   e.g. launch_single_local.bat node_2

set NODE_ID=node_2
if not "%1"=="" set NODE_ID=%1

set NDISPLAY_CFG=C:\Users\Smarteye\Documents\wall_curved_3x1_large_multi_single.cfg

REM Auto-detect the packaged build exe (takes the most recently modified version folder)
set CARLA_EXE=
for /d %%D in ("C:\carla\Build\UE4Carla\*") do (
    if exist "%%D\WindowsNoEditor\CarlaUE4.exe" set CARLA_EXE=%%D\WindowsNoEditor\CarlaUE4.exe
)

if "%CARLA_EXE%"=="" (
    echo ERROR: Could not find CarlaUE4.exe under C:\carla\Build\UE4Carla\
    pause
    exit /b 1
)
echo Using: %CARLA_EXE%
echo Launching node: %NODE_ID%

start "%NODE_ID%" "%CARLA_EXE%"^
    -messaging -dc_cluster -nosplash -fixedseed -NoVerifyGC -noxrstereo -RemoteControlIsHeadless^
    -StageFriendlyName=%NODE_ID%^
    -dc_cfg="%NDISPLAY_CFG%"^
    -dx12 -dc_dev_mono^
    -dc_node=%NODE_ID% Log=%NODE_ID%.log^
    -ini:Engine:[/Script/Engine.Engine]:GameEngine=/Script/DisplayCluster.DisplayClusterGameEngine,[/Script/Engine.Engine]:GameViewportClientClassName=/Script/DisplayCluster.DisplayClusterViewportClient^
    -ini:Game:[/Script/EngineSettings.GeneralProjectSettings]:bUseBorderlessWindow=True^
    -ExecCmds="DisableAllScreenMessages"^
    -fps=60 -log -fullscreen^
    -HeroCamX=0 -HeroCamY=0 -HeroCamZ=0
Okay