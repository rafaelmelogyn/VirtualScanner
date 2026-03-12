# VirtualScanner (reconstruído do zero)

Projeto refeito para expor um scanner virtual estável via:

- **TWAIN (32-bit)** para NAPS2 e softwares legados.
- **WIA (64-bit)** para Windows Fax and Scan, Paint e apps WIA.

A implementação TWAIN segue um fluxo de estados compatível com o modelo usado nos exemplos do repositório `twain/twain-samples` (abertura da fonte, enable, evento `MSG_XFERREADY`, transferência, `MSG_ENDXFER`).

---

## Estrutura

```text
VirtualScanner/
├─ build.ps1                  # build + instalação completa (WIA + TWAIN)
├─ build_twain_only.ps1       # build + instalação TWAIN
├─ build_wia_only.ps1         # build + instalação WIA
├─ uninstall.ps1              # remove registro e arquivos instalados
├─ shared/
│  └─ ImageLoader.h           # leitura de imagens e log
├─ twain/
│  ├─ twain.h
│  ├─ VirtualScanner_TWAIN.cpp
│  └─ VirtualScanner_TWAIN.def
└─ wia/
   ├─ WiaDriver.cpp
   ├─ WiaDriver.h
   ├─ VirtualScannerWIA.def
   └─ VirtualScannerWIA.inf
```

---

## Pré-requisitos

- Windows 10/11 x64
- Visual Studio 2019/2022 com C++ Desktop
- PowerShell em modo Administrador

---

## Build e instalação

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\build.ps1
```

O script:

1. Compila WIA x64.
2. Compila TWAIN x86.
3. Instala e registra WIA (COM + StillImage).
4. Instala TWAIN em `C:\Windows\twain_32\VirtualScanner\TWAINDS_VirtualScanner.ds`.
5. Cria chaves TWAIN para hosts 32 e 64 bits.

---

## Pasta de entrada das imagens

Coloque imagens em:

```text
C:\VirtualScanner\Queue\
```

Formatos suportados pelo loader atual: JPG/PNG/BMP/TIF/GIF.

As imagens são entregues em ordem alfabética.

---

## Checklist rápido (quando o scanner não aparece)

### 1) Validar TWAIN

```powershell
Get-Item "HKLM:\SOFTWARE\WOW6432Node\TWAIN\VirtualScanner ADS-4700W"
```

Verifique se `Path` aponta para o arquivo `.ds` em `twain_32`.

### 2) Validar WIA

```powershell
$wia = New-Object -ComObject WIA.DeviceManager
$wia.DeviceInfos | ForEach-Object { $_.Properties["Name"].Value }
```

O nome esperado é `VirtualScanner ADS-4700W`.

### 3) Reiniciar serviço WIA

```powershell
Restart-Service stisvc
```

### 4) Ver log

```text
C:\VirtualScanner\Logs\vscanner.log
```

---

## Observações importantes

- Apps 64-bit só enxergam TWAIN 64-bit; apps 32-bit enxergam TWAIN 32-bit.
  Este projeto instala **TWAIN 32-bit** (compatível com a maioria dos legados).
- Para Windows Fax and Scan, o canal é WIA.
- Se houver outro driver com mesmo nome, desinstale antes com `uninstall.ps1`.
