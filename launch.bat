call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
set GENERATOR=Visual Studio 17 2022
set ROOT_PATH=c:\carla\
set INSTALLATION_DIR=c:\carla\Build\
set UE4_ROOT=C:\UnrealEngine\
cd /d c:\carla
make launch
