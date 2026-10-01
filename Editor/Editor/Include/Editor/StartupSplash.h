#pragma once

#ifdef _WIN32
#include <windows.h>
#include <string>
#include <filesystem>
#include <vector>
#include <algorithm>
#include <iostream>

extern "C" {
    unsigned char *stbi_load(char const *filename, int *x, int *y, int *channels_in_file, int desired_channels);
    void stbi_image_free(void *retval_from_stbi_load);
}

class StartupSplash {
public:
    static StartupSplash& Get() {
        static StartupSplash s_instance;
        return s_instance;
    }

    void Show(const std::string& logoPath = "Resources/Icons/eunoia.png") {
        if (m_hwnd) return;

        HINSTANCE hInstance = GetModuleHandleA(NULL);

        WNDCLASSEXA wc = { 0 };
        wc.cbSize = sizeof(WNDCLASSEXA);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = WndProc;
        wc.hInstance = hInstance;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        wc.lpszClassName = "EunoiaStartupSplashClass";

        RegisterClassExA(&wc);

        m_winW = 540;
        m_winH = 340;

        int screenW = GetSystemMetrics(SM_CXSCREEN);
        int screenH = GetSystemMetrics(SM_CYSCREEN);
        int posX = (screenW - m_winW) / 2;
        int posY = (screenH - m_winH) / 2;

        LoadLogo(logoPath);

        m_hwnd = CreateWindowExA(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            "EunoiaStartupSplashClass",
            "Eunoia Engine",
            WS_POPUP | WS_VISIBLE,
            posX, posY, m_winW, m_winH,
            NULL, NULL, hInstance, this
        );

        if (m_hwnd) {
            ShowWindow(m_hwnd, SW_SHOW);
            UpdateWindow(m_hwnd);
            PumpEvents();
        }
    }

    void PumpEvents() {
        if (!m_hwnd) return;
        MSG msg;
        while (PeekMessageA(&msg, m_hwnd, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }

    void Close() {
        if (m_hwnd) {
            DestroyWindow(m_hwnd);
            m_hwnd = nullptr;
        }
        if (m_hBitmap) {
            DeleteObject(m_hBitmap);
            m_hBitmap = nullptr;
        }
        UnregisterClassA("EunoiaStartupSplashClass", GetModuleHandleA(NULL));
    }

    bool IsOpen() const { return m_hwnd != nullptr; }

private:
    StartupSplash() = default;
    ~StartupSplash() { Close(); }

    HWND m_hwnd = nullptr;
    HBITMAP m_hBitmap = nullptr;
    int m_logoW = 0;
    int m_logoH = 0;
    int m_winW = 540;
    int m_winH = 340;

    void LoadLogo(const std::string& path) {
        const char* candidatePaths[] = {
            path.c_str(),
            "Resources/Icons/eunoia.png",
            "../Resources/Icons/eunoia.png",
            "../../Resources/Icons/eunoia.png",
            "Build/Engine/Resources/Icons/eunoia.png"
        };

        unsigned char* data = nullptr;
        int w = 0, h = 0, comp = 0;
        for (const char* p : candidatePaths) {
            std::error_code ec;
            if (std::filesystem::exists(p, ec)) {
                data = stbi_load(p, &w, &h, &comp, 4);
                if (data) break;
            }
        }

        if (!data) return;

        // Target logo size in splash (scaled proportionally, centered)
        int targetW = 160;
        int targetH = (int)((float)h / (float)w * (float)targetW);
        if (targetH > 160) {
            targetH = 160;
            targetW = (int)((float)w / (float)h * (float)targetH);
        }

        BITMAPINFO bmi = { 0 };
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = targetW;
        bmi.bmiHeader.biHeight = -targetH; // Top-down
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void* pBits = nullptr;
        HDC screenDC = GetDC(NULL);
        m_hBitmap = CreateDIBSection(screenDC, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
        ReleaseDC(NULL, screenDC);

        if (m_hBitmap && pBits) {
            m_logoW = targetW;
            m_logoH = targetH;
            uint32_t* dstPixels = (uint32_t*)pBits;

            // Bilinear resize and alpha composite onto black background
            for (int y = 0; y < targetH; ++y) {
                float srcY = ((float)y / (float)targetH) * (float)h;
                int sy = (int)srcY;
                if (sy >= h) sy = h - 1;

                for (int x = 0; x < targetW; ++x) {
                    float srcX = ((float)x / (float)targetW) * (float)w;
                    int sx = (int)srcX;
                    if (sx >= w) sx = w - 1;

                    int srcIdx = (sy * w + sx) * 4;
                    uint8_t r = data[srcIdx + 0];
                    uint8_t g = data[srcIdx + 1];
                    uint8_t b = data[srcIdx + 2];
                    uint8_t a = data[srcIdx + 3];

                    // Premultiplied alpha composite with black (0,0,0)
                    float alphaNorm = (float)a / 255.0f;
                    uint8_t outR = (uint8_t)(r * alphaNorm);
                    uint8_t outG = (uint8_t)(g * alphaNorm);
                    uint8_t outB = (uint8_t)(b * alphaNorm);

                    dstPixels[y * targetW + x] = (outR << 16) | (outG << 8) | outB;
                }
            }
        }

        stbi_image_free(data);
    }

    void OnPaint(HWND hwnd) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rc;
        GetClientRect(hwnd, &rc);

        HBRUSH blackBrush = (HBRUSH)GetStockObject(BLACK_BRUSH);
        FillRect(hdc, &rc, blackBrush);

        if (m_hBitmap && m_logoW > 0 && m_logoH > 0) {
            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, m_hBitmap);

            int dstX = (rc.right - m_logoW) / 2;
            int dstY = (rc.bottom - m_logoH) / 2;

            BitBlt(hdc, dstX, dstY, m_logoW, m_logoH, memDC, 0, 0, SRCCOPY);

            SelectObject(memDC, oldBmp);
            DeleteDC(memDC);
        }

        EndPaint(hwnd, &ps);
    }

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        StartupSplash* self = nullptr;
        if (msg == WM_NCCREATE) {
            CREATESTRUCTA* cs = (CREATESTRUCTA*)lParam;
            self = (StartupSplash*)cs->lpCreateParams;
            SetWindowLongPtrA(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        } else {
            self = (StartupSplash*)GetWindowLongPtrA(hwnd, GWLP_USERDATA);
        }

        switch (msg) {
            case WM_ERASEBKGND:
                return 1;
            case WM_PAINT:
                if (self) {
                    self->OnPaint(hwnd);
                }
                return 0;
            case WM_DESTROY:
                return 0;
            default:
                return DefWindowProcA(hwnd, msg, wParam, lParam);
        }
    }
};

#else
class StartupSplash {
public:
    static StartupSplash& Get() {
        static StartupSplash s_instance;
        return s_instance;
    }
    void Show(const std::string& = "") {}
    void PumpEvents() {}
    void Close() {}
    bool IsOpen() const { return false; }
};
#endif
