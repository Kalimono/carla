@echo off
REM Launch all three nDisplay nodes on one machine.
REM node_2 is the nDisplay master and CARLA server (Python API connects here on port 2000).
REM node_1 and node_3 are rendering slaves (left and right screens).

set NDISPLAY_CFG=C:\Users\Smarteye\Documents\wall_curved_3x1_large_multi_local_triple.cfg

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

set COMMON_ARGS=-dc_cfg="%NDISPLAY_CFG%" -dc_dev_mono -nosplash -fixedseed -NoVerifyGC -noxrstereo -dx12

start "node_1 (left)"   "%CARLA_EXE%" -dc_node=node_1 %COMMON_ARGS% -log Log=node_1.log
timeout /t 2 /nobreak >nul
start "node_2 (master)" "%CARLA_EXE%" -dc_node=node_2 %COMMON_ARGS% -log Log=node_2.log
timeout /t 2 /nobreak >nul
start "node_3 (right)"  "%CARLA_EXE%" -dc_node=node_3 %COMMON_ARGS% -log Log=node_3.log
