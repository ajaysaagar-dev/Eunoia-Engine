#pragma once

#ifdef _WIN32
#include <windows.h>
#include <string>
#include <filesystem>
#include <vector>
#include <algorithm>
#include <iostream>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>

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
        if (m_isRunning) return;

        m_logoPath = logoPath;
        m_progress.store(0.05f);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_statusText = "Initializing Eunoia Engine...";
        }
        m_shouldClose.store(false);
        m_isRunning.store(true);

        m_splashThread = std::thread(&StartupSplash::ThreadMain, this);

        // Wait briefly until HWND is created
        for (int i = 0; i < 50 && !m_hwnd; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    void UpdateProgress(float progress, const std::string& statusText) {
        m_progress.store(std::clamp(progress, 0.0f, 1.0f));
        if (!statusText.empty()) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_statusText = statusText;
        }
        if (m_hwnd) {
            PostMessageA(m_hwnd, WM_USER + 1, 0, 0);
        }
    }

    void PumpEvents() {
        // Handled automatically on dedicated splash thread
    }

    void Close() {
        if (!m_isRunning) return;
        m_shouldClose.store(true);
        if (m_hwnd) {
            PostMessageA(m_hwnd, WM_CLOSE, 0, 0);
        }
        if (m_splashThread.joinable()) {
            m_splashThread.join();
        }
        m_isRunning.store(false);
    }

    bool IsOpen() const { return m_isRunning.load() && m_hwnd != nullptr; }

private:
    StartupSplash() = default;
    ~StartupSplash() { Close(); }

    std::atomic<bool> m_isRunning{ false };
    std::atomic<bool> m_shouldClose{ false };
    std::thread m_splashThread;

    std::string m_logoPath;
    HWND m_hwnd = nullptr;
    HBITMAP m_hBitmap = nullptr;
    int m_logoW = 0;
    int m_logoH = 0;
    int m_winW = 560;
    int m_winH = 360;

    std::mutex m_mutex;
    std::atomic<float> m_progress{ 0.0f };
    std::string m_statusText;

    void ThreadMain() {
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

        m_winW = 560;
        m_winH = 360;

        int screenW = GetSystemMetrics(SM_CXSCREEN);
        int screenH = GetSystemMetrics(SM_CYSCREEN);
        int posX = (screenW - m_winW) / 2;
        int posY = (screenH - m_winH) / 2;

        LoadLogo(m_logoPath);

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
        }

        MSG msg;
        while (!m_shouldClose.load()) {
            while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT || msg.message == WM_CLOSE) {
                    m_shouldClose.store(true);
                    break;
                }
                TranslateMessage(&msg);
                DispatchMessageA(&msg);
            }

            if (m_hwnd) {
                InvalidateRect(m_hwnd, NULL, FALSE);
                UpdateWindow(m_hwnd);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }

        if (m_hwnd) {
            DestroyWindow(m_hwnd);
            m_hwnd = nullptr;
        }
        if (m_hBitmap) {
            DeleteObject(m_hBitmap);
            m_hBitmap = nullptr;
        }
        UnregisterClassA("EunoiaStartupSplashClass", hInstance);
    }

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

        HDC memHdc = CreateCompatibleDC(hdc);
        HBITMAP memBmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HBITMAP oldMemBmp = (HBITMAP)SelectObject(memHdc, memBmp);

        // 1. Pure black background
        HBRUSH blackBrush = (HBRUSH)GetStockObject(BLACK_BRUSH);
        FillRect(memHdc, &rc, blackBrush);

        int statusBarHeight = 38;
        int mainAreaHeight = rc.bottom - statusBarHeight;

        // 2. Centered Logo
        if (m_hBitmap && m_logoW > 0 && m_logoH > 0) {
            HDC logoDC = CreateCompatibleDC(memHdc);
            HBITMAP oldLogoBmp = (HBITMAP)SelectObject(logoDC, m_hBitmap);

            int dstX = (rc.right - m_logoW) / 2;
            int dstY = (mainAreaHeight - m_logoH) / 2;

            BitBlt(memHdc, dstX, dstY, m_logoW, m_logoH, logoDC, 0, 0, SRCCOPY);

            SelectObject(logoDC, oldLogoBmp);
            DeleteDC(logoDC);
        }

        // 3. Status Bar at the bottom in #1e1e1e
        RECT barRect;
        barRect.left = 0;
        barRect.top = mainAreaHeight;
        barRect.right = rc.right;
        barRect.bottom = rc.bottom;

        HBRUSH barBgBrush = CreateSolidBrush(RGB(0x1E, 0x1E, 0x1E));
        FillRect(memHdc, &barRect, barBgBrush);
        DeleteObject(barBgBrush);

        HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(45, 45, 48));
        HPEN oldPen = (HPEN)SelectObject(memHdc, borderPen);
        MoveToEx(memHdc, 0, barRect.top, NULL);
        LineTo(memHdc, rc.right, barRect.top);
        SelectObject(memHdc, oldPen);
        DeleteObject(borderPen);

        float curProgress = m_progress.load();
        std::string curStatus;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            curStatus = m_statusText;
        }

        if (curProgress > 0.001f) {
            int fillW = (int)((float)rc.right * curProgress);
            if (fillW > 0) {
                RECT fillRect;
                fillRect.left = 0;
                fillRect.top = barRect.top;
                fillRect.right = fillW;
                fillRect.bottom = barRect.top + 3;

                HBRUSH fillBrush = CreateSolidBrush(RGB(14, 112, 224));
                FillRect(memHdc, &fillRect, fillBrush);
                DeleteObject(fillBrush);
            }
        }

        if (!curStatus.empty()) {
            HFONT hFont = CreateFontA(
                15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI"
            );
            HFONT oldFont = (HFONT)SelectObject(memHdc, hFont);

            SetBkMode(memHdc, TRANSPARENT);
            SetTextColor(memHdc, RGB(200, 205, 215));

            RECT textRect = barRect;
            textRect.left += 16;
            textRect.right -= 80;
            textRect.top += 4;

            DrawTextA(memHdc, curStatus.c_str(), -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

            char pctStr[16];
            snprintf(pctStr, sizeof(pctStr), "%d%%", (int)(curProgress * 100.0f));
            SetTextColor(memHdc, RGB(0, 190, 255));
            RECT pctRect = barRect;
            pctRect.left = rc.right - 70;
            pctRect.right = rc.right - 16;
            pctRect.top += 4;
            DrawTextA(memHdc, pctStr, -1, &pctRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

            SelectObject(memHdc, oldFont);
            DeleteObject(hFont);
        }

        BitBlt(hdc, 0, 0, rc.right, rc.bottom, memHdc, 0, 0, SRCCOPY);

        SelectObject(memHdc, oldMemBmp);
        DeleteObject(memBmp);
        DeleteDC(memHdc);

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
            case WM_USER + 1:
                if (hwnd) InvalidateRect(hwnd, NULL, FALSE);
                return 0;
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
    void UpdateProgress(float, const std::string&) {}
    void PumpEvents() {}
    void Close() {}
    bool IsOpen() const { return false; }
};
#endif
