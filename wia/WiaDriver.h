#pragma once
//=============================================================================
// WiaDriver.h - Virtual Scanner WIA 2.0 Minidriver (64-bit)
// Strategy: uses wiautil.h inline helpers where possible to avoid
// hard dependency on wiaservc.lib (which requires full WDK)
//=============================================================================
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
using std::min;
using std::max;

#include <objbase.h>
#include <initguid.h>

// STI
#include <sti.h>
#include <stiusd.h>

// WIA
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

#include "..\shared\ImageLoader.h"

// {A1B2C3D4-E5F6-7890-ABCD-EF1234567890}
DEFINE_GUID(CLSID_VirtualScannerWIA,
    0xA1B2C3D4, 0xE5F6, 0x7890,
    0xAB, 0xCD, 0xEF, 0x12, 0x34, 0x56, 0x78, 0x90);

//-----------------------------------------------------------------------------
class CWiaDriver : public IStiUSD, public IWiaMiniDrv
{
public:
    CWiaDriver();
    ~CWiaDriver();

    STDMETHODIMP         QueryInterface(REFIID riid, void** ppv);
    STDMETHODIMP_(ULONG) AddRef();
    STDMETHODIMP_(ULONG) Release();

    // IStiUSD
    STDMETHODIMP Initialize(PSTIDEVICECONTROL, DWORD, HKEY);
    STDMETHODIMP GetCapabilities(PSTI_USD_CAPS);
    STDMETHODIMP GetStatus(PSTI_DEVICE_STATUS);
    STDMETHODIMP DeviceReset();
    STDMETHODIMP Diagnostic(LPSTI_DIAG);
    STDMETHODIMP Escape(STI_RAW_CONTROL_CODE, LPVOID, DWORD, LPVOID, DWORD, LPDWORD);
    STDMETHODIMP GetLastError(LPDWORD);
    STDMETHODIMP LockDevice();
    STDMETHODIMP UnLockDevice();
    STDMETHODIMP RawReadData(LPVOID, LPDWORD, LPOVERLAPPED);
    STDMETHODIMP RawWriteData(LPVOID, DWORD, LPOVERLAPPED);
    STDMETHODIMP RawReadCommand(LPVOID, LPDWORD, LPOVERLAPPED);
    STDMETHODIMP RawWriteCommand(LPVOID, DWORD, LPOVERLAPPED);
    STDMETHODIMP SetNotificationHandle(HANDLE);
    STDMETHODIMP GetNotificationData(LPSTINOTIFY);
    STDMETHODIMP GetLastErrorInfo(STI_ERROR_INFO*);

    // IWiaMiniDrv
    STDMETHODIMP drvInitializeWia(BYTE*, LONG, BSTR, BSTR, IUnknown*, IUnknown*,
                                  IWiaDrvItem**, IUnknown**, LONG*);
    STDMETHODIMP drvAcquireItemData(BYTE*, LONG, PMINIDRV_TRANSFER_CONTEXT, LONG*);
    STDMETHODIMP drvInitItemProperties(BYTE*, LONG, LONG*);
    STDMETHODIMP drvValidateItemProperties(BYTE*, LONG, ULONG, const PROPSPEC*, LONG*);
    STDMETHODIMP drvWriteItemProperties(BYTE*, LONG, PMINIDRV_TRANSFER_CONTEXT, LONG*);
    STDMETHODIMP drvReadItemProperties(BYTE*, LONG, ULONG, const PROPSPEC*, LONG*);
    STDMETHODIMP drvLockWiaDevice(BYTE*, LONG, LONG*);
    STDMETHODIMP drvUnLockWiaDevice(BYTE*, LONG, LONG*);
    STDMETHODIMP drvAnalyzeItem(BYTE*, LONG, LONG*);
    STDMETHODIMP drvGetDeviceErrorStr(LONG, LONG, LPOLESTR*, LONG*);
    STDMETHODIMP drvDeviceCommand(BYTE*, LONG, const GUID*, IWiaDrvItem**, LONG*);
    STDMETHODIMP drvGetCapabilities(BYTE*, LONG, LONG*, WIA_DEV_CAP_DRV**, LONG*);
    STDMETHODIMP drvDeleteItem(BYTE*, LONG, LONG*);
    STDMETHODIMP drvFreeDrvItemContext(LONG, BYTE*, LONG*);
    STDMETHODIMP drvGetWiaFormatInfo(BYTE*, LONG, LONG*, WIA_FORMAT_INFO**, LONG*);
    STDMETHODIMP drvNotifyPnpEvent(const GUID*, BSTR, ULONG);
    STDMETHODIMP drvUnInitializeWia(BYTE*);

private:
    volatile LONG             m_cRef;
    IWiaDrvItem*              m_pRoot;
    IWiaDrvItem*              m_pChild;
    HANDLE                    m_hEvent;
    std::vector<std::wstring> m_queue;
    int                       m_queueIdx;

    HRESULT BuildItemTree(BSTR bstrRootName);
    HRESULT InitRootProperties(BYTE* pWiasContext);
    HRESULT InitChildProperties(BYTE* pWiasContext);
};

class CClassFactory : public IClassFactory
{
public:
    CClassFactory() : m_cRef(1) {}

    STDMETHODIMP         QueryInterface(REFIID riid, void** ppv);
    STDMETHODIMP_(ULONG) AddRef();
    STDMETHODIMP_(ULONG) Release();
    STDMETHODIMP CreateInstance(IUnknown* pOuter, REFIID riid, void** ppv);
    STDMETHODIMP LockServer(BOOL fLock);

private:
    volatile LONG m_cRef;
};
