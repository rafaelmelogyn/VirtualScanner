#Requires -RunAsAdministrator
$ErrorActionPreference = "Stop"

$ROOT   = $PSScriptRoot
$TWAIN  = Join-Path $ROOT "twain"
$SHARED = Join-Path $ROOT "shared"

$candidates = @(
    "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat",
    "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars32.bat",
    "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars32.bat",
    "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvars32.bat"
)
$vcvars = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (!$vcvars) { throw "Visual Studio com C++ nao encontrado." }
Write-Host "VS: $vcvars"

$clCmd = "cl /LD /MT /EHsc /O2 /DWIN32 /D_WINDOWS /DUNICODE /D_UNICODE " +
         "/I `"$SHARED`" /I `"$TWAIN`" " +
         "VirtualScanner_TWAIN.cpp " +
         "/Fe:TWAINDS_VirtualScanner.dll " +
         "/link /DEF:VirtualScanner_TWAIN.def " +
         "user32.lib gdi32.lib gdiplus.lib"

$bat = Join-Path $env:TEMP "build_twain.bat"
@"
@echo off
call "$vcvars"
cd /d "$TWAIN"
$clCmd
"@ | Set-Content $bat -Encoding ASCII

Write-Host ""
Write-Host "Compilando TWAIN driver (32-bit)..."
cmd /c $bat

if (!(Test-Path "$TWAIN\TWAINDS_VirtualScanner.dll")) {
    Write-Host "ERRO: TWAINDS_VirtualScanner.dll nao gerada."
    exit 1
}

Write-Host ""
Write-Host "Build OK: $TWAIN\TWAINDS_VirtualScanner.dll"
Write-Host ""
Write-Host "Instalando TWAIN driver..."

$twainDest = "C:\Windows\twain_32\VirtualScanner"
New-Item -ItemType Directory -Force -Path $twainDest | Out-Null
Copy-Item "$TWAIN\TWAINDS_VirtualScanner.dll" "$twainDest\TWAINDS_VirtualScanner.ds" -Force
Copy-Item "$TWAIN\TWAINDS_VirtualScanner.dll" "C:\Windows\twain_32\TWAINDS_VirtualScanner.ds" -Force -ErrorAction SilentlyContinue

$twainName = "VirtualScanner ADS-4700W"
$twainPath = "$twainDest\TWAINDS_VirtualScanner.ds"
@(
    "HKLM:\SOFTWARE\WOW6432Node\TWAIN Working Group\TWAIN\$twainName",
    "HKLM:\SOFTWARE\WOW6432Node\TWAIN\$twainName",
    "HKLM:\SOFTWARE\TWAIN Working Group\TWAIN\$twainName",
    "HKLM:\SOFTWARE\TWAIN\$twainName"
) | ForEach-Object {
    New-Item -Path $_ -Force | Out-Null
    Set-ItemProperty -Path $_ -Name "Name" -Value $twainName
    Set-ItemProperty -Path $_ -Name "Path" -Value $twainPath
    Write-Host "  Registrado: $_"
}

Write-Host ""
Write-Host "TWAIN driver instalado."
Write-Host "Abra NAPS2, selecione TWAIN e procure 'VirtualScanner ADS-4700W'"
