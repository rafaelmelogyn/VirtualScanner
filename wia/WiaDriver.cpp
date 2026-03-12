//=============================================================================
// WiaDriver.cpp - Virtual Scanner WIA 2.0 Minidriver (64-bit)
//=============================================================================
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
using std::min;
using std::max;

#include <objbase.h>
#include <initguid.h>
#include <sti.h>
#include <stiusd.h>
#include <wiamindr.h>
#include <wiautil.h>
#include <wiamdef.h>
#include <propidl.h>
#include <shlwapi.h>
#include <strsafe.h>
#include <gdiplus.h>
#include <new>
#include <vector>
#include <string>

#include "WiaDriver.h"
#include "..\shared\ImageLoader.h"

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "wiaguid.lib")
#pragma comment(lib, "wiaservc.lib")

static HINSTANCE g_hInst  = nullptr;
static LONG      g_cLocks = 0;

BOOL APIENTRY DllMain(HINSTANCE hInst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_hInst = hInst;
        DisableThreadLibraryCalls(hInst);
        CreateDirectoryW(L"C:\\VirtualScanner",        nullptr);
        CreateDirectoryW(L"C:\\VirtualScanner\\Logs",  nullptr);
        CreateDirectoryW(L"C:\\VirtualScanner\\Queue", nullptr);
        VSLog(L"WIA DLL attached PID=%lu", GetCurrentProcessId());
    }
    return TRUE;
}

//=============================================================================
// CWiaDriver
//=============================================================================
CWiaDriver::CWiaDriver()
    : m_cRef(1), m_pRoot(nullptr), m_pChild(nullptr),
      m_hEvent(nullptr), m_queueIdx(0)
{
    InterlockedIncrement(&g_cLocks);
    VSLog(L"CWiaDriver ctor this=%p", this);
}

CWiaDriver::~CWiaDriver()
{
    VSLog(L"CWiaDriver dtor");
    if (m_pRoot)  { m_pRoot->Release();  m_pRoot  = nullptr; }
    if (m_pChild) { m_pChild->Release(); m_pChild = nullptr; }
    InterlockedDecrement(&g_cLocks);
}

STDMETHODIMP_(ULONG) CWiaDriver::AddRef()
{
    ULONG r = InterlockedIncrement(&m_cRef);
    VSLog(L"AddRef -> %lu", r);
    return r;
}
STDMETHODIMP_(ULONG) CWiaDriver::Release()
{
    ULONG r = InterlockedDecrement(&m_cRef);
    VSLog(L"Release -> %lu", r);
    if (!r) delete this;
    return r;
}

STDMETHODIMP CWiaDriver::QueryInterface(REFIID riid, void** ppv)
{
    if (!ppv) return E_POINTER;
    *ppv = nullptr;

    wchar_t guidStr[64] = {};
    StringFromGUID2(riid, guidStr, 64);

    // IMPORTANT: for COM identity with multiple inheritance, IUnknown must
    // always map to the same controlling unknown pointer.
    if (riid == IID_IUnknown || riid == IID_IStiUSD) {
        *ppv = static_cast<IStiUSD*>(this);
    } else if (riid == IID_IWiaMiniDrv) {
        *ppv = static_cast<IWiaMiniDrv*>(this);
    }

    if (*ppv) {
        VSLog(L"QI OK: %s", guidStr);
        AddRef();
        return S_OK;
    }

    VSLog(L"QI NOINTERFACE: %s", guidStr);
    return E_NOINTERFACE;
}

//=============================================================================
// IStiUSD
//=============================================================================
STDMETHODIMP CWiaDriver::Initialize(PSTIDEVICECONTROL pDev, DWORD dwVer, HKEY hKey)
{
    VSLog(L"IStiUSD::Initialize ver=%lu hKey=%p", dwVer, hKey);
    return S_OK;
}

STDMETHODIMP CWiaDriver::GetCapabilities(PSTI_USD_CAPS p)
{
    VSLog(L"GetCapabilities");
    if (!p) return E_POINTER;
    ZeroMemory(p, sizeof(*p));
    p->dwVersion     = STI_VERSION;
    p->dwGenericCaps = STI_USD_GENCAP_NATIVE_PUSHSUPPORT | STI_GENCAP_NOTIFICATIONS;
    return S_OK;
}

STDMETHODIMP CWiaDriver::GetStatus(PSTI_DEVICE_STATUS p)
{
    if (!p) return E_POINTER;
    if (p->StatusMask & STI_DEVSTATUS_ONLINE_STATE)
        p->dwOnlineState = STI_ONLINESTATE_OPERATIONAL;
    if (p->StatusMask & STI_DEVSTATUS_EVENTS_STATE)
        p->dwEventHandlingState = STI_EVENTHANDLING_PENDING;
    return S_OK;
}

STDMETHODIMP CWiaDriver::DeviceReset()  { return S_OK; }
STDMETHODIMP CWiaDriver::Diagnostic(LPSTI_DIAG p)
{
    if (p) { p->dwStatusMask = 0; p->sErrorInfo.dwGenericError = NOERROR; }
    return S_OK;
}
STDMETHODIMP CWiaDriver::Escape(STI_RAW_CONTROL_CODE,LPVOID,DWORD,LPVOID,DWORD,LPDWORD)
    { return E_NOTIMPL; }
STDMETHODIMP CWiaDriver::GetLastError(LPDWORD p) { if(p)*p=0; return S_OK; }
STDMETHODIMP CWiaDriver::LockDevice()   { return S_OK; }
STDMETHODIMP CWiaDriver::UnLockDevice() { return S_OK; }
STDMETHODIMP CWiaDriver::RawReadData(LPVOID,LPDWORD,LPOVERLAPPED)    { return E_NOTIMPL; }
STDMETHODIMP CWiaDriver::RawWriteData(LPVOID,DWORD,LPOVERLAPPED)     { return E_NOTIMPL; }
STDMETHODIMP CWiaDriver::RawReadCommand(LPVOID,LPDWORD,LPOVERLAPPED) { return E_NOTIMPL; }
STDMETHODIMP CWiaDriver::RawWriteCommand(LPVOID,DWORD,LPOVERLAPPED)  { return E_NOTIMPL; }

STDMETHODIMP CWiaDriver::SetNotificationHandle(HANDLE hEvent)
{
    VSLog(L"SetNotificationHandle hEvent=%p", hEvent);
    m_hEvent = hEvent;
    if (hEvent && hEvent != INVALID_HANDLE_VALUE && !VS_ScanQueue().empty())
        SetEvent(hEvent);
    return S_OK;
}
STDMETHODIMP CWiaDriver::GetNotificationData(LPSTINOTIFY p)
{
    if (!p) return E_POINTER;
    ZeroMemory(p, sizeof(*p));
    p->dwSize               = sizeof(STINOTIFY);
    p->guidNotificationCode = WIA_EVENT_SCAN_IMAGE;
    return S_OK;
}
STDMETHODIMP CWiaDriver::GetLastErrorInfo(STI_ERROR_INFO* p)
    { if(p) ZeroMemory(p,sizeof(*p)); return S_OK; }

//=============================================================================
// Item tree
//=============================================================================
HRESULT CWiaDriver::BuildItemTree(BSTR bstrRootName)
{
    VSLog(L"BuildItemTree root=%s", bstrRootName ? bstrRootName : L"(null)");

    if (m_pRoot) {
        VSLog(L"BuildItemTree: already built, skipping");
        return S_OK;
    }

    BSTR bRootName = bstrRootName ? SysAllocString(bstrRootName) : SysAllocString(VS_DEVICE_NAME);
    BSTR bRootFull = bstrRootName ? SysAllocString(bstrRootName) : SysAllocString(VS_DEVICE_NAME);
    if (!bRootName || !bRootFull) {
        if (bRootName) SysFreeString(bRootName);
        if (bRootFull) SysFreeString(bRootFull);
        return E_OUTOFMEMORY;
    }

    HRESULT hr = wiasCreateDrvItem(
        WiaItemTypeRoot | WiaItemTypeDevice | WiaItemTypeFolder,
        bRootName, bRootFull,
        static_cast<IWiaMiniDrv*>(this), 0, nullptr, &m_pRoot);
    SysFreeString(bRootName);
    SysFreeString(bRootFull);
    VSLog(L"wiasCreateDrvItem Root hr=0x%08X m_pRoot=%p", hr, m_pRoot);
    if (FAILED(hr)) return hr;

    wchar_t childFull[512];
    swprintf_s(childFull, L"%s\\Flatbed", bstrRootName ? bstrRootName : L"Root");
    BSTR bName = SysAllocString(L"Flatbed");
    BSTR bFull = SysAllocString(childFull);

    hr = wiasCreateDrvItem(
        WiaItemTypeImage | WiaItemTypeFile |
        WiaItemTypeTransfer | WiaItemTypeGenerated,
        bName, bFull,
        static_cast<IWiaMiniDrv*>(this), 0, nullptr, &m_pChild);
    SysFreeString(bName);
    SysFreeString(bFull);
    VSLog(L"wiasCreateDrvItem Child hr=0x%08X m_pChild=%p", hr, m_pChild);
    if (FAILED(hr)) return hr;

    hr = m_pRoot->AddItemToFolder(m_pChild);
    VSLog(L"AddItemToFolder hr=0x%08X", hr);
    if (FAILED(hr)) return hr;

    VSLog(L"BuildItemTree OK");
    return S_OK;
}

HRESULT CWiaDriver::InitRootProperties(BYTE* pWiasContext)
{
    VSLog(L"InitRootProperties ctx=%p", pWiasContext);
    BSTR bName = SysAllocString(VS_DEVICE_NAME);
    HRESULT hr = wiasWritePropStr(pWiasContext, WIA_DIP_DEV_NAME, bName);
    VSLog(L"  WIA_DIP_DEV_NAME hr=0x%08X", hr);
    SysFreeString(bName);
    LONG devType = StiDeviceTypeScanner;
    hr = wiasWritePropLong(pWiasContext, WIA_DIP_DEV_TYPE, devType);
    VSLog(L"  WIA_DIP_DEV_TYPE hr=0x%08X", hr);
    return S_OK;
}

HRESULT CWiaDriver::InitChildProperties(BYTE* pWiasContext)
{
    VSLog(L"InitChildProperties ctx=%p", pWiasContext);
    HRESULT hr;

    LONG dataTypes[] = { WIA_DATA_COLOR };
    hr = wiasSetValidListLong(pWiasContext, WIA_IPA_DATATYPE, 1, WIA_DATA_COLOR, dataTypes);
    VSLog(L"  WIA_IPA_DATATYPE hr=0x%08X", hr);

    LONG depths[] = { 24 };
    hr = wiasSetValidListLong(pWiasContext, WIA_IPA_DEPTH, 1, 24, depths);
    VSLog(L"  WIA_IPA_DEPTH hr=0x%08X", hr);

    hr = wiasWritePropLong(pWiasContext, WIA_IPA_CHANNELS_PER_PIXEL, 3);
    hr = wiasWritePropLong(pWiasContext, WIA_IPA_BITS_PER_CHANNEL,   8);

    LONG res[] = { 75, 100, 150, 200, 300, 600 };
    hr = wiasSetValidListLong(pWiasContext, WIA_IPS_XRES, ARRAYSIZE(res), 200, res);
    VSLog(L"  WIA_IPS_XRES hr=0x%08X", hr);
    hr = wiasSetValidListLong(pWiasContext, WIA_IPS_YRES, ARRAYSIZE(res), 200, res);

    hr = wiasWritePropLong(pWiasContext, WIA_IPS_XPOS,    0);
    hr = wiasWritePropLong(pWiasContext, WIA_IPS_YPOS,    0);
    hr = wiasWritePropLong(pWiasContext, WIA_IPS_XEXTENT, 1700);
    hr = wiasWritePropLong(pWiasContext, WIA_IPS_YEXTENT, 2200);
    hr = wiasSetValidRangeLong(pWiasContext, WIA_IPS_XEXTENT, 1, 1700, 1, 1700);
    hr = wiasSetValidRangeLong(pWiasContext, WIA_IPS_YEXTENT, 1, 2200, 1, 2200);
    hr = wiasSetValidRangeLong(pWiasContext, WIA_IPS_XPOS,    0,    0, 1, 1700);
    hr = wiasSetValidRangeLong(pWiasContext, WIA_IPS_YPOS,    0,    0, 1, 2200);

    hr = wiasWritePropGuid(pWiasContext, WIA_IPA_FORMAT,           WiaImgFmt_BMP);
    VSLog(L"  WIA_IPA_FORMAT hr=0x%08X", hr);
    hr = wiasWritePropGuid(pWiasContext, WIA_IPA_PREFERRED_FORMAT, WiaImgFmt_BMP);
    hr = wiasWritePropLong(pWiasContext, WIA_IPA_TYMED,            TYMED_FILE);

    LONG comp[] = { WIA_COMPRESSION_NONE };
    hr = wiasSetValidListLong(pWiasContext, WIA_IPA_COMPRESSION, 1, WIA_COMPRESSION_NONE, comp);
    hr = wiasWritePropLong(pWiasContext, WIA_IPA_ITEM_FLAGS,
        WiaItemTypeImage | WiaItemTypeFile | WiaItemTypeTransfer);

    VSLog(L"InitChildProperties done");
    return S_OK;
}

//=============================================================================
// IWiaMiniDrv
//=============================================================================
STDMETHODIMP CWiaDriver::drvInitializeWia(
    BYTE* pCtx, LONG lFlags, BSTR bstrDeviceID, BSTR bstrRootName,
    IUnknown* pStiDevice, IUnknown* pIUnknownOuter,
    IWiaDrvItem** ppRoot, IUnknown** ppInner, LONG* plErr)
{
    VSLog(L"drvInitializeWia flags=0x%08X devID=%s root=%s",
          lFlags,
          bstrDeviceID ? bstrDeviceID : L"(null)",
          bstrRootName ? bstrRootName : L"(null)");
    if (!plErr) return E_POINTER;
    *plErr = 0;
    if (ppInner) *ppInner = nullptr;
    if (!ppRoot) return E_POINTER;
    *ppRoot = nullptr;

    HRESULT hr = BuildItemTree(bstrRootName);
    if (FAILED(hr)) {
        VSLog(L"drvInitializeWia: BuildItemTree FAILED hr=0x%08X", hr);
        return hr;
    }

    *ppRoot    = m_pRoot;
    if (m_pRoot) m_pRoot->AddRef();
    m_queue    = VS_ScanQueue();
    m_queueIdx = 0;
    VSLog(L"drvInitializeWia OK ppRoot=%p queue=%zu", *ppRoot, m_queue.size());
    return S_OK;
}

STDMETHODIMP CWiaDriver::drvInitItemProperties(BYTE* pWiasContext, LONG lFlags, LONG* plErr)
{
    *plErr = 0;
    LONG itemType = 0;
    HRESULT hr = wiasGetItemType(pWiasContext, &itemType);
    VSLog(L"drvInitItemProperties flags=0x%08X wiasGetItemType hr=0x%08X type=0x%08X",
          lFlags, hr, itemType);
    if (FAILED(hr)) {
        // Fallback: assume child
        return InitChildProperties(pWiasContext);
    }
    return (itemType & WiaItemTypeRoot)
        ? InitRootProperties(pWiasContext)
        : InitChildProperties(pWiasContext);
}

STDMETHODIMP CWiaDriver::drvValidateItemProperties(
    BYTE* pWiasContext, LONG, ULONG nPropSpec, const PROPSPEC* pPropSpec, LONG* plErr)
{
    *plErr = 0;
    VSLog(L"drvValidateItemProperties n=%lu", nPropSpec);
    return wiasValidateItemProperties(pWiasContext, nPropSpec, pPropSpec);
}

STDMETHODIMP CWiaDriver::drvWriteItemProperties(BYTE*, LONG, PMINIDRV_TRANSFER_CONTEXT, LONG* plErr)
    { *plErr=0; return S_OK; }
STDMETHODIMP CWiaDriver::drvReadItemProperties(BYTE*, LONG, ULONG, const PROPSPEC*, LONG* plErr)
    { *plErr=0; return S_OK; }
STDMETHODIMP CWiaDriver::drvLockWiaDevice(BYTE*, LONG, LONG* plErr)
    { *plErr=0; VSLog(L"drvLockWiaDevice"); return S_OK; }
STDMETHODIMP CWiaDriver::drvUnLockWiaDevice(BYTE*, LONG, LONG* plErr)
    { *plErr=0; VSLog(L"drvUnLockWiaDevice"); return S_OK; }
STDMETHODIMP CWiaDriver::drvAnalyzeItem(BYTE*, LONG, LONG* plErr)
    { *plErr=0; return E_NOTIMPL; }
STDMETHODIMP CWiaDriver::drvGetDeviceErrorStr(LONG, LONG, LPOLESTR*, LONG* plErr)
    { *plErr=0; return E_NOTIMPL; }
STDMETHODIMP CWiaDriver::drvDeleteItem(BYTE*, LONG, LONG* plErr)
    { *plErr=0; return E_NOTIMPL; }
STDMETHODIMP CWiaDriver::drvFreeDrvItemContext(LONG, BYTE*, LONG* plErr)
    { *plErr=0; return S_OK; }
STDMETHODIMP CWiaDriver::drvNotifyPnpEvent(const GUID* pGuid, BSTR bstrDevID, ULONG ul)
{
    wchar_t g[64] = {};
    if (pGuid) StringFromGUID2(*pGuid, g, 64);
    VSLog(L"drvNotifyPnpEvent guid=%s", g);
    return S_OK;
}
STDMETHODIMP CWiaDriver::drvUnInitializeWia(BYTE*)
    { VSLog(L"drvUnInitializeWia"); return S_OK; }

STDMETHODIMP CWiaDriver::drvDeviceCommand(BYTE*, LONG, const GUID* pCmd,
    IWiaDrvItem**, LONG* plErr)
{
    *plErr = 0;
    if (pCmd && *pCmd == WIA_CMD_SYNCHRONIZE) {
        m_queue = VS_ScanQueue(); m_queueIdx = 0;
        VSLog(L"drvDeviceCommand SYNCHRONIZE: %zu files", m_queue.size());
    }
    return S_OK;
}

STDMETHODIMP CWiaDriver::drvGetCapabilities(BYTE*, LONG, LONG* pcelt,
    WIA_DEV_CAP_DRV** ppCaps, LONG* plErr)
{
    VSLog(L"drvGetCapabilities");
    *plErr=0; *pcelt=0; *ppCaps=nullptr; return S_OK;
}

STDMETHODIMP CWiaDriver::drvGetWiaFormatInfo(BYTE*, LONG, LONG* pcelt,
    WIA_FORMAT_INFO** ppwfi, LONG* plErr)
{
    VSLog(L"drvGetWiaFormatInfo");
    *plErr = 0;
    static WIA_FORMAT_INFO fmts[] = {
        { WiaImgFmt_BMP,       TYMED_FILE     },
        { WiaImgFmt_MEMORYBMP, TYMED_CALLBACK },
    };
    *pcelt = ARRAYSIZE(fmts);
    *ppwfi = fmts;
    return S_OK;
}

static void VS_RefreshQueueForAcquire(std::vector<std::wstring>& queue, int& idx)
{
    if (idx >= (int)queue.size()) {
        std::vector<std::wstring> updated = VS_ScanQueue();
        if (!updated.empty()) {
            queue.swap(updated);
            idx = 0;
            VSLog(L"WIA queue refreshed dynamically: %zu files", queue.size());
        }
    }
}

//=============================================================================
// drvAcquireItemData
//=============================================================================
STDMETHODIMP CWiaDriver::drvAcquireItemData(
    BYTE* pWiasContext, LONG lFlags,
    PMINIDRV_TRANSFER_CONTEXT pmdtc, LONG* plErr)
{
    if (!plErr) return E_POINTER;
    *plErr = 0;
    if (!pmdtc) return E_POINTER;

    VSLog(L"drvAcquireItemData flags=0x%08X tymed=%ld", lFlags, pmdtc->tymed);

    if (m_queueIdx == 0) {
        m_queue = VS_ScanQueue();
        VSLog(L"Queue reloaded: %zu files", m_queue.size());
    }
    VS_RefreshQueueForAcquire(m_queue, m_queueIdx);
    if (m_queue.empty() || m_queueIdx >= (int)m_queue.size()) {
        VSLog(L"No more images idx=%d total=%zu", m_queueIdx, m_queue.size());
        return HRESULT_FROM_WIN32(ERROR_NO_MORE_ITEMS);
    }

    const std::wstring& path = m_queue[m_queueIdx];
    VSLog(L"Delivering [%d/%zu]: %s", m_queueIdx, m_queue.size(), path.c_str());

    int W = 0, H = 0;
    HGLOBAL hDib = VS_LoadAsDIB(path, &W, &H);
    if (!hDib) { VSLog(L"VS_LoadAsDIB failed"); return E_FAIL; }

    DWORD dibSize = (DWORD)GlobalSize(hDib);
    BYTE* pDib    = (BYTE*)GlobalLock(hDib);
    if (!pDib) {
        GlobalFree(hDib);
        VSLog(L"GlobalLock failed for DIB");
        return E_OUTOFMEMORY;
    }

    BITMAPFILEHEADER bfh = {};
    bfh.bfType    = 0x4D42;
    bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    bfh.bfSize    = bfh.bfOffBits + ((BITMAPINFOHEADER*)pDib)->biSizeImage;

    if (pmdtc->tymed == TYMED_FILE) {
        HANDLE hFile = (HANDLE)(ULONG_PTR)pmdtc->hFile;
        if (hFile && hFile != INVALID_HANDLE_VALUE) {
            DWORD w = 0;
            BOOL ok1 = WriteFile(hFile, &bfh, sizeof(bfh), &w, nullptr);
            BOOL ok2 = WriteFile(hFile,  pDib, dibSize,    &w, nullptr);
            if (!ok1 || !ok2) {
                VSLog(L"WriteFile failed err=%lu", ::GetLastError());
                GlobalUnlock(hDib);
                GlobalFree(hDib);
                return HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
            }
            pmdtc->lItemSize = (LONG)bfh.bfSize;
            VSLog(L"Wrote %lu bytes to file", bfh.bfSize);
        }
    } else {
        DWORD total = (DWORD)(sizeof(bfh) + dibSize);
        if (!pmdtc->pTransferBuffer || (DWORD)pmdtc->lBufferSize < total) {
            VSLog(L"Transfer buffer too small: have=%ld need=%lu", pmdtc->lBufferSize, total);
            GlobalUnlock(hDib);
            GlobalFree(hDib);
            return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
        }
        memcpy(pmdtc->pTransferBuffer,               &bfh, sizeof(bfh));
        memcpy(pmdtc->pTransferBuffer + sizeof(bfh),  pDib, dibSize);
        pmdtc->lItemSize = (LONG)total;
        VSLog(L"Wrote %lu bytes to buffer", total);
    }

    GlobalUnlock(hDib);
    GlobalFree(hDib);
    m_queueIdx++;

    if (pWiasContext && pmdtc)
        wiasGetImageInformation(pWiasContext, 0, pmdtc);

    return S_OK;
}

//=============================================================================
// COM exports
//=============================================================================
STDMETHODIMP_(ULONG) CClassFactory::AddRef()
{
    return (ULONG)InterlockedIncrement(&m_cRef);
}

STDMETHODIMP_(ULONG) CClassFactory::Release()
{
    LONG r = InterlockedDecrement(&m_cRef);
    if (r <= 0) {
        m_cRef = 1; // static factory; never delete
        return 1;
    }
    return (ULONG)r;
}

STDMETHODIMP CClassFactory::QueryInterface(REFIID riid, void** ppv)
{
    if (!ppv) return E_POINTER;
    *ppv = nullptr;

    wchar_t guidStr[64] = {};
    StringFromGUID2(riid, guidStr, 64);

    if (riid == IID_IUnknown || riid == IID_IClassFactory) {
        *ppv = static_cast<IClassFactory*>(this);
        AddRef();
        VSLog(L"ClassFactory::QI OK %s", guidStr);
        return S_OK;
    }

    VSLog(L"ClassFactory::QI NOINTERFACE %s", guidStr);
    return E_NOINTERFACE;
}

STDMETHODIMP CClassFactory::CreateInstance(IUnknown* pOuter, REFIID riid, void** ppv)
{
    wchar_t guidStr[64] = {};
    StringFromGUID2(riid, guidStr, 64);
    VSLog(L"CClassFactory::CreateInstance pOuter=%p riid=%s", pOuter, guidStr);

    // Some WIA host flows are strict/quirky with activation flags; don't abort
    // activation only because pOuter is non-null in this virtual driver scenario.
    if (!ppv)   return E_POINTER;
    *ppv = nullptr;

    CWiaDriver* p = new (std::nothrow) CWiaDriver();
    if (!p) return E_OUTOFMEMORY;

    HRESULT hr = p->QueryInterface(riid, ppv);
    VSLog(L"CClassFactory::CreateInstance QI hr=0x%08X out=%p", hr, ppv ? *ppv : nullptr);
    p->Release();
    return hr;
}

STDMETHODIMP CClassFactory::LockServer(BOOL fLock)
{
    if (fLock) InterlockedIncrement(&g_cLocks);
    else       InterlockedDecrement(&g_cLocks);
    return S_OK;
}

static CClassFactory g_ClassFactory;

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv)
{
    wchar_t clsidStr[64] = {};
    wchar_t iidStr[64] = {};
    StringFromGUID2(rclsid, clsidStr, 64);
    StringFromGUID2(riid, iidStr, 64);
    VSLog(L"DllGetClassObject clsid=%s iid=%s", clsidStr, iidStr);
    if (rclsid != CLSID_VirtualScannerWIA) return CLASS_E_CLASSNOTAVAILABLE;
    return g_ClassFactory.QueryInterface(riid, ppv);
}
STDAPI DllCanUnloadNow()      { return g_cLocks == 0 ? S_OK : S_FALSE; }
STDAPI DllRegisterServer()    { return S_OK; }
STDAPI DllUnregisterServer()  { return S_OK; }
