#Requires -RunAsAdministrator
Write-Host "Desinstalando VirtualScanner..." -ForegroundColor Yellow

Stop-Service -Name stisvc -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1

$clsid = "{A1B2C3D4-E5F6-7890-ABCD-EF1234567890}"
Remove-Item "HKLM:\SOFTWARE\Classes\CLSID\$clsid" -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item "HKLM:\SYSTEM\CurrentControlSet\Control\StillImage\Devices\VSCANNER_VirtualScannerWIA" -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item "C:\Windows\System32\VirtualScannerWIA.dll" -Force -ErrorAction SilentlyContinue

$twainName = "VirtualScanner ADS-4700W"
@(
    "HKLM:\SOFTWARE\WOW6432Node\TWAIN Working Group\TWAIN\$twainName",
    "HKLM:\SOFTWARE\WOW6432Node\TWAIN\$twainName",
    "HKLM:\SOFTWARE\TWAIN Working Group\TWAIN\$twainName",
    "HKLM:\SOFTWARE\TWAIN\$twainName"
) | ForEach-Object { Remove-Item $_ -Recurse -Force -ErrorAction SilentlyContinue }

Remove-Item "C:\Windows\twain_32\VirtualScanner" -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item "C:\Windows\twain_32\TWAINDS_VirtualScanner.ds" -Force -ErrorAction SilentlyContinue

Start-Service stisvc -ErrorAction SilentlyContinue
Write-Host "Desinstalado com sucesso." -ForegroundColor Green
