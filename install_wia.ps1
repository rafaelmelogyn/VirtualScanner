#Requires -RunAsAdministrator
$ErrorActionPreference = "Stop"

$ROOT  = $PSScriptRoot
$WIA   = Join-Path $ROOT "wia"
$DLL   = Join-Path $WIA "VirtualScannerWIA.dll"
$DST   = "C:\Windows\System32\VirtualScannerWIA.dll"
$CLSID = "{A1B2C3D4-E5F6-7890-ABCD-EF1234567890}"
$classGUID = "{6bdd1fc6-810f-11d0-bec7-08002be2092f}"

Write-Host ""
Write-Host "=============================="
Write-Host "  VirtualScanner WIA INSTALL"
Write-Host "=============================="

if (!(Test-Path $DLL)) { throw "DLL nao encontrada. Rode .\build_wia_only.ps1 primeiro." }

# Stop WIA
Write-Host "[1] Parando WIA..."
Stop-Service stisvc -Force -ErrorAction SilentlyContinue
Start-Sleep 2

# Copy DLL
Write-Host "[2] Copiando DLL..."
Copy-Item $DLL $DST -Force
Write-Host "    OK: $DST"

# Register COM (64-bit)
Write-Host "[3] Registrando COM..."
$b = "HKLM:\SOFTWARE\Classes\CLSID\$CLSID"
New-Item "$b"                     -Force | Out-Null
New-Item "$b\InprocServer32"      -Force | Out-Null
Set-ItemProperty $b                    "(Default)"       "VirtualScanner WIA Driver"
Set-ItemProperty "$b\InprocServer32"   "(Default)"       $DST
Set-ItemProperty "$b\InprocServer32"   "ThreadingModel"  "Both"
# 32-bit view
$b32 = "HKLM:\SOFTWARE\WOW6432Node\Classes\CLSID\$CLSID"
New-Item "$b32"                   -Force | Out-Null
New-Item "$b32\InprocServer32"    -Force | Out-Null
Set-ItemProperty $b32                  "(Default)"       "VirtualScanner WIA Driver"
Set-ItemProperty "$b32\InprocServer32" "(Default)"       $DST
Set-ItemProperty "$b32\InprocServer32" "ThreadingModel"  "Both"
Write-Host "    OK"

# -----------------------------------------------------------------------
# The key step: register as a WIA device using SetupDi / INF approach
# We create a "Software Device" that the WIA service will pick up.
# Use devcon-style root enumeration via CfgMgr32.
# -----------------------------------------------------------------------
Write-Host "[4] Registrando dispositivo via SetupAPI (CM_Add_Empty_LogConf)..."

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class SetupDiHelper {
    // SetupDiCreateDeviceInfoList
    [DllImport("setupapi.dll", SetLastError=true)]
    public static extern IntPtr SetupDiCreateDeviceInfoList(ref Guid ClassGuid, IntPtr hwndParent);

    // SetupDiCreateDeviceInfo
    [DllImport("setupapi.dll", SetLastError=true, CharSet=CharSet.Auto)]
    public static extern bool SetupDiCreateDeviceInfo(
        IntPtr DeviceInfoSet, string DeviceName, ref Guid ClassGuid,
        string DeviceDescription, IntPtr hwndParent, uint CreationFlags,
        ref SP_DEVINFO_DATA DeviceInfoData);

    // SetupDiSetDeviceRegistryProperty
    [DllImport("setupapi.dll", SetLastError=true, CharSet=CharSet.Auto)]
    public static extern bool SetupDiSetDeviceRegistryProperty(
        IntPtr DeviceInfoSet, ref SP_DEVINFO_DATA DeviceInfoData,
        uint Property, byte[] PropertyBuffer, uint PropertyBufferSize);

    // SetupDiCallClassInstaller
    [DllImport("setupapi.dll", SetLastError=true)]
    public static extern bool SetupDiCallClassInstaller(
        uint InstallFunction, IntPtr DeviceInfoSet, ref SP_DEVINFO_DATA DeviceInfoData);

    // SetupDiDestroyDeviceInfoList
    [DllImport("setupapi.dll", SetLastError=true)]
    public static extern bool SetupDiDestroyDeviceInfoList(IntPtr DeviceInfoSet);

    // SetupDiOpenDevRegKey
    [DllImport("setupapi.dll", SetLastError=true)]
    public static extern IntPtr SetupDiOpenDevRegKey(
        IntPtr DeviceInfoSet, ref SP_DEVINFO_DATA DeviceInfoData,
        uint Scope, uint HwProfile, uint KeyType, uint samDesired);

    public const uint DICD_GENERATE_ID   = 0x00000001;
    public const uint DIF_REGISTERDEVICE = 0x00000019;
    public const uint DIF_REMOVE         = 0x00000005;
    public const uint SPDRP_HARDWAREID   = 0x00000001;
    public const uint SPDRP_DEVICEDESC   = 0x00000000;
    public const uint SPDRP_MFG          = 0x0000000B;
    public const uint DIREG_DEV          = 0x00000001;
    public const uint KEY_ALL_ACCESS     = 0xF003F;
    public static readonly IntPtr INVALID_HANDLE = new IntPtr(-1);
}

[StructLayout(LayoutKind.Sequential)]
public struct SP_DEVINFO_DATA {
    public uint  cbSize;
    public Guid  ClassGuid;
    public uint  DevInst;
    public IntPtr Reserved;
}
'@ -ErrorAction Stop

$classGuidObj = [Guid]$classGUID
$devInfo = New-Object SP_DEVINFO_DATA
$devInfo.cbSize = [System.Runtime.InteropServices.Marshal]::SizeOf($devInfo)

$hDevInfo = [SetupDiHelper]::SetupDiCreateDeviceInfoList([ref]$classGuidObj, [IntPtr]::Zero)
if ($hDevInfo -eq [SetupDiHelper]::INVALID_HANDLE) {
    throw "SetupDiCreateDeviceInfoList failed: $([System.Runtime.InteropServices.Marshal]::GetLastWin32Error())"
}

$ok = [SetupDiHelper]::SetupDiCreateDeviceInfo(
    $hDevInfo, "VirtualScannerWIA", [ref]$classGuidObj,
    "VirtualScanner ADS-4700W", [IntPtr]::Zero,
    [SetupDiHelper]::DICD_GENERATE_ID, [ref]$devInfo)

if (!$ok) {
    $err = [System.Runtime.InteropServices.Marshal]::GetLastWin32Error()
    [SetupDiHelper]::SetupDiDestroyDeviceInfoList($hDevInfo) | Out-Null
    Write-Host "    SetupDiCreateDeviceInfo err=$err (device may already exist)"
} else {
    # Set HardwareID
    $hwid = [System.Text.Encoding]::Unicode.GetBytes("ROOT\IMAGE\VirtualScannerWIA`0`0")
    [SetupDiHelper]::SetupDiSetDeviceRegistryProperty(
        $hDevInfo, [ref]$devInfo,
        [SetupDiHelper]::SPDRP_HARDWAREID, $hwid, $hwid.Length) | Out-Null

    # Set description
    $desc = [System.Text.Encoding]::Unicode.GetBytes("VirtualScanner ADS-4700W`0")
    [SetupDiHelper]::SetupDiSetDeviceRegistryProperty(
        $hDevInfo, [ref]$devInfo,
        [SetupDiHelper]::SPDRP_DEVICEDESC, $desc, $desc.Length) | Out-Null

    # Register the device
    $ok2 = [SetupDiHelper]::SetupDiCallClassInstaller(
        [SetupDiHelper]::DIF_REGISTERDEVICE, $hDevInfo, [ref]$devInfo)
    if ($ok2) {
        Write-Host "    SetupDi: dispositivo registrado"
    } else {
        $err = [System.Runtime.InteropServices.Marshal]::GetLastWin32Error()
        Write-Host "    SetupDi DIF_REGISTERDEVICE err=$err"
    }
    [SetupDiHelper]::SetupDiDestroyDeviceInfoList($hDevInfo) | Out-Null
}

# -----------------------------------------------------------------------
# Also write registry keys manually as fallback (belt + suspenders)
# -----------------------------------------------------------------------
Write-Host "[5] Escrevendo chaves de registro WIA..."

# Class\{image}\XXXX
$classBase = "HKLM:\SYSTEM\CurrentControlSet\Control\Class\$classGUID"
$nextIdx = 0
if (Test-Path $classBase) {
    $nums = Get-ChildItem $classBase -ErrorAction SilentlyContinue |
            Where-Object { $_.PSChildName -match '^\d{4}$' } |
            ForEach-Object { [int]$_.PSChildName } | Sort-Object -Descending
    if ($nums) { $nextIdx = $nums[0] + 1 }
}
$idxStr   = "{0:D4}" -f $nextIdx
$classKey = "$classBase\$idxStr"
New-Item $classKey                    -Force | Out-Null
New-Item "$classKey\DeviceData"       -Force | Out-Null
Set-ItemProperty $classKey "ClassGUID"      $classGUID
Set-ItemProperty $classKey "Class"          "Image"
Set-ItemProperty $classKey "DriverDesc"     "VirtualScanner ADS-4700W"
Set-ItemProperty $classKey "Manufacturer"   "VirtualScanner"
Set-ItemProperty $classKey "USDClass"       $CLSID
Set-ItemProperty $classKey "HardwareConfig" -Value 1 -Type DWord
Set-ItemProperty $classKey "DeviceType"     -Value 1 -Type DWord
Set-ItemProperty $classKey "Capabilities"   -Value 0 -Type DWord
Set-ItemProperty $classKey "WiaVersion"     "2.0"
Set-ItemProperty $classKey "SubClass"       "StillImage"
Set-ItemProperty "$classKey\DeviceData" "Server"  "local"
Set-ItemProperty "$classKey\DeviceData" "TwainDS" ""
Set-ItemProperty "$classKey\DeviceData" "USDClass" $CLSID
Set-ItemProperty "$classKey\DeviceData" "WiaVersion" "2.0"
Set-ItemProperty "$classKey\DeviceData" "SubClass" "StillImage"
Write-Host "    Class key: $idxStr"

# Enum\ROOT\IMAGE
$enumBase = "HKLM:\SYSTEM\CurrentControlSet\Enum\ROOT\IMAGE"
New-Item $enumBase -Force -ErrorAction SilentlyContinue | Out-Null
$nextDev = 0
if (Test-Path $enumBase) {
    $ex = Get-ChildItem $enumBase -ErrorAction SilentlyContinue |
          Where-Object { $_.PSChildName -match '^\d{4}$' } |
          ForEach-Object { [int]$_.PSChildName } | Sort-Object -Descending
    if ($ex) { $nextDev = $ex[0] + 1 }
}
$devStr  = "{0:D4}" -f $nextDev
$enumKey = "$enumBase\$devStr"
New-Item $enumKey                         -Force | Out-Null
New-Item "$enumKey\Device Parameters"     -Force | Out-Null
Set-ItemProperty $enumKey "ClassGUID"     $classGUID
Set-ItemProperty $enumKey "Class"         "Image"
Set-ItemProperty $enumKey "DeviceDesc"    "VirtualScanner ADS-4700W"
Set-ItemProperty $enumKey "Manufacturer"  "VirtualScanner"
Set-ItemProperty $enumKey "HardwareID"    "ROOT\IMAGE\VirtualScannerWIA"
Set-ItemProperty $enumKey "Service"       "StillImage"
Set-ItemProperty $enumKey "ConfigFlags"   -Value 0 -Type DWord
Set-ItemProperty $enumKey "Capabilities"  -Value 0 -Type DWord
Set-ItemProperty $enumKey "Driver"        "$classGUID\$idxStr"
# The critical DeviceData under Enum (stisvc reads USDClass from here)
New-Item "$enumKey\DeviceData"            -Force | Out-Null
Set-ItemProperty "$enumKey\DeviceData" "USDClass"       $CLSID
Set-ItemProperty "$enumKey\DeviceData" "Server"         "local"
Set-ItemProperty "$enumKey\DeviceData" "HardwareConfig" -Value 1 -Type DWord
Set-ItemProperty "$enumKey\DeviceData" "DeviceType"     -Value 1 -Type DWord
Write-Host "    Enum key: ROOT\IMAGE\$devStr"

# -----------------------------------------------------------------------
# Start WIA and force re-enumeration
# -----------------------------------------------------------------------
Write-Host "[6] Iniciando WIA..."
Start-Service stisvc -ErrorAction SilentlyContinue
Start-Sleep 4

$svc = Get-Service stisvc -ErrorAction SilentlyContinue
Write-Host "    WIA Service: $(if($svc){$svc.Status}else{'N/A'})"

# Force WIA to re-scan devices by restarting once more
Write-Host "[7] Forcando re-enumeracao..."
Stop-Service stisvc -Force -ErrorAction SilentlyContinue
Start-Sleep 2
Start-Service stisvc -ErrorAction SilentlyContinue
Start-Sleep 4

Write-Host "[8] Dispositivos WIA:"
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
} catch {
    Write-Host "    Erro COM: $($_.Exception.Message)"
}

Write-Host ""
Write-Host "=============================="
Write-Host "  Concluido!"
Write-Host "=============================="
Write-Host ""
Write-Host "Se VirtualScanner nao aparecer na lista acima,"
Write-Host "rode o metodo alternativo via INF:"
Write-Host "  .\install_wia_inf.ps1"
Write-Host ""
Write-Host "Coloque imagens em: C:\VirtualScanner\Queue\"
Write-Host "Log: C:\VirtualScanner\Logs\vscanner.log"
