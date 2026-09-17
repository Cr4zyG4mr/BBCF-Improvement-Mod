// Real Win32 child process. The parent verifies exit codes and durable logs.
#include <Windows.h>
#include <cwchar>
extern "C" __declspec(dllimport) DWORD InitializeTest(const wchar_t*, const wchar_t*);
extern "C" __declspec(dllimport) void WindowMessageTest(UINT, WPARAM, LPARAM);

int wmain(int argc, wchar_t** argv)
{
    if (argc != 4) return 90;
    if (InitializeTest(argv[2], argv[3]) != 31) return 91;
    const wchar_t* mode = argv[1];
    if (!wcscmp(mode, L"exit")) ExitProcess(37);
    if (!wcscmp(mode, L"return")) return 38;
    if (!wcscmp(mode, L"terminate")) { TerminateProcess(GetCurrentProcess(), 39); return 92; }
    if (!wcscmp(mode, L"rtl"))
    {
        using Fn = void (NTAPI*)(LONG);
        reinterpret_cast<Fn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlExitUserProcess"))(40);
        return 93;
    }
    if (!wcscmp(mode, L"nt"))
    {
        using Fn = LONG (NTAPI*)(HANDLE, LONG);
        reinterpret_cast<Fn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtTerminateProcess"))(GetCurrentProcess(), 41);
        return 94;
    }
    if (!wcscmp(mode, L"messages"))
    {
        SetLastError(0x1234);
        WindowMessageTest(WM_MOUSEMOVE, 0, 0); // must not produce event or change error
        if (GetLastError() != 0x1234) return 95;
        WindowMessageTest(WM_CLOSE, 0x123, 0x456);
        if (GetLastError() != 0x1234) return 96;
        WindowMessageTest(WM_SYSCOMMAND, SC_CLOSE, 0);
        WindowMessageTest(WM_DESTROY, 0, 0);
        PostQuitMessage(42);
        MSG message = {};
        if (!PeekMessageW(&message, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE) ||
            message.message != WM_QUIT || message.wParam != 42) return 97;
        return 42;
    }
    if (!wcscmp(mode, L"invalid-handle"))
    {
        // Preserve original API failure behavior through both levels of hooks.
        SetLastError(0);
        const BOOL result = TerminateProcess(reinterpret_cast<HANDLE>(0x12345678), 55);
        if (result || GetLastError() != ERROR_INVALID_HANDLE) return 98;
        return 43;
    }
    return 99;
}
