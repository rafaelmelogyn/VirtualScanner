# VirtualScanner – Scanner Virtual para Windows

Scanner virtual que lê imagens de uma pasta e as expõe como scanner físico
para qualquer software Windows, incluindo sistemas de cartórios.

## Compatibilidade

| Software                     | Protocolo | Bits |
|------------------------------|-----------|------|
| Windows Fax and Scan         | WIA       | 64   |
| Microsoft Paint              | WIA       | 64   |
| Qualquer app moderno Windows | WIA       | 64   |
| NAPS2                        | TWAIN     | 32   |
| IrfanView                    | TWAIN     | 32   |
| e-Notariado / PrinTWAIN      | TWAIN     | 32   |
| Software de cartório legado  | TWAIN     | 32   |

## Pré-requisitos

- Windows 10 ou 11 (x64)
- Visual Studio 2019 ou 2022 Community (com "Desktop development with C++")
  Download: https://visualstudio.microsoft.com/downloads/
- PowerShell rodando como **Administrador**

## Estrutura do projeto

```
VirtualScanner/
├── build.ps1                    ← script principal (compilar + instalar)
├── uninstall.ps1                ← remover o driver
├── shared/
│   └── ImageLoader.h            ← carregador de imagens (GDI+, shared)
├── wia/
│   ├── WiaDriver.h              ← header do minidriver WIA
│   ├── WiaDriver.cpp            ← implementação WIA 2.0 (64-bit)
│   ├── VirtualScannerWIA.def    ← exports COM
│   └── VirtualScannerWIA.inf   ← INF de instalação (para deploy via setupapi)
└── twain/
    ├── twain.h                  ← header TWAIN 1.9 (limpo, sem conflito)
    ├── VirtualScanner_TWAIN.cpp ← implementação TWAIN DS (32-bit)
    └── VirtualScanner_TWAIN.def ← export DS_Entry @1
```

## Como usar

### 1. Compilar e instalar

Abra **PowerShell como Administrador**, vá até a pasta do projeto e rode:

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
.\build.ps1
```

O script:
- Compila o WIA driver (64-bit) usando vcvars64
- Compila o TWAIN driver (32-bit) usando vcvars32
- Copia as DLLs para os locais corretos do Windows
- Registra todos os entries de registro necessários
- Reinicia o serviço WIA (stisvc)

### 2. Colocar imagens na fila

Copie arquivos JPG, PNG, BMP, TIF para:

```
C:\VirtualScanner\Queue\
```

Compatibilidade adicional: se existir uma pasta legada `C:\VirtulScanner\Queue\`,
a fila também será lida automaticamente para facilitar testes e migração.

As imagens são lidas em **ordem alfabética** e o driver recarrega a fila automaticamente
quando novas imagens entram após o início da sessão de digitalização.

### 3. Escanear

**Via WIA (Windows Fax and Scan, Paint, etc.):**
1. Abra o aplicativo
2. Vá em Arquivo → Digitalizar / Novo scan
3. Selecione "VirtualScanner ADS-4700W"
4. Clique em Digitalizar

**Via TWAIN (NAPS2, cartório, etc.):**
1. Abra o aplicativo
2. Vá em Scanners → Adicionar dispositivo → TWAIN
3. Selecione "VirtualScanner ADS-4700W"
4. Clique em Escanear

### 4. Verificar log

```
C:\VirtualScanner\Logs\vscanner.log
```

### 5. Desinstalar

```powershell
.\uninstall.ps1
```

## Solução de problemas

**Scanner não aparece no WIA:**
```powershell
Restart-Service stisvc
```

**Log mostra apenas `DllGetClassObject` / `CreateInstance` / `QueryInterface` (sem `drvInitializeWia`):**
Isso indica que o COM está carregando a DLL, mas a sessão WIA não está completando a negociação do mini-driver.
Use esta versão que corrige identidade COM de `IUnknown` no `QueryInterface` e marca o item raiz como `WiaItemTypeFolder`.

**Scanner WIA instalado mas não listado (Total: 0):**
Execute estas verificações no PowerShell (Admin):
```powershell
Get-ItemProperty "HKLM:\SYSTEM\CurrentControlSet\Control\StillImage\Devices\ROOT\IMAGE\0000" -ErrorAction SilentlyContinue
Get-ItemProperty "HKLM:\SYSTEM\CurrentControlSet\Enum\ROOT\IMAGE\0000" -ErrorAction SilentlyContinue
Get-ItemProperty "HKLM:\SYSTEM\CurrentControlSet\Enum\ROOT\IMAGE\0000\DeviceData" -ErrorAction SilentlyContinue
```
Se não existir `StillImage\Devices\ROOT\IMAGE\0000`, rode novamente `install_wia.ps1` atualizado.

Se continuar com `Total: 0`, verifique no log o `riid` pedido em `CreateInstance`/`QI`.
Se aparecer `QI NOINTERFACE`, envie o GUID para ajustar suporte de interface no driver.

Após instalar, confirme no log chamadas de `drvInitializeWia` e `InitRootProperties`; se só houver `CreateInstance`, a enumeração não completou.

**Scanner não aparece no TWAIN:**
Verifique se os 4 registros foram criados:
```powershell
Get-Item "HKLM:\SOFTWARE\WOW6432Node\TWAIN\VirtualScanner ADS-4700W"
```

**Build WIA mostra erro mas ainda diz "Build OK":**
Use a versão atualizada de `build_wia_only.ps1` (ela remove DLL antiga e falha corretamente no `cl`).
Se necessário, apague manualmente `wia\VirtualScannerWIA.dll` antes de compilar.

**Erro de compilação "wiaguid.lib not found":**
Instale o Windows SDK (incluso no Visual Studio Installer como componente opcional
"Windows 10 SDK" ou "Windows 11 SDK").

**Erro de policy de execução do PowerShell:**
```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
```

**Testar sem abrir app:**
```powershell
# Listar scanners WIA disponíveis:
$wia = New-Object -ComObject WIA.DeviceManager
$wia.DeviceInfos | ForEach-Object { $_.Properties["Name"].Value }
```

## Notas técnicas

- O **WIA driver** é uma DLL COM 64-bit carregada pelo serviço `stisvc` no processo local.
  Não requer assinatura de kernel (é user-mode).

- O **TWAIN driver** é uma DLL 32-bit colocada em `C:\Windows\twain_32\`.
  O TWAIN DSM a carrega via registro em `HKLM\SOFTWARE\WOW6432Node\TWAIN\`.

- As imagens são lidas com **GDI+** (nativo no Windows), suportando
  JPG, PNG, BMP, GIF, TIF sem dependências externas.

- O CLSID do WIA driver é fixo:
  `{A1B2C3D4-E5F6-7890-ABCD-EF1234567890}`
