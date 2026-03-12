#Requires -RunAsAdministrator
$ErrorActionPreference = "Stop"

$ROOT   = $PSScriptRoot
$WIA    = Join-Path $ROOT "wia"
$SHARED = Join-Path $ROOT "shared"

$candidates = @(
    "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat",
    "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat",
    "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat",
    "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat",
    "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvars64.bat"
)
$vcvars = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (!$vcvars) { throw "Visual Studio com C++ nao encontrado." }
Write-Host "VS: $vcvars"

# Find WIA libs (x64)
function Find-Lib($name) {
    $result = Get-ChildItem "C:\Program Files (x86)\Windows Kits" -Recurse -Filter $name `
              -ErrorAction SilentlyContinue |
              Where-Object { $_.FullName -like "*\x64\*" } |
              Select-Object -First 1
    if (!$result) {
        $result = Get-ChildItem "C:\Program Files\Windows Kits" -Recurse -Filter $name `
                  -ErrorAction SilentlyContinue |
                  Where-Object { $_.FullName -like "*\x64\*" } |
                  Select-Object -First 1
    }
    return $result
}

$wiaguid  = Find-Lib "WiaGuid.Lib"
$wiaservc = Find-Lib "wiaservc.lib"

if (!$wiaguid)  { throw "WiaGuid.Lib not found in Windows Kits x64" }
if (!$wiaservc) { throw "wiaservc.lib not found in Windows Kits x64" }

Write-Host "WiaGuid.Lib : $($wiaguid.FullName)"
Write-Host "wiaservc.lib: $($wiaservc.FullName)"

# Both libs are typically in the same directory
$libDir = $wiaservc.DirectoryName
$libPath = " /LIBPATH:`"$libDir`""

$clCmd = "cl /LD /MT /EHsc /O2 /DWIN32 /D_WINDOWS /DUNICODE /D_UNICODE" +
         " /I `"$SHARED`" /I `"$WIA`"" +
         " WiaDriver.cpp" +
         " /Fe:VirtualScannerWIA.dll" +
         " /link /DEF:VirtualScannerWIA.def$libPath" +
         " ole32.lib oleaut32.lib user32.lib gdi32.lib gdiplus.lib shlwapi.lib" +
         " WiaGuid.Lib wiaservc.lib"

$bat = Join-Path $env:TEMP "build_wia.bat"
@"
@echo off
call "$vcvars"
cd /d "$WIA"
$clCmd
"@ | Set-Content $bat -Encoding ASCII

Write-Host ""
Write-Host "Compilando WIA driver (64-bit)..."
cmd /c $bat

if (!(Test-Path "$WIA\VirtualScannerWIA.dll")) {
    Write-Host "ERRO: build falhou."
    exit 1
}

$size = (Get-Item "$WIA\VirtualScannerWIA.dll").Length
Write-Host ""
Write-Host "Build OK: $WIA\VirtualScannerWIA.dll ($size bytes)"
Write-Host ""
Write-Host "Proximo passo: .\install_wia.ps1"
