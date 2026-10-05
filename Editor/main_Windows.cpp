#include "stdafx.h"

#include <fstream>
#include <shellapi.h> // drag n drop
#include <dwmapi.h> // DwmSetWindowAttribute

#pragma comment(lib, "dwmapi.lib")

Editor editor;

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
	wi::arguments::Parse(lpCmdLine);

	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

	static auto WndProc = [](HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) -> LRESULT {
		switch (message)
		{
		case WM_NCCALCSIZE:
			if (wParam == TRUE)
			{
				if (IsZoomed(hWnd))
				{
					HMONITOR hMon = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
					MONITORINFO mi = { sizeof(mi) };
					if (GetMonitorInfo(hMon, &mi))
					{
						NCCALCSIZE_PARAMS* p = (NCCALCSIZE_PARAMS*)lParam;
						p->rgrc[0] = mi.rcWork;
					}
					return 0;
				}
				return 0;
			}
			return 0;

		case WM_GETMINMAXINFO:
		{
			MINMAXINFO* mmi = (MINMAXINFO*)lParam;
			HMONITOR hMon = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
			MONITORINFO mi = { sizeof(mi) };
			if (GetMonitorInfo(hMon, &mi))
			{
				mmi->ptMaxPosition.x = mi.rcWork.left - mi.rcMonitor.left;
				mmi->ptMaxPosition.y = mi.rcWork.top - mi.rcMonitor.top;
				mmi->ptMaxSize.x = mi.rcWork.right - mi.rcWork.left;
				mmi->ptMaxSize.y = mi.rcWork.bottom - mi.rcWork.top;
			}
			mmi->ptMinTrackSize.x = 800;
			mmi->ptMinTrackSize.y = 500;
			return 0;
		}

		case WM_NCHITTEST:
		{
			if (!IsZoomed(hWnd))
			{
				POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
				RECT rc;
				GetWindowRect(hWnd, &rc);

				const int BORDER_SIZE = 7;
				bool left   = (pt.x >= rc.left && pt.x < rc.left + BORDER_SIZE);
				bool right  = (pt.x <= rc.right && pt.x > rc.right - BORDER_SIZE);
				bool top    = (pt.y >= rc.top && pt.y < rc.top + BORDER_SIZE);
				bool bottom = (pt.y <= rc.bottom && pt.y > rc.bottom - BORDER_SIZE);

				if (top && left)     return HTTOPLEFT;
				if (top && right)    return HTTOPRIGHT;
				if (bottom && left)  return HTBOTTOMLEFT;
				if (bottom && right) return HTBOTTOMRIGHT;
				if (left)            return HTLEFT;
				if (right)           return HTRIGHT;
				if (top)             return HTTOP;
				if (bottom)          return HTBOTTOM;
			}
			break;
		}

		case WM_NCACTIVATE:
			return TRUE;
		}

		if (ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam))
			return true;

		switch (message)

		{
		case WM_SIZE:
		case WM_DPICHANGED:
			if (editor.is_window_active && LOWORD(lParam) > 0 && HIWORD(lParam) > 0)
			{
				editor.SetWindow(hWnd);
				editor.SaveWindowSize();
			}
			break;
		case WM_CHAR:
			switch (wParam)
			{
			case VK_BACK:
				wi::gui::TextInputField::DeleteFromInput();
				break;
			case VK_RETURN:
				break;
			default:
				const wchar_t c = (const wchar_t)wParam;
				wi::gui::TextInputField::AddInput(c);
				break;
			}
			break;
		case WM_INPUT:
			if (editor.is_window_active)
			{
				wi::input::rawinput::ParseMessage((void*)lParam);
			}
			break;
		case WM_POINTERUPDATE:
		{
			POINTER_PEN_INFO pen_info = {};
			if (GetPointerPenInfo(GET_POINTERID_WPARAM(wParam), &pen_info))
			{
				ScreenToClient(hWnd, &pen_info.pointerInfo.ptPixelLocation);
				const float dpiscaling = (float)GetDpiForWindow(hWnd) / 96.0f;
				wi::input::Pen pen;
				pen.position = XMFLOAT2(pen_info.pointerInfo.ptPixelLocation.x / dpiscaling, pen_info.pointerInfo.ptPixelLocation.y / dpiscaling);
				pen.pressure = float(pen_info.pressure) / 1024.0f;
				wi::input::SetPen(pen);
			}
		}
		break;
		case WM_KILLFOCUS:
			editor.is_window_active = false;
			break;
		case WM_SETFOCUS:
			editor.is_window_active = true;
			editor.HotReload();
			break;
		case WM_DROPFILES:
		{
			HDROP hdrop = (HDROP)wParam;
			UINT filecount = DragQueryFile(hdrop, 0xFFFFFFFF, nullptr, 0);
			assert(filecount != 0);
			for (UINT i = 0; i < filecount; ++i)
			{
				wchar_t wfilename[1024] = {};
				UINT res = DragQueryFile(hdrop, i, wfilename, arraysize(wfilename));
				if (res == 0)
				{
					assert(0);
					continue;
				}
				std::string filename;
				wi::helper::StringConvert(wfilename, filename);
				editor.renderComponent.Open(filename);
			}
			SetForegroundWindow(hWnd);
		}
		break;
		case WM_CLOSE:
			editor.Exit();
			break;
		case WM_DESTROY:
			PostQuitMessage(0);
			break;
		case WM_SETCURSOR:
			switch (LOWORD(lParam))
			{
			case HTBOTTOM:
			case HTBOTTOMLEFT:
			case HTBOTTOMRIGHT:
			case HTLEFT:
			case HTRIGHT:
			case HTTOP:
			case HTTOPLEFT:
			case HTTOPRIGHT:
				// allow the system to handle these window resize cursors:
				return DefWindowProc(hWnd, message, wParam, lParam);
			default:
				// notify the engine at other cursor changes to set its own cursor instead
				wi::input::NotifyCursorChanged();
				break;
			}
			break;
		case WM_SYSCHAR:
		case WM_SYSKEYDOWN:
		case WM_SYSKEYUP:
			if (editor.config.GetBool("bypass_system_key") && wParam != VK_F4)
			{
				// Handle system key messages (like Alt key) to prevent menu bar activation freeze
				// Return 0 to indicate we processed the message and prevent default behavior
				break;
			}
			return DefWindowProc(hWnd, message, wParam, lParam);
		case WM_SETTINGCHANGE:
			{
				// Change window theme dark mode based on system setting:
				HKEY hKey;
				DWORD value = 1; // Default to light mode (1 = light, 0 = dark)
				DWORD dataSize = sizeof(value);
				if (RegOpenKeyEx(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
				{
					RegQueryValueEx(hKey, L"AppsUseLightTheme", NULL, NULL, (LPBYTE)&value, &dataSize);
					RegCloseKey(hKey);
				}
				BOOL darkmode = value == 0;
				DwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkmode, sizeof(darkmode));
			}
			break;
		default:
			return DefWindowProc(hWnd, message, wParam, lParam);
		}
		return 0;
	};

	WNDCLASSEXW wcex = {};
	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = WndProc;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = hInstance;
	wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(1001)); // 1001 = icon from Resource.rc file
	wcex.hIconSm = NULL;
	wcex.hCursor = LoadCursor(hInstance, IDC_ARROW);
	wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wcex.lpszMenuName = NULL;
	wchar_t wname[256] = {};
	wi::helper::StringConvert(wi::helper::StringRemoveTrailingWhitespaces(exe_customization.name_padded).c_str(), wname, arraysize(wname));
	wcex.lpszClassName = wname;
	RegisterClassExW(&wcex);



	int width = CW_USEDEFAULT;
	int height = 0;
	bool fullscreen = false;
	bool borderless = false;

	wi::Timer timer;
	if (editor.config.Open("config.ini"))
	{
		if (editor.config.Has("width"))
		{
			width = editor.config.GetInt("width");
			height = editor.config.GetInt("height");
		}
		fullscreen = editor.config.GetBool("fullscreen");
		borderless = editor.config.GetBool("borderless");
		editor.allow_hdr = editor.config.GetBool("allow_hdr");
		if (editor.config.GetBool("window_maximized"))
		{
			nCmdShow = SW_MAXIMIZE;
		}

		wilog("config.ini loaded in %.2f milliseconds\n", (float)timer.elapsed_milliseconds());
	}

	HWND hWnd = NULL;

	if (borderless || fullscreen)
	{
		width = std::max(100, width);
		height = std::max(100, height);

		hWnd = CreateWindowEx(
			WS_EX_APPWINDOW,
			wcex.lpszClassName,
			L"Eunoia Engine",
			WS_POPUP,
			CW_USEDEFAULT, 0, width, height,
			NULL,
			NULL,
			hInstance,
			NULL
		);
	}
	else
	{
		hWnd = CreateWindow(
			wcex.lpszClassName,
			L"Eunoia Engine",
			WS_OVERLAPPEDWINDOW,
			CW_USEDEFAULT, 0, width, height,
			NULL,
			NULL,
			hInstance,
			NULL
		);
	}
	if (hWnd == NULL)
	{
		wilog_error("Win32 window creation failure!");
		return -1;
	}

	MARGINS margins = { 1, 1, 1, 1 };
	DwmExtendFrameIntoClientArea(hWnd, &margins);
	SetWindowPos(hWnd, NULL, 0, 0, 0, 0,
		SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
	if (fullscreen)
	{
		HMONITOR monitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
		MONITORINFO info;
		info.cbSize = sizeof(MONITORINFO);
		GetMonitorInfo(monitor, &info);
		width = info.rcMonitor.right - info.rcMonitor.left;
		height = info.rcMonitor.bottom - info.rcMonitor.top;
		MoveWindow(hWnd, 0, 0, width, height, FALSE);
	}
	SendMessage(hWnd, WM_SETTINGCHANGE, 0, 0); // trigger dark mode theme detection
	ShowWindow(hWnd, nCmdShow);
	UpdateWindow(hWnd);
	DragAcceptFiles(hWnd, TRUE);
	SetForegroundWindow(hWnd);

	editor.SetWindow(hWnd);

	MSG msg = { 0 };

	while (editor.KeepRunning())
	{
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else {

			editor.Run();

		}
	}

	wi::jobsystem::ShutDown();

    return (int)msg.wParam;
}
