@echo off
REM Fix Unreal Engine 4.26 IPAddress.h compatibility issue with Visual Studio 2022

set UE4_IPADDRESS_FILE=C:\UnrealEngine\Engine\Source\Runtime\Sockets\Public\IPAddress.h

echo Fixing IPAddress.h for VS2022 compatibility...

if not exist "%UE4_IPADDRESS_FILE%" (
    echo Error: IPAddress.h not found at %UE4_IPADDRESS_FILE%
    pause
    exit /b 1
)

REM Create backup if it doesn't exist
if not exist "%UE4_IPADDRESS_FILE%.bak" (
    echo Creating backup...
    copy "%UE4_IPADDRESS_FILE%" "%UE4_IPADDRESS_FILE%.bak"
)

REM Use PowerShell to replace FPlatformAtomics with FWindowsPlatformAtomics
powershell -Command "(Get-Content '%UE4_IPADDRESS_FILE%') -replace 'FPlatformAtomics::InterlockedExchange', 'FWindowsPlatformAtomics::InterlockedExchange' | Set-Content '%UE4_IPADDRESS_FILE%'"

echo.
echo IPAddress.h has been patched!
echo Original file backed up to: %UE4_IPADDRESS_FILE%.bak
echo.
pause
