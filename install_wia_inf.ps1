#Requires -RunAsAdministrator
$ErrorActionPreference = "Stop"

$ROOT  = $PSScriptRoot
$WIA   = Join-Path $ROOT "wia"
$DLL   = Join-Path $WIA "VirtualScannerWIA.dll"
$INF   = Join-Path $WIA "VirtualScannerWIA.inf"
$CLSID = "{A1B2C3D4-E5F6-7890-ABCD-EF1234567890}"
$classGUID = "{6bdd1fc6-810f-11d0-bec7-08002be2092f}"

Write-Host ""
Write-Host "======================================"
Write-Host "  VirtualScanner WIA INSTALL (INF)"
Write-Host "======================================"

if (!(Test-Path $DLL)) { throw "DLL nao encontrada. Rode .\build_wia_only.ps1 primeiro." }
if (!(Test-Path $INF)) { throw "INF nao encontrado: $INF" }

# Stop WIA
Write-Host "[1] Parando WIA..."
Stop-Service stisvc -Force -ErrorAction SilentlyContinue
Start-Sleep 2

# Copy DLL to System32
Write-Host "[2] Copiando DLL para System32..."
$DST = "C:\Windows\System32\VirtualScannerWIA.dll"
Copy-Item $DLL $DST -Force
Write-Host "    OK: $DST"

# Register COM manually (pnputil INF doesn't handle HKCR on unsigned drivers)
Write-Host "[3] Registrando COM CLSID..."
foreach ($base in @("HKLM:\SOFTWARE\Classes", "HKLM:\SOFTWARE\WOW6432Node\Classes")) {
    New-Item "$base\CLSID\$CLSID"                     -Force | Out-Null
    New-Item "$base\CLSID\$CLSID\InprocServer32"      -Force | Out-Null
    Set-ItemProperty "$base\CLSID\$CLSID"                   "(Default)"      "VirtualScanner WIA Driver"
    Set-ItemProperty "$base\CLSID\$CLSID\InprocServer32"    "(Default)"      $DST
    Set-ItemProperty "$base\CLSID\$CLSID\InprocServer32"    "ThreadingModel" "Both"
}
Write-Host "    OK"

# Write the WIA device registry structure that stisvc actually reads
# stisvc enumerates from: HKLM\SYSTEM\CCS\Control\Class\{6bdd1fc6...}
# Each subkey = one device. Key name must be 4-digit number.
# stisvc looks for USDClass value to find the COM server.
Write-Host "[4] Escrevendo estrutura de dispositivo WIA..."

$classBase = "HKLM:\SYSTEM\CurrentControlSet\Control\Class\$classGUID"
New-Item $classBase -Force -ErrorAction SilentlyContinue | Out-Null

# Remove existing VirtualScanner entries to avoid duplicates
Get-ChildItem $classBase -ErrorAction SilentlyContinue | ForEach-Object {
    try {
        $usd = (Get-ItemProperty $_.PSPath -Name "USDClass" -ErrorAction SilentlyContinue).USDClass
        if ($usd -eq $CLSID) {
            Write-Host "    Removendo entrada antiga: $($_.PSChildName)"
            Remove-Item $_.PSPath -Recurse -Force
        }
    } catch {}
}

# Find next free index
$nextIdx = 0
$existing = Get-ChildItem $classBase -ErrorAction SilentlyContinue |
            Where-Object { $_.PSChildName -match '^\d{4}$' } |
            ForEach-Object { [int]$_.PSChildName } | Sort-Object -Descending
if ($existing) { $nextIdx = $existing[0] + 1 }
$idxStr   = "{0:D4}" -f $nextIdx
$classKey = "$classBase\$idxStr"

Write-Host "    Criando Class key: $idxStr"
New-Item $classKey              -Force | Out-Null
New-Item "$classKey\DeviceData" -Force | Out-Null

Set-ItemProperty $classKey "ClassGUID"      $classGUID
Set-ItemProperty $classKey "Class"          "Image"
Set-ItemProperty $classKey "DriverDesc"     "VirtualScanner ADS-4700W"
Set-ItemProperty $classKey "Manufacturer"   "VirtualScanner"
Set-ItemProperty $classKey "USDClass"       $CLSID
Set-ItemProperty $classKey "HardwareConfig" -Value 1   -Type DWord
Set-ItemProperty $classKey "DeviceType"     -Value 1   -Type DWord
Set-ItemProperty $classKey "DeviceSubType"  -Value 0   -Type DWord
Set-ItemProperty $classKey "Capabilities"   -Value 0   -Type DWord
Set-ItemProperty $classKey "WiaVersion"     "2.0"
Set-ItemProperty $classKey "SubClass"       "StillImage"
Set-ItemProperty $classKey "Server"         "local"

Set-ItemProperty "$classKey\DeviceData" "Server"        "local"
Set-ItemProperty "$classKey\DeviceData" "TwainDS"       ""
Set-ItemProperty "$classKey\DeviceData" "HardwareConfig" -Value 1 -Type DWord
Set-ItemProperty "$classKey\DeviceData" "DeviceType"     -Value 1 -Type DWord
Set-ItemProperty "$classKey\DeviceData" "USDClass"       $CLSID

Write-Host "[5] Criando entrada ROOT\IMAGE para PnP..."
$enumBase = "HKLM:\SYSTEM\CurrentControlSet\Enum\ROOT\IMAGE"
New-Item $enumBase -Force -ErrorAction SilentlyContinue | Out-Null

# Remove old VirtualScanner PnP entries
Get-ChildItem $enumBase -ErrorAction SilentlyContinue | ForEach-Object {
    try {
        $hw = (Get-ItemProperty $_.PSPath -Name "HardwareID" -ErrorAction SilentlyContinue).HardwareID
        if ($hw -like "*VirtualScanner*") {
            Write-Host "    Removendo entrada PnP antiga: $($_.PSChildName)"
            Remove-Item $_.PSPath -Recurse -Force -ErrorAction SilentlyContinue
        }
    } catch {}
}

$nextDev = 0
$exDev = Get-ChildItem $enumBase -ErrorAction SilentlyContinue |
         Where-Object { $_.PSChildName -match '^\d{4}$' } |
         ForEach-Object { [int]$_.PSChildName } | Sort-Object -Descending
if ($exDev) { $nextDev = $exDev[0] + 1 }
$devStr  = "{0:D4}" -f $nextDev
$enumKey = "$enumBase\$devStr"

New-Item $enumKey                         -Force | Out-Null
New-Item "$enumKey\Device Parameters"     -Force | Out-Null
New-Item "$enumKey\DeviceData"            -Force | Out-Null

Set-ItemProperty $enumKey "ClassGUID"     $classGUID
Set-ItemProperty $enumKey "Class"         "Image"
Set-ItemProperty $enumKey "DeviceDesc"    "VirtualScanner ADS-4700W"
Set-ItemProperty $enumKey "Manufacturer"  "VirtualScanner"
Set-ItemProperty $enumKey "HardwareID"    -Type MultiString -Value @("ROOT\IMAGE\VirtualScannerWIA")
Set-ItemProperty $enumKey "Service"       "stisvc"
Set-ItemProperty $enumKey "ConfigFlags"   -Value 0 -Type DWord
Set-ItemProperty $enumKey "Capabilities"  -Value 0 -Type DWord
Set-ItemProperty $enumKey "Driver"        "$classGUID\$idxStr"

# DeviceData under Enum - stisvc reads USDClass from here first
Set-ItemProperty "$enumKey\DeviceData" "USDClass"       $CLSID
Set-ItemProperty "$enumKey\DeviceData" "Server"         "local"
Set-ItemProperty "$enumKey\DeviceData" "HardwareConfig" -Value 1 -Type DWord
Set-ItemProperty "$enumKey\DeviceData" "DeviceType"     -Value 1 -Type DWord
Set-ItemProperty "$enumKey\DeviceData" "WiaVersion"     "2.0"
Set-ItemProperty "$enumKey\DeviceData" "SubClass"       "StillImage"

Write-Host "    OK: ROOT\IMAGE\$devStr -> Class $idxStr"

# Also write to StillImage\Devices with instance hierarchy expected by STI/WIA
Write-Host "[6] Escrevendo StillImage\Devices..."
$siBase = "HKLM:\SYSTEM\CurrentControlSet\Control\StillImage\Devices"
New-Item $siBase -Force -ErrorAction SilentlyContinue | Out-Null
$siRoot = "$siBase\ROOT"
$siImage = "$siRoot\IMAGE"
$siKey  = "$siImage\$devStr"
New-Item $siRoot  -Force -ErrorAction SilentlyContinue | Out-Null
New-Item $siImage -Force -ErrorAction SilentlyContinue | Out-Null
Remove-Item $siKey -Recurse -Force -ErrorAction SilentlyContinue
New-Item $siKey              -Force | Out-Null
New-Item "$siKey\DeviceData" -Force | Out-Null
Set-ItemProperty $siKey "ClassGUID"      $classGUID
Set-ItemProperty $siKey "DriverDesc"     "VirtualScanner ADS-4700W"
Set-ItemProperty $siKey "Manufacturer"   "VirtualScanner"
Set-ItemProperty $siKey "HardwareID"     -Type MultiString -Value @("ROOT\IMAGE\VirtualScannerWIA")
Set-ItemProperty $siKey "Service"        "stisvc"
Set-ItemProperty $siKey "USDClass"       $CLSID
Set-ItemProperty $siKey "HardwareConfig" -Value 1 -Type DWord
Set-ItemProperty $siKey "DeviceType"     -Value 1 -Type DWord
Set-ItemProperty $siKey "Server"         "local"
Set-ItemProperty $siKey "SubClass"       "StillImage"
Set-ItemProperty $siKey "WiaVersion"     "2.0"
Set-ItemProperty "$siKey\DeviceData" "Server"         "local"
Set-ItemProperty "$siKey\DeviceData" "USDClass"       $CLSID
Set-ItemProperty "$siKey\DeviceData" "HardwareConfig" -Value 1 -Type DWord
Set-ItemProperty "$siKey\DeviceData" "DeviceType"     -Value 1 -Type DWord
Set-ItemProperty "$siKey\DeviceData" "SubClass"       "StillImage"
Set-ItemProperty "$siKey\DeviceData" "WiaVersion"     "2.0"
Write-Host "    StillImage key: ROOT\IMAGE\$devStr"

# Restart WIA
Write-Host "[7] Reiniciando WIA..."
Stop-Service stisvc -Force -ErrorAction SilentlyContinue
Start-Sleep 3
Start-Service stisvc
$deadline = (Get-Date).AddSeconds(30)
while ((Get-Service stisvc).Status -ne "Running" -and (Get-Date) -lt $deadline) {
    Start-Sleep 1
}
Write-Host "    WIA: $((Get-Service stisvc).Status)"
Start-Sleep 2

# Enumerate
Write-Host "[8] Dispositivos WIA encontrados:"
try {
    $wia = New-Object -ComObject WIA.DeviceManager
    $n   = $wia.DeviceInfos.Count
    Write-Host "    Total: $n"
    for ($i = 1; $i -le $n; $i++) {
        $di   = $wia.DeviceInfos.Item($i)
        $name = try { $di.Properties("Name").Value } catch { "(sem nome)" }
        $id   = try { $di.DeviceID } catch { "?" }
        Write-Host "      [$i] $name  (ID: $id)"
    }
    if ($n -le 1) {
        Write-Host ""
        Write-Host "  VirtualScanner nao apareceu ainda."
        Write-Host "  Verificando log..."
        $log = "C:\VirtualScanner\Logs\vscanner.log"
        if (Test-Path $log) {
            Write-Host "--- Log (ultimas linhas) ---"
            Get-Content $log -Tail 20
        } else {
            Write-Host "  Log nao existe - DLL ainda nao foi carregada pelo stisvc"
            Write-Host ""
            Write-Host "  Diagnostico: verificando se DLL foi copiada..."
            Get-Item $DST -ErrorAction SilentlyContinue | Select-Object Name, Length, LastWriteTime
            Write-Host ""
            Write-Host "  Verificando registro Class key..."
            Get-ItemProperty "HKLM:\SYSTEM\CurrentControlSet\Control\Class\$classGUID\$idxStr" |
                Select-Object USDClass, WiaVersion, SubClass, DriverDesc
        }
    }
} catch {
    Write-Host "    Erro: $($_.Exception.Message)"
}

Write-Host ""
Write-Host "=============================="
Write-Host "  Concluido!"
Write-Host "=============================="
Write-Host ""
Write-Host "Para testar: coloque uma imagem em C:\VirtualScanner\Queue\"
Write-Host "Abra: wiaacmgr  ou  mspaint (Arquivo > Da camera ou scanner)"
