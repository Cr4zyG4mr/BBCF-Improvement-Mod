#pragma once
#include <Windows.h>

// Called once, outside DllMain, while the process is healthy. Takes ownership of
// an optional duplicate of DEBUG's file handle. Both handles live until OS exit.
// No background worker, symbol loading, or gameplay polling.
DWORD InitializeExitDiagnostics(const wchar_t* path, HANDLE debugHandle = INVALID_HANDLE_VALUE);
void TraceExitDiagnostic(const char* event, ULONG_PTR arg1 = 0, ULONG_PTR arg2 = 0,
    const void* caller = nullptr);
void TraceExitWindowMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
