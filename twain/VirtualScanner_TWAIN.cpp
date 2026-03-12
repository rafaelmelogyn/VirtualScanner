//=============================================================================
// VirtualScanner_TWAIN.cpp
// TWAIN 1.9 Data Source  –  32-bit DLL
// Compatible with: NAPS2, IrfanView, cartório software (PrinTWAIN, eCartório…)
// Build: vcvars32 + cl (see build.ps1)
//=============================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <gdiplus.h>
#include <vector>
#include <string>
#include <algorithm>

// twain.h lives in the same twain/ folder
#include "twain.h"

// shared image loader
#include "..\shared\ImageLoader.h"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

//=============================================================================
// State
//=============================================================================
static TW_UINT16             g_cc            = TWCC_SUCCESS;
static std::vector<std::wstring> g_queue;
static int                   g_idx           = 0;
static bool                  g_ready         = false;   // images available
static bool                  g_readySent     = false;   // XFERREADY already sent
static HWND                  g_hAppWnd       = nullptr;

//=============================================================================
// DllMain
//=============================================================================
BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        CreateDirectoryW(L"C:\\VirtualScanner",        nullptr);
        CreateDirectoryW(L"C:\\VirtualScanner\\Logs",  nullptr);
        CreateDirectoryW(L"C:\\VirtualScanner\\Queue", nullptr);
        VSLog(L"TWAIN DS attached (PID=%lu)", GetCurrentProcessId());
    }
    return TRUE;
}

//=============================================================================
// Helpers
//=============================================================================
static void ResetState()
{
    g_queue     = VS_ScanQueue();
    g_idx       = 0;
    g_ready     = !g_queue.empty();
    g_readySent = false;
    VSLog(L"Queue reloaded: %zu files", g_queue.size());
}

static void RefreshQueueIfNeeded()
{
    if (g_idx >= (int)g_queue.size()) {
        std::vector<std::wstring> updated = VS_ScanQueue();
        if (!updated.empty()) {
            g_queue.swap(updated);
            g_idx = 0;
            VSLog(L"Queue refreshed dynamically: %zu files", g_queue.size());
        }
    }
    g_ready = g_idx < (int)g_queue.size();
}

// Build a TW_ONEVALUE container on the heap (caller owns memory via GlobalAlloc)
static TW_HANDLE MakeOneVal(TW_UINT16 type, TW_UINT32 item)
{
    TW_ONEVALUE* v = (TW_ONEVALUE*)GlobalAlloc(GPTR, sizeof(TW_ONEVALUE));
    if (v) { v->ItemType = type; v->Item = item; }
    return v;
}
static TW_HANDLE MakeFix32Val(float f)
{
    TW_ONEVALUE* v = (TW_ONEVALUE*)GlobalAlloc(GPTR, sizeof(TW_ONEVALUE));
    if (v) {
        v->ItemType = TWTY_FIX32;
        TW_FIX32 fx; fx.Whole=(TW_INT16)f; fx.Frac=(TW_UINT16)((f-(int)f)*65536.0f);
        memcpy(&v->Item, &fx, sizeof(TW_FIX32));
    }
    return v;
}

//=============================================================================
// DG_CONTROL dispatcher
//=============================================================================
static TW_UINT16 Control(TW_UINT32 DAT, TW_UINT16 MSG, TW_MEMREF pData)
{
    //--- DAT_STATUS --------------------------------------------------------
    if (DAT == DAT_STATUS && MSG == MSG_GET) {
        pTW_STATUS p = (pTW_STATUS)pData;
        p->ConditionCode = g_cc; p->Reserved = 0;
        g_cc = TWCC_SUCCESS;
        return TWRC_SUCCESS;
    }

    //--- DAT_IDENTITY -------------------------------------------------------
    if (DAT == DAT_IDENTITY) {
        auto Fill = [](pTW_IDENTITY p) {
            p->Id                 = 1;
            p->ProtocolMajor      = TWON_PROTOCOLMAJOR;
            p->ProtocolMinor      = TWON_PROTOCOLMINOR;
            p->SupportedGroups    = DG_CONTROL | DG_IMAGE;
            p->Version.MajorNum   = 1; p->Version.MinorNum = 0;
            p->Version.Language   = TWLG_USA; p->Version.Country = TWCY_USA;
            strncpy_s(p->Version.Info,    "1.0",                    sizeof(p->Version.Info)-1);
            strncpy_s(p->Manufacturer,    "VirtualScanner",         sizeof(p->Manufacturer)-1);
            strncpy_s(p->ProductFamily,   "Virtual Scanner",        sizeof(p->ProductFamily)-1);
            strncpy_s(p->ProductName,     "VirtualScanner ADS-4700W", sizeof(p->ProductName)-1);
        };
        if (MSG == MSG_GET || MSG == MSG_GETFIRST) { Fill((pTW_IDENTITY)pData); return TWRC_SUCCESS; }
        if (MSG == MSG_GETNEXT)                    { Fill((pTW_IDENTITY)pData); return TWRC_ENDOFLIST; }
        if (MSG == MSG_OPENDS)  { ResetState(); VSLog(L"OPENDS");  return TWRC_SUCCESS; }
        if (MSG == MSG_CLOSEDS) { VSLog(L"CLOSEDS");               return TWRC_SUCCESS; }
    }

    //--- DAT_USERINTERFACE --------------------------------------------------
    if (DAT == DAT_USERINTERFACE) {
        if (MSG == MSG_ENABLEDS || MSG == MSG_ENABLEDSUIONLY) {
            pTW_USERINTERFACE ui = (pTW_USERINTERFACE)pData;
            g_hAppWnd = ui ? (HWND)ui->hParent : nullptr;
            ResetState();
            VSLog(L"ENABLEDS: %zu files queued", g_queue.size());
            return TWRC_SUCCESS;
        }
        if (MSG == MSG_DISABLEDS) { VSLog(L"DISABLEDS"); return TWRC_SUCCESS; }
    }

    //--- DAT_EVENT ----------------------------------------------------------
    if (DAT == DAT_EVENT && MSG == MSG_PROCESSEVENT) {
        pTW_EVENT pEv = (pTW_EVENT)pData;
        RefreshQueueIfNeeded();
        if (g_ready && !g_readySent) {
            pEv->TWMessage = MSG_XFERREADY;
            g_readySent    = true;
            VSLog(L"EVENT -> MSG_XFERREADY");
            return TWRC_DSEVENT;
        }
        pEv->TWMessage = MSG_NULL;
        return TWRC_NOTDSEVENT;
    }

    //--- DAT_PENDINGXFERS ---------------------------------------------------
    if (DAT == DAT_PENDINGXFERS) {
        pTW_PENDINGXFERS p = (pTW_PENDINGXFERS)pData;
        if (MSG == MSG_GET) {
            int rem = max(0, (int)g_queue.size() - g_idx);
            p->Count = (TW_UINT16)rem;
            VSLog(L"PENDINGXFERS GET=%d", rem);
            return TWRC_SUCCESS;
        }
        if (MSG == MSG_ENDXFER) {
            int rem = max(0, (int)g_queue.size() - g_idx);
            p->Count = (TW_UINT16)rem;
            if (rem > 0) { g_ready = true; g_readySent = false; }
            else         { g_ready = false; g_readySent = false; }
            VSLog(L"ENDXFER rem=%d", rem);
            return TWRC_SUCCESS;
        }
        if (MSG == MSG_RESET) {
            p->Count    = 0;
            g_idx       = (int)g_queue.size();
            g_ready     = false;
            g_readySent = false;
            VSLog(L"PENDINGXFERS RESET");
            return TWRC_SUCCESS;
        }
    }

    //--- DAT_CAPABILITY -----------------------------------------------------
    if (DAT == DAT_CAPABILITY) {
        pTW_CAPABILITY cap = (pTW_CAPABILITY)pData;

        // Silently accept any SET/RESET
        if (MSG == MSG_SET || MSG == MSG_SETCONSTRAINT || MSG == MSG_RESET)
            return TWRC_SUCCESS;

        if (MSG == MSG_GET || MSG == MSG_GETCURRENT || MSG == MSG_GETDEFAULT ||
            MSG == MSG_QUERYSUPPORT)
        {
            if (MSG == MSG_QUERYSUPPORT) {
                cap->hContainer = MakeOneVal(TWTY_UINT32,
                    TWQC_GET | TWQC_GETDEFAULT | TWQC_GETCURRENT | TWQC_SET | TWQC_RESET);
                cap->ConType = TWON_ONEVALUE;
                return TWRC_SUCCESS;
            }

            switch (cap->Cap) {
            case CAP_XFERCOUNT:
                cap->hContainer = MakeOneVal(TWTY_INT16, (TW_UINT32)(TW_INT16)-1);
                cap->ConType    = TWON_ONEVALUE;
                return TWRC_SUCCESS;

            case ICAP_XFERMECH:
                cap->hContainer = MakeOneVal(TWTY_UINT16, TWSX_NATIVE);
                cap->ConType    = TWON_ONEVALUE;
                return TWRC_SUCCESS;

            case ICAP_PIXELTYPE:
                cap->hContainer = MakeOneVal(TWTY_UINT16, TWPT_RGB);
                cap->ConType    = TWON_ONEVALUE;
                return TWRC_SUCCESS;

            case ICAP_BITDEPTH:
                cap->hContainer = MakeOneVal(TWTY_UINT16, 24);
                cap->ConType    = TWON_ONEVALUE;
                return TWRC_SUCCESS;

            case ICAP_UNITS:
                cap->hContainer = MakeOneVal(TWTY_UINT16, TWUN_INCHES);
                cap->ConType    = TWON_ONEVALUE;
                return TWRC_SUCCESS;

            case ICAP_COMPRESSION:
                cap->hContainer = MakeOneVal(TWTY_UINT16, TWCP_NONE);
                cap->ConType    = TWON_ONEVALUE;
                return TWRC_SUCCESS;

            case ICAP_PLANARCHUNKY:
                cap->hContainer = MakeOneVal(TWTY_UINT16, TWPC_CHUNKY);
                cap->ConType    = TWON_ONEVALUE;
                return TWRC_SUCCESS;

            case ICAP_PIXELFLAVOR:
                cap->hContainer = MakeOneVal(TWTY_UINT16, TWPF_CHOCOLATE);
                cap->ConType    = TWON_ONEVALUE;
                return TWRC_SUCCESS;

            case ICAP_XRESOLUTION:
            case ICAP_YRESOLUTION:
            case ICAP_XNATIVERESOLUTION:
            case ICAP_YNATIVERESOLUTION:
                cap->hContainer = MakeFix32Val(200.0f);
                cap->ConType    = TWON_ONEVALUE;
                return TWRC_SUCCESS;

            case ICAP_PHYSICALWIDTH:
                cap->hContainer = MakeFix32Val(8.5f);
                cap->ConType    = TWON_ONEVALUE;
                return TWRC_SUCCESS;

            case ICAP_PHYSICALHEIGHT:
                cap->hContainer = MakeFix32Val(14.0f);  // legal size
                cap->ConType    = TWON_ONEVALUE;
                return TWRC_SUCCESS;

            case ICAP_SUPPORTEDSIZES: {
                // enumerate: NONE, A4, USLETTER
                DWORD sz = sizeof(TW_ENUMERATION) + 2 * sizeof(TW_UINT16);
                TW_ENUMERATION* e = (TW_ENUMERATION*)GlobalAlloc(GPTR, sz);
                if (!e) return TWRC_FAILURE;
                e->ItemType     = TWTY_UINT16;
                e->NumItems     = 3;
                e->CurrentIndex = 0;
                e->DefaultIndex = 0;
                TW_UINT16* items = (TW_UINT16*)e->ItemList;
                items[0] = TWSS_NONE; items[1] = TWSS_A4; items[2] = TWSS_USLETTER;
                cap->hContainer = e;
                cap->ConType    = TWON_ENUMERATION;
                return TWRC_SUCCESS;
            }

            case CAP_UICONTROLLABLE:
                cap->hContainer = MakeOneVal(TWTY_BOOL, TRUE);
                cap->ConType    = TWON_ONEVALUE;
                return TWRC_SUCCESS;

            case CAP_DEVICEONLINE:
                cap->hContainer = MakeOneVal(TWTY_BOOL, TRUE);
                cap->ConType    = TWON_ONEVALUE;
                return TWRC_SUCCESS;

            case CAP_FEEDERENABLED:
            case CAP_FEEDERLOADED:
                cap->hContainer = MakeOneVal(TWTY_BOOL,
                    cap->Cap == CAP_FEEDERLOADED ? (!g_queue.empty() ? TRUE : FALSE) : FALSE);
                cap->ConType = TWON_ONEVALUE;
                return TWRC_SUCCESS;

            case CAP_INDICATORS:
                cap->hContainer = MakeOneVal(TWTY_BOOL, FALSE);
                cap->ConType    = TWON_ONEVALUE;
                return TWRC_SUCCESS;

            case CAP_SUPPORTEDCAPS: {
                static const TW_UINT16 caps[] = {
                    CAP_XFERCOUNT, CAP_SUPPORTEDCAPS, CAP_UICONTROLLABLE,
                    CAP_DEVICEONLINE, CAP_FEEDERENABLED, CAP_FEEDERLOADED,
                    CAP_INDICATORS,
                    ICAP_XFERMECH, ICAP_COMPRESSION, ICAP_PIXELTYPE,
                    ICAP_BITDEPTH, ICAP_UNITS, ICAP_XRESOLUTION, ICAP_YRESOLUTION,
                    ICAP_XNATIVERESOLUTION, ICAP_YNATIVERESOLUTION,
                    ICAP_PHYSICALWIDTH, ICAP_PHYSICALHEIGHT, ICAP_SUPPORTEDSIZES,
                    ICAP_PLANARCHUNKY, ICAP_PIXELFLAVOR
                };
                DWORD n = ARRAYSIZE(caps);
                DWORD sz = sizeof(TW_ARRAY) - 1 + n * sizeof(TW_UINT16);
                TW_ARRAY* arr = (TW_ARRAY*)GlobalAlloc(GPTR, sz);
                if (!arr) return TWRC_FAILURE;
                arr->ItemType = TWTY_UINT16; arr->NumItems = n;
                memcpy(arr->ItemList, caps, n * sizeof(TW_UINT16));
                cap->hContainer = arr; cap->ConType = TWON_ARRAY;
                return TWRC_SUCCESS;
            }
            } // switch

            g_cc = TWCC_CAPUNSUPPORTED;
            return TWRC_FAILURE;
        }
    }

    //--- DAT_SETUPMEMXFER ---------------------------------------------------
    if (DAT == DAT_SETUPMEMXFER && MSG == MSG_GET) {
        pTW_SETUPMEMXFER p = (pTW_SETUPMEMXFER)pData;
        p->MinBufSize = 65536; p->MaxBufSize = 524288; p->Preferred = 65536;
        return TWRC_SUCCESS;
    }

    // Unknown — succeed silently so DSM doesn't abort the session
    VSLog(L"Control: unhandled DAT=0x%04x MSG=0x%04x -> TWRC_SUCCESS", DAT, MSG);
    return TWRC_SUCCESS;
}

//=============================================================================
// DG_IMAGE dispatcher
//=============================================================================
static TW_UINT16 Image(TW_UINT32 DAT, TW_UINT16 MSG, TW_MEMREF pData)
{
    //--- DAT_IMAGEINFO ------------------------------------------------------
    if (DAT == DAT_IMAGEINFO && MSG == MSG_GET) {
        pTW_IMAGEINFO p = (pTW_IMAGEINFO)pData;
        // Read actual image dimensions if available
        int W = 850, H = 1100;
        if (g_idx < (int)g_queue.size()) {
            HGLOBAL tmp = VS_LoadAsDIB(g_queue[g_idx], &W, &H);
            if (tmp) { GlobalFree(tmp); }  // just for dimensions; real load on xfer
        }
        p->XResolution.Whole=200; p->XResolution.Frac=0;
        p->YResolution.Whole=200; p->YResolution.Frac=0;
        p->ImageWidth         = W;
        p->ImageLength        = H;
        p->SamplesPerPixel    = 3;
        p->BitsPerSample[0]   = p->BitsPerSample[1] = p->BitsPerSample[2] = 8;
        p->BitsPerPixel       = 24;
        p->Planar             = FALSE;
        p->PixelType          = TWPT_RGB;
        p->Compression        = TWCP_NONE;
        return TWRC_SUCCESS;
    }

    //--- DAT_IMAGENATIVEXFER ------------------------------------------------
    if (DAT == DAT_IMAGENATIVEXFER && MSG == MSG_GET) {
        RefreshQueueIfNeeded();
        VSLog(L"IMAGENATIVEXFER idx=%d / %zu", g_idx, g_queue.size());
        if (g_idx >= (int)g_queue.size()) {
            g_cc = TWCC_NODS;
            return TWRC_FAILURE;
        }
        HGLOBAL hDib = VS_LoadAsDIB(g_queue[g_idx]);
        if (!hDib) { g_cc = TWCC_OPERATIONERROR; return TWRC_FAILURE; }

        g_cc = TWCC_SUCCESS;

        *(TW_HANDLE*)pData = hDib;
        VSLog(L"Delivered image %d: %s", g_idx, g_queue[g_idx].c_str());
        g_idx++;
        return TWRC_XFERDONE;
    }

    //--- DAT_IMAGELAYOUT (some apps request this) ---------------------------
    if (DAT == DAT_IMAGELAYOUT && MSG == MSG_GET) {
        pTW_IMAGELAYOUT p = (pTW_IMAGELAYOUT)pData;
        p->Frame.Left.Whole=0;  p->Frame.Left.Frac=0;
        p->Frame.Top.Whole=0;   p->Frame.Top.Frac=0;
        p->Frame.Right.Whole=8; p->Frame.Right.Frac=(TW_UINT16)(0.5f*65536);
        p->Frame.Bottom.Whole=11; p->Frame.Bottom.Frac=0;
        p->DocumentNumber = p->PageNumber = p->FrameNumber = 1;
        return TWRC_SUCCESS;
    }

    VSLog(L"Image: unhandled DAT=0x%04x MSG=0x%04x -> TWRC_SUCCESS", DAT, MSG);
    return TWRC_SUCCESS;
}

//=============================================================================
// DS_Entry — the single exported entry point
// Exported as ordinal @1 via .def file
//=============================================================================
extern "C" __declspec(dllexport)
TW_UINT16 FAR __stdcall DS_Entry(
    pTW_IDENTITY /*pOrigin*/,
    pTW_IDENTITY /*pDest*/,
    TW_UINT32    DG,
    TW_UINT16    DAT,
    TW_UINT16    MSG,
    TW_MEMREF    pData)
{
    // Route by DAT first — handles DG=3 "fingerprint" probes from some DSMs
    switch (DAT) {
        case DAT_STATUS:
        case DAT_IDENTITY:
        case DAT_USERINTERFACE:
        case DAT_EVENT:
        case DAT_PENDINGXFERS:
        case DAT_CAPABILITY:
        case DAT_SETUPMEMXFER:
        case DAT_SETUPFILEXFER:
        case DAT_CUSTOMDSDATA:
            return Control(DAT, MSG, pData);
        default: break;
    }
    if (DG == DG_CONTROL) return Control(DAT, MSG, pData);
    if (DG == DG_IMAGE)   return Image(DAT, MSG, pData);

    VSLog(L"DS_Entry: unknown DG=%lu DAT=0x%04x MSG=0x%04x", DG, DAT, MSG);
    return TWRC_SUCCESS;
}
