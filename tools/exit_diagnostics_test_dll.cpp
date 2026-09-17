#include "Core/ExitDiagnostics.h"

extern "C" __declspec(dllexport) DWORD InitializeTest(const wchar_t* log, const wchar_t* debug)
{
    HANDLE mirror = CreateFileW(debug, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    return InitializeExitDiagnostics(log, mirror);
}
extern "C" __declspec(dllexport) void WindowMessageTest(UINT message, WPARAM wParam, LPARAM lParam)
{
    TraceExitWindowMessage(reinterpret_cast<HWND>(123), message, wParam, lParam);
}
BOOL WINAPI DllMain(HMODULE, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_DETACH)
    {
        TraceExitDiagnostic("DLL_PROCESS_DETACH", reinterpret_cast<ULONG_PTR>(reserved));
        TraceExitDiagnostic("BBCF_IM_Shutdown");
    }
    return TRUE;
}
