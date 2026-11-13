#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdbool.h>

void
window_register_class(void);

// Exits the process when the window cannot be created.
HWND
window_create(const wchar_t *title, DWORD style, int x, int y, int width, int height);

// Dispatches pending messages. Returns false once the window was closed or
// Escape was pressed.
bool
window_pump_messages(void);
