# PowerShell script to patch old CMake version requirements in downloaded dependencies
# This fixes compatibility issues with CMake 4.x which requires minimum version 3.5

param(
    [string]$BuildDir = "C:\carla\Build"
)

Write-Host "Patching CMake version requirements in $BuildDir..." -ForegroundColor Cyan

$patchedCount = 0
$errorCount = 0

# Find all CMakeLists.txt files in the Build directory
$cmakeFiles = Get-ChildItem -Path $BuildDir -Filter "CMakeLists.txt" -Recurse -ErrorAction SilentlyContinue

foreach ($file in $cmakeFiles) {
    try {
        $content = Get-Content $file.FullName -Raw -ErrorAction Stop
        $originalContent = $content
        
        # Pattern 1: cmake_minimum_required(VERSION x.y.z) where x.y.z < 3.5
        $pattern1 = 'cmake_minimum_required\s*\(\s*VERSION\s+([0-2]\.\d+(?:\.\d+)?)\s*\)'
        if ($content -match $pattern1) {
            $oldVersion = $matches[1]
            $content = $content -replace $pattern1, 'cmake_minimum_required(VERSION 3.5)'
            Write-Host "  [PATCH] $($file.FullName)" -ForegroundColor Green
            Write-Host "    Changed: VERSION $oldVersion -> VERSION 3.5" -ForegroundColor Yellow
            $patchedCount++
        }
        
        # Pattern 2: cmake_minimum_required(VERSION 3.x) where x < 5
        $pattern2 = 'cmake_minimum_required\s*\(\s*VERSION\s+3\.([0-4])(?:\.\d+)?\s*\)'
        if ($content -match $pattern2) {
            $oldMinor = $matches[1]
            $content = $content -replace $pattern2, 'cmake_minimum_required(VERSION 3.5)'
            Write-Host "  [PATCH] $($file.FullName)" -ForegroundColor Green
            Write-Host "    Changed: VERSION 3.$oldMinor -> VERSION 3.5" -ForegroundColor Yellow
            $patchedCount++
        }
        
        # Only write if content changed
        if ($content -ne $originalContent) {
            Set-Content -Path $file.FullName -Value $content -NoNewline -ErrorAction Stop
        }
    }
    catch {
        Write-Host "  [ERROR] Failed to patch $($file.FullName): $_" -ForegroundColor Red
        $errorCount++
    }
}

Write-Host "`nPatch Summary:" -ForegroundColor Cyan
Write-Host "  Files patched: $patchedCount" -ForegroundColor Green
Write-Host "  Errors: $errorCount" -ForegroundColor $(if ($errorCount -gt 0) { "Red" } else { "Green" })

if ($patchedCount -eq 0 -and $errorCount -eq 0) {
    Write-Host "  No files needed patching." -ForegroundColor Gray
}

exit $(if ($errorCount -gt 0) { 1 } else { 0 })
