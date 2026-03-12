//=============================================================================
// VirtualScanner_TWAIN.cpp
// TWAIN Data Source (32-bit) inspired by twain/twain-samples state flow.
//=============================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <string>
#include <vector>

#include "twain.h"
#include "..\shared\ImageLoader.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "gdiplus.lib")

namespace {

constexpr char kManufacturer[] = "VirtualScanner";
constexpr char kProductFamily[] = "Virtual Scanner";
constexpr char kProductName[] = "VirtualScanner ADS-4700W";

TW_UINT16 g_conditionCode = TWCC_SUCCESS;
HWND g_parentWindow = nullptr;
bool g_dsOpen = false;
bool g_dsEnabled = false;
bool g_xferReadySent = false;
std::vector<std::wstring> g_imageQueue;
size_t g_nextImageIndex = 0;

void ResetTransferQueue()
{
    g_imageQueue = VS_ScanQueue();
    g_nextImageIndex = 0;
    g_xferReadySent = false;
    VSLog(L"TWAIN queue loaded: %zu file(s)", g_imageQueue.size());
}

bool HasPendingTransfer()
{
    return g_nextImageIndex < g_imageQueue.size();
}

TW_HANDLE MakeOneValue(TW_UINT16 itemType, TW_UINT32 value)
{
    auto* one = reinterpret_cast<TW_ONEVALUE*>(GlobalAlloc(GPTR, sizeof(TW_ONEVALUE)));
    if (!one) {
        g_conditionCode = TWCC_LOWMEMORY;
        return nullptr;
    }
    one->ItemType = itemType;
    one->Item = value;
    return one;
}

TW_HANDLE MakeFix32(float value)
{
    auto* one = reinterpret_cast<TW_ONEVALUE*>(GlobalAlloc(GPTR, sizeof(TW_ONEVALUE)));
    if (!one) {
        g_conditionCode = TWCC_LOWMEMORY;
        return nullptr;
    }

    one->ItemType = TWTY_FIX32;
    TW_FIX32 fx{};
    fx.Whole = static_cast<TW_INT16>(value);
    fx.Frac = static_cast<TW_UINT16>((value - static_cast<float>(fx.Whole)) * 65536.0f);
    memcpy(&one->Item, &fx, sizeof(TW_FIX32));
    return one;
}

TW_UINT16 FillIdentity(pTW_IDENTITY identity)
{
    if (!identity) {
        g_conditionCode = TWCC_BADVALUE;
        return TWRC_FAILURE;
    }

    identity->Id = 1;
    identity->ProtocolMajor = TWON_PROTOCOLMAJOR;
    identity->ProtocolMinor = TWON_PROTOCOLMINOR;
    identity->SupportedGroups = DG_CONTROL | DG_IMAGE;
    identity->Version.MajorNum = 1;
    identity->Version.MinorNum = 0;
    identity->Version.Language = TWLG_USA;
    identity->Version.Country = TWCY_USA;

    strncpy_s(identity->Version.Info, "1.0.0", sizeof(identity->Version.Info) - 1);
    strncpy_s(identity->Manufacturer, kManufacturer, sizeof(identity->Manufacturer) - 1);
    strncpy_s(identity->ProductFamily, kProductFamily, sizeof(identity->ProductFamily) - 1);
    strncpy_s(identity->ProductName, kProductName, sizeof(identity->ProductName) - 1);

    g_conditionCode = TWCC_SUCCESS;
    return TWRC_SUCCESS;
}

TW_UINT16 HandleCapability(pTW_CAPABILITY cap, TW_UINT16 msg)
{
    if (!cap) {
        g_conditionCode = TWCC_BADVALUE;
        return TWRC_FAILURE;
    }

    if (msg == MSG_SET || msg == MSG_SETCONSTRAINT || msg == MSG_RESET) {
        g_conditionCode = TWCC_SUCCESS;
        return TWRC_SUCCESS;
    }

    if (msg != MSG_GET && msg != MSG_GETCURRENT && msg != MSG_GETDEFAULT && msg != MSG_QUERYSUPPORT) {
        g_conditionCode = TWCC_CAPBADOPERATION;
        return TWRC_FAILURE;
    }

    switch (cap->Cap) {
    case CAP_XFERCOUNT:
        cap->ConType = TWON_ONEVALUE;
        cap->hContainer = MakeOneValue(TWTY_INT16, static_cast<TW_UINT32>(static_cast<TW_INT16>(-1)));
        return cap->hContainer ? TWRC_SUCCESS : TWRC_FAILURE;

    case CAP_UICONTROLLABLE:
    case CAP_DEVICEONLINE:
        cap->ConType = TWON_ONEVALUE;
        cap->hContainer = MakeOneValue(TWTY_BOOL, TRUE);
        return cap->hContainer ? TWRC_SUCCESS : TWRC_FAILURE;

    case CAP_FEEDERENABLED:
        cap->ConType = TWON_ONEVALUE;
        cap->hContainer = MakeOneValue(TWTY_BOOL, FALSE);
        return cap->hContainer ? TWRC_SUCCESS : TWRC_FAILURE;

    case CAP_FEEDERLOADED:
        cap->ConType = TWON_ONEVALUE;
        cap->hContainer = MakeOneValue(TWTY_BOOL, HasPendingTransfer() ? TRUE : FALSE);
        return cap->hContainer ? TWRC_SUCCESS : TWRC_FAILURE;

    case CAP_INDICATORS:
        cap->ConType = TWON_ONEVALUE;
        cap->hContainer = MakeOneValue(TWTY_BOOL, FALSE);
        return cap->hContainer ? TWRC_SUCCESS : TWRC_FAILURE;

    case ICAP_XFERMECH:
        cap->ConType = TWON_ONEVALUE;
        cap->hContainer = MakeOneValue(TWTY_UINT16, TWSX_NATIVE);
        return cap->hContainer ? TWRC_SUCCESS : TWRC_FAILURE;

    case ICAP_PIXELTYPE:
        cap->ConType = TWON_ONEVALUE;
        cap->hContainer = MakeOneValue(TWTY_UINT16, TWPT_RGB);
        return cap->hContainer ? TWRC_SUCCESS : TWRC_FAILURE;

    case ICAP_BITDEPTH:
        cap->ConType = TWON_ONEVALUE;
        cap->hContainer = MakeOneValue(TWTY_UINT16, 24);
        return cap->hContainer ? TWRC_SUCCESS : TWRC_FAILURE;

    case ICAP_UNITS:
        cap->ConType = TWON_ONEVALUE;
        cap->hContainer = MakeOneValue(TWTY_UINT16, TWUN_INCHES);
        return cap->hContainer ? TWRC_SUCCESS : TWRC_FAILURE;

    case ICAP_XRESOLUTION:
    case ICAP_YRESOLUTION:
    case ICAP_XNATIVERESOLUTION:
    case ICAP_YNATIVERESOLUTION:
        cap->ConType = TWON_ONEVALUE;
        cap->hContainer = MakeFix32(200.0f);
        return cap->hContainer ? TWRC_SUCCESS : TWRC_FAILURE;

    default:
        g_conditionCode = TWCC_CAPUNSUPPORTED;
        return TWRC_FAILURE;
    }
}

TW_UINT16 HandleControl(TW_UINT16 DAT, TW_UINT16 MSG, TW_MEMREF pData)
{
    switch (DAT) {
    case DAT_STATUS:
        if (MSG == MSG_GET) {
            auto* status = reinterpret_cast<pTW_STATUS>(pData);
            if (!status) {
                g_conditionCode = TWCC_BADVALUE;
                return TWRC_FAILURE;
            }
            status->ConditionCode = g_conditionCode;
            status->Reserved = 0;
            return TWRC_SUCCESS;
        }
        break;

    case DAT_IDENTITY:
        if (MSG == MSG_GET || MSG == MSG_GETFIRST) {
            return FillIdentity(reinterpret_cast<pTW_IDENTITY>(pData));
        }
        if (MSG == MSG_GETNEXT) {
            auto rc = FillIdentity(reinterpret_cast<pTW_IDENTITY>(pData));
            return (rc == TWRC_SUCCESS) ? TWRC_ENDOFLIST : rc;
        }
        if (MSG == MSG_OPENDS) {
            g_dsOpen = true;
            g_conditionCode = TWCC_SUCCESS;
            ResetTransferQueue();
            VSLog(L"TWAIN MSG_OPENDS");
            return TWRC_SUCCESS;
        }
        if (MSG == MSG_CLOSEDS) {
            g_dsEnabled = false;
            g_dsOpen = false;
            g_xferReadySent = false;
            g_conditionCode = TWCC_SUCCESS;
            VSLog(L"TWAIN MSG_CLOSEDS");
            return TWRC_SUCCESS;
        }
        break;

    case DAT_USERINTERFACE:
        if (MSG == MSG_ENABLEDS || MSG == MSG_ENABLEDSUIONLY) {
            auto* ui = reinterpret_cast<pTW_USERINTERFACE>(pData);
            g_parentWindow = ui ? reinterpret_cast<HWND>(ui->hParent) : nullptr;
            g_dsEnabled = true;
            g_conditionCode = TWCC_SUCCESS;
            ResetTransferQueue();
            VSLog(L"TWAIN MSG_ENABLEDS parent=%p", g_parentWindow);
            return TWRC_SUCCESS;
        }
        if (MSG == MSG_DISABLEDS) {
            g_dsEnabled = false;
            g_xferReadySent = false;
            g_conditionCode = TWCC_SUCCESS;
            VSLog(L"TWAIN MSG_DISABLEDS");
            return TWRC_SUCCESS;
        }
        break;

    case DAT_EVENT:
        if (MSG == MSG_PROCESSEVENT) {
            auto* twEvent = reinterpret_cast<pTW_EVENT>(pData);
            if (!twEvent) {
                g_conditionCode = TWCC_BADVALUE;
                return TWRC_FAILURE;
            }

            if (g_dsEnabled && HasPendingTransfer() && !g_xferReadySent) {
                twEvent->TWMessage = MSG_XFERREADY;
                g_xferReadySent = true;
                g_conditionCode = TWCC_SUCCESS;
                return TWRC_DSEVENT;
            }

            twEvent->TWMessage = MSG_NULL;
            g_conditionCode = TWCC_SUCCESS;
            return TWRC_NOTDSEVENT;
        }
        break;

    case DAT_PENDINGXFERS:
        if (!pData) {
            g_conditionCode = TWCC_BADVALUE;
            return TWRC_FAILURE;
        }
        {
            auto* pending = reinterpret_cast<pTW_PENDINGXFERS>(pData);
            if (MSG == MSG_GET || MSG == MSG_ENDXFER) {
                pending->Count = static_cast<TW_UINT16>(std::min<size_t>(65535, g_imageQueue.size() - g_nextImageIndex));
                if (MSG == MSG_ENDXFER) {
                    g_xferReadySent = false;
                }
                g_conditionCode = TWCC_SUCCESS;
                return TWRC_SUCCESS;
            }
            if (MSG == MSG_RESET) {
                g_nextImageIndex = g_imageQueue.size();
                pending->Count = 0;
                g_xferReadySent = false;
                g_conditionCode = TWCC_SUCCESS;
                return TWRC_SUCCESS;
            }
        }
        break;

    case DAT_CAPABILITY:
        return HandleCapability(reinterpret_cast<pTW_CAPABILITY>(pData), MSG);

    case DAT_SETUPMEMXFER:
        if (MSG == MSG_GET && pData) {
            auto* mem = reinterpret_cast<pTW_SETUPMEMXFER>(pData);
            mem->MinBufSize = 64 * 1024;
            mem->MaxBufSize = 512 * 1024;
            mem->Preferred = 128 * 1024;
            g_conditionCode = TWCC_SUCCESS;
            return TWRC_SUCCESS;
        }
        break;

    default:
        break;
    }

    g_conditionCode = TWCC_SUCCESS;
    return TWRC_SUCCESS;
}

TW_UINT16 HandleImage(TW_UINT16 DAT, TW_UINT16 MSG, TW_MEMREF pData)
{
    if (DAT == DAT_IMAGEINFO && MSG == MSG_GET) {
        auto* info = reinterpret_cast<pTW_IMAGEINFO>(pData);
        if (!info) {
            g_conditionCode = TWCC_BADVALUE;
            return TWRC_FAILURE;
        }

        int width = 850;
        int height = 1100;
        if (HasPendingTransfer()) {
            HGLOBAL dimProbe = VS_LoadAsDIB(g_imageQueue[g_nextImageIndex], &width, &height);
            if (dimProbe) {
                GlobalFree(dimProbe);
            }
        }

        info->XResolution.Whole = 200;
        info->XResolution.Frac = 0;
        info->YResolution.Whole = 200;
        info->YResolution.Frac = 0;
        info->ImageWidth = width;
        info->ImageLength = height;
        info->SamplesPerPixel = 3;
        info->BitsPerSample[0] = 8;
        info->BitsPerSample[1] = 8;
        info->BitsPerSample[2] = 8;
        info->BitsPerPixel = 24;
        info->Planar = FALSE;
        info->PixelType = TWPT_RGB;
        info->Compression = TWCP_NONE;
        g_conditionCode = TWCC_SUCCESS;
        return TWRC_SUCCESS;
    }

    if (DAT == DAT_IMAGENATIVEXFER && MSG == MSG_GET) {
        if (!pData || !HasPendingTransfer()) {
            g_conditionCode = TWCC_NODS;
            return TWRC_FAILURE;
        }

        HGLOBAL dib = VS_LoadAsDIB(g_imageQueue[g_nextImageIndex]);
        if (!dib) {
            g_conditionCode = TWCC_OPERATIONERROR;
            return TWRC_FAILURE;
        }

        *reinterpret_cast<TW_HANDLE*>(pData) = dib;
        VSLog(L"TWAIN native xfer: %s", g_imageQueue[g_nextImageIndex].c_str());
        ++g_nextImageIndex;
        g_conditionCode = TWCC_SUCCESS;
        return TWRC_XFERDONE;
    }

    g_conditionCode = TWCC_SUCCESS;
    return TWRC_SUCCESS;
}

} // namespace

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        CreateDirectoryW(L"C:\\VirtualScanner", nullptr);
        CreateDirectoryW(L"C:\\VirtualScanner\\Logs", nullptr);
        CreateDirectoryW(L"C:\\VirtualScanner\\Queue", nullptr);
        VSLog(L"TWAIN DS attached PID=%lu", GetCurrentProcessId());
    }
    return TRUE;
}

extern "C" __declspec(dllexport)
TW_UINT16 FAR __stdcall DS_Entry(
    pTW_IDENTITY /*origin*/,
    pTW_IDENTITY /*dest*/,
    TW_UINT32 DG,
    TW_UINT16 DAT,
    TW_UINT16 MSG,
    TW_MEMREF pData)
{
    if (DG == DG_CONTROL || DAT == DAT_STATUS || DAT == DAT_IDENTITY || DAT == DAT_CAPABILITY || DAT == DAT_PENDINGXFERS || DAT == DAT_EVENT || DAT == DAT_USERINTERFACE) {
        return HandleControl(DAT, MSG, pData);
    }

    if (DG == DG_IMAGE) {
        return HandleImage(DAT, MSG, pData);
    }

    g_conditionCode = TWCC_SUCCESS;
    return TWRC_SUCCESS;
}
