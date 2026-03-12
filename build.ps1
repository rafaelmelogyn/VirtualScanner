#Requires -RunAsAdministrator
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$ROOT   = Split-Path -Parent $MyInvocation.MyCommand.Path
$SHARED = "$ROOT\shared"
$WIA    = "$ROOT\wia"
$TWAIN  = "$ROOT\twain"

$VS2022 = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build"
$VS2019 = "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build"

if      (Test-Path "$VS2022\vcvars64.bat") { $VSBASE = $VS2022 }
elseif  (Test-Path "$VS2019\vcvars64.bat") { $VSBASE = $VS2019 }
else    { throw "Visual Studio 2019 ou 2022 nao encontrado!" }

Write-Host ""
Write-Host "=== VirtualScanner Build Script ===" -ForegroundColor Cyan
Write-Host "Visual Studio: $VSBASE"
Write-Host ""

function Invoke-VCCmd($vcvars, $dir, $cmd) {
    $full = "`"$vcvars`" && cd /d `"$dir`" && $cmd"
    Write-Host "CMD: $cmd"
    $out = cmd /c $full 2>&1
    $out | ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE -ne 0) { throw "Compilacao falhou com codigo $LASTEXITCODE" }
}

# --- Criar diretorios de runtime ---
@("C:\VirtualScanner","C:\VirtualScanner\Queue","C:\VirtualScanner\Logs") | ForEach-Object {
    New-Item -ItemType Directory -Force -Path $_ | Out-Null
}

# =========================================================
# PARTE 1: WIA driver 64-bit
# =========================================================
Write-Host "--- Compilando WIA driver (64-bit) ---" -ForegroundColor Yellow

$wiaCmd = 'cl /LD /MT /EHsc /O2 /DWIN32 /D_WINDOWS /DUNICODE /D_UNICODE' `
    + " /I `"$SHARED`" /I `"$WIA`"" `
    + ' WiaDriver.cpp' `
    + ' /Fe:VirtualScannerWIA.dll' `
    + ' /link /DEF:VirtualScannerWIA.def' `
    + ' ole32.lib oleaut32.lib user32.lib gdi32.lib wiaguid.lib shlwapi.lib gdiplus.lib'

Invoke-VCCmd "$VSBASE\vcvars64.bat" $WIA $wiaCmd

if (-not (Test-Path "$WIA\VirtualScannerWIA.dll")) {
    throw "ERRO: VirtualScannerWIA.dll nao gerada!"
}
Write-Host "WIA DLL OK: $WIA\VirtualScannerWIA.dll" -ForegroundColor Green

# =========================================================
# PARTE 2: TWAIN driver 32-bit
# =========================================================
Write-Host ""
Write-Host "--- Compilando TWAIN driver (32-bit) ---" -ForegroundColor Yellow

$twainCmd = 'cl /LD /MT /EHsc /O2 /DWIN32 /D_WINDOWS /DUNICODE /D_UNICODE' `
    + " /I `"$SHARED`" /I `"$TWAIN`"" `
    + ' VirtualScanner_TWAIN.cpp' `
    + ' /Fe:TWAINDS_VirtualScanner.dll' `
    + ' /link /DEF:VirtualScanner_TWAIN.def' `
    + ' user32.lib gdi32.lib gdiplus.lib'

Invoke-VCCmd "$VSBASE\vcvars32.bat" $TWAIN $twainCmd

if (-not (Test-Path "$TWAIN\TWAINDS_VirtualScanner.dll")) {
    throw "ERRO: TWAINDS_VirtualScanner.dll nao gerada!"
}
Write-Host "TWAIN DLL OK: $TWAIN\TWAINDS_VirtualScanner.dll" -ForegroundColor Green

# =========================================================
# PARTE 3: Instalar WIA driver
# =========================================================
Write-Host ""
Write-Host "--- Instalando WIA driver ---" -ForegroundColor Yellow

Stop-Service -Name stisvc -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2

$wiaSystem = "C:\Windows\System32\VirtualScannerWIA.dll"
Copy-Item "$WIA\VirtualScannerWIA.dll" $wiaSystem -Force
Write-Host "  Copiado: $wiaSystem"

$clsid   = "{A1B2C3D4-E5F6-7890-ABCD-EF1234567890}"
$comBase = "HKLM:\SOFTWARE\Classes\CLSID\$clsid"
New-Item -Path $comBase -Force | Out-Null
Set-ItemProperty -Path $comBase -Name "(Default)" -Value "VirtualScanner WIA Driver"
New-Item -Path "$comBase\InprocServer32" -Force | Out-Null
Set-ItemProperty -Path "$comBase\InprocServer32" -Name "(Default)"     -Value $wiaSystem
Set-ItemProperty -Path "$comBase\InprocServer32" -Name "ThreadingModel" -Value "Both"
Write-Host "  COM CLSID registrado: $clsid"

$deviceKey = "HKLM:\SYSTEM\CurrentControlSet\Control\StillImage\Devices\VSCANNER_VirtualScannerWIA"
New-Item -Path $deviceKey -Force | Out-Null
Set-ItemProperty -Path $deviceKey -Name "DeviceType"    -Value 1 -Type DWord
Set-ItemProperty -Path $deviceKey -Name "DeviceSubType" -Value 0 -Type DWord
Set-ItemProperty -Path $deviceKey -Name "Manufacturer"  -Value "VirtualScanner"
Set-ItemProperty -Path $deviceKey -Name "Description"   -Value "VirtualScanner ADS-4700W"
Set-ItemProperty -Path $deviceKey -Name "USDClass"      -Value $clsid
Set-ItemProperty -Path $deviceKey -Name "HardwareConfig" -Value 1 -Type DWord
Set-ItemProperty -Path $deviceKey -Name "Server"        -Value "local"
Set-ItemProperty -Path $deviceKey -Name "UI DLL"        -Value ""
New-Item -Path "$deviceKey\DeviceData" -Force | Out-Null
Set-ItemProperty -Path "$deviceKey\DeviceData" -Name "Server" -Value "local"
Write-Host "  STI device key registrado"

$wiaClassPath = "HKLM:\SYSTEM\CurrentControlSet\Control\Class\{6bdd1fc6-810f-11d0-bec7-08002be2092f}"
$nextIdx = 0
if (Test-Path $wiaClassPath) {
    $nums = Get-ChildItem $wiaClassPath -ErrorAction SilentlyContinue |
            Where-Object { $_.PSChildName -match '^\d{4}$' } |
            ForEach-Object { [int]$_.PSChildName } |
            Sort-Object -Descending
    if ($nums) { $nextIdx = $nums[0] + 1 }
}
$idxStr      = "{0:D4}" -f $nextIdx
$classDevKey = "$wiaClassPath\$idxStr"
New-Item -Path $classDevKey -Force | Out-Null
Set-ItemProperty -Path $classDevKey -Name "ClassGUID"  -Value "{6bdd1fc6-810f-11d0-bec7-08002be2092f}"
Set-ItemProperty -Path $classDevKey -Name "DriverDesc" -Value "VirtualScanner ADS-4700W"
Set-ItemProperty -Path $classDevKey -Name "USDClass"   -Value $clsid
Write-Host "  Classe Image registrada (indice $idxStr)"

# =========================================================
# PARTE 4: Instalar TWAIN driver
# =========================================================
Write-Host ""
Write-Host "--- Instalando TWAIN driver ---" -ForegroundColor Yellow

$twainDest = "C:\Windows\twain_32\VirtualScanner"
New-Item -ItemType Directory -Force -Path $twainDest | Out-Null
Copy-Item "$TWAIN\TWAINDS_VirtualScanner.dll" "$twainDest\TWAINDS_VirtualScanner.ds" -Force
Copy-Item "$TWAIN\TWAINDS_VirtualScanner.dll" "C:\Windows\twain_32\TWAINDS_VirtualScanner.ds" -Force -ErrorAction SilentlyContinue
Write-Host "  TWAIN DS copiado: $twainDest"

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

# =========================================================
# PARTE 5: Reiniciar WIA
# =========================================================
Write-Host ""
Write-Host "--- Reiniciando servico WIA ---" -ForegroundColor Yellow
Start-Service -Name stisvc -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2
$svc = Get-Service -Name stisvc -ErrorAction SilentlyContinue
if ($svc) { Write-Host "  WIA Service: $($svc.Status)" }

Write-Host ""
Write-Host "============================================" -ForegroundColor Green
Write-Host "  VirtualScanner instalado com sucesso!"     -ForegroundColor Green
Write-Host "============================================" -ForegroundColor Green
Write-Host "  Coloque imagens em: C:\VirtualScanner\Queue\" -ForegroundColor Green
Write-Host "  WIA : Windows Fax and Scan / Paint"          -ForegroundColor Green
Write-Host "  TWAIN: NAPS2 / cartorios / IrfanView"        -ForegroundColor Green
Write-Host "  Log : C:\VirtualScanner\Logs\vscanner.log"   -ForegroundColor Green
Write-Host "============================================" -ForegroundColor Green
