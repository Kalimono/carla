@echo off

set CARLA_EXE=C:\carla\Build\UE4Carla\0.9.15.2-1-g41c8a99bd-dirty\WindowsNoEditor\CarlaUE4.exe
set NDISPLAY_CONFIG=C:\carla\Unreal\CarlaUE4\Config\nDisplay\TripleScreen.ndisplay

start "CARLA nDisplay" "%CARLA_EXE%" ^
-dc_node=node_master ^
-dc_cfg="%NDISPLAY_CONFIG%" ^
-dc_dev_mono ^
-log
