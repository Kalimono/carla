@echo off
echo Fixing UE4 IPAddress.h for VS2022 compatibility...

set IPADDRESS_FILE=C:\UnrealEngine\Engine\Source\Runtime\Sockets\Public\IPAddress.h

if not exist "%IPADDRESS_FILE%" (
    echo ERROR: IPAddress.h not found at %IPADDRESS_FILE%
    exit /b 1
)

echo Creating backup...
copy "%IPADDRESS_FILE%" "%IPADDRESS_FILE%.backup_v2"

echo Patching file...
powershell -Command "(Get-Content '%IPADDRESS_FILE%') -replace 'FPlatformAtomics::InterlockedExchange\(\(volatile int32\*\)&Parent->bShouldAbandon,\(int32\)true\)', '_InterlockedExchange((volatile LONG*)&Parent->bShouldAbandon, (LONG)true)' | Set-Content '%IPADDRESS_FILE%'"

powershell -Command "(Get-Content '%IPADDRESS_FILE%') -replace 'FWindowsPlatformAtomics::InterlockedExchange\(\(volatile int32\*\)&Parent->bShouldAbandon,\(int32\)true\)', '_InterlockedExchange((volatile LONG*)&Parent->bShouldAbandon, (LONG)true)' | Set-Content '%IPADDRESS_FILE%'"

echo.
echo Done! IPAddress.h has been patched with direct Windows API call.
echo Backup saved as: %IPADDRESS_FILE%.backup_v2
echo.
pause
