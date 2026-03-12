#pragma once
//=============================================================================
// ImageLoader.h  - shared image loading + queue scanning
// Supports: BMP, JPG, JPEG, PNG, TIF, TIFF
// Output:   packed DIB (BITMAPINFOHEADER + bottom-up 24bpp) in HGLOBAL
//=============================================================================

// NOTE: Do NOT define NOMINMAX before including gdiplus.h
// GDI+ internally uses min/max from <algorithm>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>   // must come BEFORE gdiplus.h so min/max are defined
using std::min;
using std::max;
#include <gdiplus.h>
#include <cstdarg>
#include <vector>
#include <string>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

#define VS_QUEUE_DIR   L"C:\\VirtualScanner\\Queue\\"
#define VS_QUEUE_DIR_LEGACY_TYPO L"C:\\VirtulScanner\\Queue\\"
#define VS_LOG_PATH    L"C:\\VirtualScanner\\Logs\\vscanner.log"
#define VS_DEVICE_NAME L"VirtualScanner ADS-4700W"

//-----------------------------------------------------------------------------
// Logging
//-----------------------------------------------------------------------------
inline void VSLog(const wchar_t* fmt, ...)
{
    CreateDirectoryW(L"C:\\VirtualScanner",        nullptr);
    CreateDirectoryW(L"C:\\VirtualScanner\\Logs",  nullptr);

    wchar_t buf[2048] = {};
    va_list va; va_start(va, fmt); vswprintf_s(buf, fmt, va); va_end(va);

    HANDLE hf = CreateFileW(VS_LOG_PATH, GENERIC_WRITE, FILE_SHARE_READ,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hf == INVALID_HANDLE_VALUE) return;

    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t line[2200];
    swprintf_s(line, L"[%02d:%02d:%02d] %s\r\n",
        st.wHour, st.wMinute, st.wSecond, buf);

    SetFilePointer(hf, 0, nullptr, FILE_END);
    DWORD w;
    WriteFile(hf, line, (DWORD)(wcslen(line) * sizeof(wchar_t)), &w, nullptr);
    CloseHandle(hf);
}

//-----------------------------------------------------------------------------
// Scan the queue directory for image files
//-----------------------------------------------------------------------------
inline std::vector<std::wstring> VS_ScanQueue()
{
    std::vector<std::wstring> result;
    const wchar_t* exts[] = {
        L"*.bmp", L"*.jpg", L"*.jpeg",
        L"*.png", L"*.tif", L"*.tiff", nullptr
    };

    const wchar_t* dirs[] = { VS_QUEUE_DIR, VS_QUEUE_DIR_LEGACY_TYPO, nullptr };
    for (int d = 0; dirs[d]; ++d) {
        for (int i = 0; exts[i]; i++) {
            std::wstring pat = std::wstring(dirs[d]) + exts[i];
            WIN32_FIND_DATAW fd;
            HANDLE h = FindFirstFileW(pat.c_str(), &fd);
            if (h != INVALID_HANDLE_VALUE) {
                do {
                    if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                        result.push_back(std::wstring(dirs[d]) + fd.cFileName);
                } while (FindNextFileW(h, &fd));
                FindClose(h);
            }
        }
    }

    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

//-----------------------------------------------------------------------------
// Load any image as packed DIB (BITMAPINFOHEADER + 24bpp bottom-up pixels)
//-----------------------------------------------------------------------------
inline HGLOBAL VS_LoadAsDIB(const std::wstring& path, int* outW = nullptr, int* outH = nullptr)
{
    ULONG_PTR gdipToken = 0;
    Gdiplus::GdiplusStartupInput si;
    Gdiplus::GdiplusStartup(&gdipToken, &si, nullptr);

    HGLOBAL hDib = nullptr;

    {
        Gdiplus::Bitmap bmp(path.c_str());
        if (bmp.GetLastStatus() != Gdiplus::Ok) {
            VSLog(L"GDI+ failed: %s  status=%d", path.c_str(), (int)bmp.GetLastStatus());
            goto done;
        }

        UINT W = bmp.GetWidth();
        UINT H = bmp.GetHeight();
        if (outW) *outW = (int)W;
        if (outH) *outH = (int)H;
        if (!W || !H) { VSLog(L"Zero size: %s", path.c_str()); goto done; }

        Gdiplus::BitmapData bd = {};
        Gdiplus::Rect rc(0, 0, W, H);
        if (bmp.LockBits(&rc, Gdiplus::ImageLockModeRead,
                         PixelFormat24bppRGB, &bd) != Gdiplus::Ok) {
            VSLog(L"LockBits failed");
            goto done;
        }

        DWORD rowStride = ((W * 3 + 3) & ~3);
        DWORD pixBytes  = rowStride * H;
        DWORD total     = sizeof(BITMAPINFOHEADER) + pixBytes;

        hDib = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, total);
        if (hDib) {
            BYTE* dst = (BYTE*)GlobalLock(hDib);
            BITMAPINFOHEADER& bih = *(BITMAPINFOHEADER*)dst;
            bih.biSize          = sizeof(BITMAPINFOHEADER);
            bih.biWidth         = (LONG)W;
            bih.biHeight        = (LONG)H;  // positive = bottom-up
            bih.biPlanes        = 1;
            bih.biBitCount      = 24;
            bih.biCompression   = BI_RGB;
            bih.biSizeImage     = pixBytes;
            bih.biXPelsPerMeter = 5906;
            bih.biYPelsPerMeter = 5906;

            // GDI+ = top-down; DIB = bottom-up
            BYTE* pixDst = dst + sizeof(BITMAPINFOHEADER);
            for (UINT row = 0; row < H; row++) {
                const BYTE* src = (const BYTE*)bd.Scan0 + row * abs(bd.Stride);
                BYTE* drow = pixDst + (H - 1 - row) * rowStride;
                memcpy(drow, src, W * 3);
            }
            GlobalUnlock(hDib);
        }
        bmp.UnlockBits(&bd);
        VSLog(L"Loaded %ux%u: %s", W, H, path.c_str());
    }

done:
    Gdiplus::GdiplusShutdown(gdipToken);
    return hDib;
}
