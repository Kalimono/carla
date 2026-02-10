# Patch Unreal Engine IPAddress.h for MSVC 14.44+ compatibility
$filePath = "C:\UnrealEngine\Engine\Source\Runtime\Sockets\Public\IPAddress.h"

Write-Host "Patching IPAddress.h for MSVC compatibility..."

$content = Get-Content $filePath -Raw

# Backup original
if (-not (Test-Path "$filePath.bak")) {
    Copy-Item $filePath "$filePath.bak"
    Write-Host "Backup created: $filePath.bak"
}

# Fix: Replace FPlatformAtomics::InterlockedExchange with proper cast
$oldPattern = 'FPlatformAtomics::InterlockedExchange\(&bIsValid, NewValue\);'
$newPattern = 'FPlatformAtomics::InterlockedExchange((volatile int32*)&bIsValid, (int32)NewValue);'

if ($content -match [regex]::Escape($oldPattern)) {
    $content = $content -replace [regex]::Escape($oldPattern), $newPattern
    Set-Content -Path $filePath -Value $content -NoNewline
    Write-Host "Successfully patched IPAddress.h"
} else {
    Write-Host "Pattern not found or already patched. Trying alternative fix..."
    
    # Alternative: Look for the line and replace it
    $lines = Get-Content $filePath
    $modified = $false
    for ($i = 0; $i -lt $lines.Length; $i++) {
        if ($lines[$i] -match 'InterlockedExchange.*bIsValid.*NewValue') {
            Write-Host "Found line $($i+1): $($lines[$i])"
            $lines[$i] = $lines[$i] -replace 'FPlatformAtomics::InterlockedExchange\s*\(\s*&bIsValid\s*,\s*NewValue\s*\)', 'FPlatformAtomics::InterlockedExchange((volatile int32*)&bIsValid, (int32)NewValue)'
            $modified = $true
            Write-Host "Modified to: $($lines[$i])"
        }
    }
    
    if ($modified) {
        Set-Content -Path $filePath -Value $lines
        Write-Host "Successfully patched IPAddress.h (alternative method)"
    } else {
        Write-Host "Could not find pattern to patch. Manual intervention may be required."
    }
}
