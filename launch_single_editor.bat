@echo off
REM Launch nDisplay single node using the UE4 Editor (no packaging needed).
REM Usage: launch_single_editor.bat [node_id]

set NODE_ID=node_2
if not "%1"=="" set NODE_ID=%1

set NDISPLAY_CFG=C:\Users\Smarteye\Documents\wall_curved_3x1_large_multi_single.cfg
set UE4_EDITOR=C:\UnrealEngine\Engine\Binaries\Win64\UE4Editor.exe
set UPROJECT=C:\carla\Unreal\CarlaUE4\CarlaUE4.uproject

echo Using editor: %UE4_EDITOR%
echo Launching node: %NODE_ID%

start "%NODE_ID%" "%UE4_EDITOR%" "%UPROJECT%" -game^
    -messaging -dc_cluster -nosplash -fixedseed -NoVerifyGC -noxrstereo -RemoteControlIsHeadless^
    -StageFriendlyName=%NODE_ID%^
    -dc_cfg="%NDISPLAY_CFG%"^
    -dx12 -dc_dev_mono^
    -dc_node=%NODE_ID% Log=%NODE_ID%.log^
    -ini:Engine:[/Script/Engine.Engine]:GameEngine=/Script/DisplayCluster.DisplayClusterGameEngine,[/Script/Engine.Engine]:GameViewportClientClassName=/Script/DisplayCluster.DisplayClusterViewportClient^
    -ini:Game:[/Script/EngineSettings.GeneralProjectSettings]:bUseBorderlessWindow=True^
    -ExecCmds="DisableAllScreenMessages"^
    -fps=60 -log -fullscreen^
    -HeroCamX=-800 -HeroCamY=0 -HeroCamZ=50
