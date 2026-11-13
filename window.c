#include "window.h"

#include <stdio.h>
#include <stdlib.h>

static const wchar_t CLASS_NAME[] = L"stereo_probe";

static LRESULT CALLBACK
window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	if (msg == WM_CLOSE) {
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void
window_register_class(void)
{
	WNDCLASSW wc = {0};
	wc.lpfnWndProc = window_proc;
	wc.hInstance = GetModuleHandleW(NULL);
	wc.lpszClassName = CLASS_NAME;
	wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
	RegisterClassW(&wc);
}

HWND
window_create(const wchar_t *title, DWORD style, int x, int y, int width, int height)
{
	HWND hwnd = CreateWindowExW(0, CLASS_NAME, title, style | WS_VISIBLE, x, y, width, height, NULL,
	                            NULL, GetModuleHandleW(NULL), NULL);
	if (hwnd == NULL) {
		fprintf(stderr, "CreateWindowExW failed: %lu\n", GetLastError());
		exit(1);
	}
	return hwnd;
}

bool
window_pump_messages(void)
{
	bool keep_running = true;
	MSG msg;
	while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
		if (msg.message == WM_QUIT || (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE)) {
			keep_running = false;
		}
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}
	return keep_running;
}
