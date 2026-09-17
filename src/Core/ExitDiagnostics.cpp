#include "ExitDiagnostics.h"
#include <detours.h>
#include <intrin.h>
#include <tlhelp32.h>

#pragma intrinsic(_ReturnAddress)
#pragma comment(lib, "detours.lib")

namespace
{
    HANDLE g_file = INVALID_HANDLE_VALUE;
    HANDLE g_debug = INVALID_HANDLE_VALUE;
    volatile LONG g_writing = 0;
    volatile LONG g_sequence = 0;
    volatile LONG g_windowEvents = 0;
    DWORD g_pid = 0;
    bool g_initialized = false;

    struct Module { ULONG_PTR base; DWORD size; char name[MAX_PATH]; };
    Module g_modules[256] = {};
    unsigned g_moduleCount = 0;

    // No CRT formatting, allocations, logger mutex, or symbol/loader calls in
    // the exit path. Fixed buffers also remain usable during DLL teardown.
    struct Text
    {
        char bytes[8192];
        DWORD size = 0;
        void Add(const char* s)
        {
            while (*s && size < sizeof(bytes)) bytes[size++] = *s++;
        }
        void Number(ULONGLONG value, unsigned radix = 10)
        {
            char reverse[32]; unsigned n = 0;
            do { reverse[n++] = "0123456789ABCDEF"[value % radix]; value /= radix; } while (value);
            while (n && size < sizeof(bytes)) bytes[size++] = reverse[--n];
        }
        void Field(const char* name, ULONGLONG value, unsigned radix = 10)
        {
            Add(name); if (radix == 16) Add("0x"); Number(value, radix);
        }
    };

    void Write(const Text& text)
    {
        // One kernel write per block; never wait for another logging thread.
        // Windows still has to complete the disk I/O. This is best effort, not
        // protection against a hung disk or an external kill during the write.
        HANDLE files[] = { g_file, g_debug };
        for (HANDLE file : files)
        {
            if (file != INVALID_HANDLE_VALUE && file != nullptr)
            {
                DWORD written = 0;
                WriteFile(file, text.bytes, text.size, &written, nullptr);
                FlushFileBuffers(file);
            }
        }
    }

    void Address(Text& text, const void* address)
    {
        const ULONG_PTR value = reinterpret_cast<ULONG_PTR>(address);
        text.Field(" address=", value, 16);
        for (unsigned i = 0; i < g_moduleCount; ++i)
        {
            const Module& m = g_modules[i];
            if (value >= m.base && value - m.base < m.size)
            {
                text.Add(" module="); text.Add(m.name);
                text.Field(" base=", m.base, 16);
                text.Field(" rva=", value - m.base, 16);
                return;
            }
        }
        MEMORY_BASIC_INFORMATION info = {};
        if (VirtualQuery(address, &info, sizeof(info)) && info.State == MEM_COMMIT)
        {
            const ULONG_PTR base = reinterpret_cast<ULONG_PTR>(info.AllocationBase);
            text.Add(" module=<not-in-startup-map>");
            text.Field(" base=", base, 16); text.Field(" offset=", value - base, 16);
        }
        else text.Add(" module=<unmapped>");
    }

    USHORT Capture(void** frames, DWORD count)
    {
        __try { return CaptureStackBackTrace(0, count, frames, nullptr); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    }

    void CacheModules()
    {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, g_pid);
        if (snapshot == INVALID_HANDLE_VALUE) return;
        MODULEENTRY32W entry = {}; entry.dwSize = sizeof(entry);
        if (Module32FirstW(snapshot, &entry)) do
        {
            Module& m = g_modules[g_moduleCount++];
            m.base = reinterpret_cast<ULONG_PTR>(entry.modBaseAddr);
            m.size = entry.modBaseSize;
            WideCharToMultiByte(CP_UTF8, 0, entry.szModule, -1, m.name, MAX_PATH, nullptr, nullptr);
            m.name[MAX_PATH - 1] = 0;
        } while (g_moduleCount < 256 && Module32NextW(snapshot, &entry));
        CloseHandle(snapshot);
    }

    using ExitProcessFn = void (WINAPI*)(UINT);
    using TerminateProcessFn = BOOL (WINAPI*)(HANDLE, UINT);
    using RtlExitUserProcessFn = void (NTAPI*)(LONG);
    using NtTerminateProcessFn = LONG (NTAPI*)(HANDLE, LONG);
    using PostQuitMessageFn = void (WINAPI*)(int);
    ExitProcessFn g_exit = nullptr;
    TerminateProcessFn g_terminate = nullptr;
    RtlExitUserProcessFn g_rtlExit = nullptr;
    NtTerminateProcessFn g_ntTerminate = nullptr;
    PostQuitMessageFn g_postQuit = nullptr;

    void WINAPI OnExitProcess(UINT code)
    {
        TraceExitDiagnostic("ExitProcess", code, 0, _ReturnAddress());
        g_exit(code);
    }
    BOOL WINAPI OnTerminateProcess(HANDLE process, UINT code)
    {
        // Log the raw handle without querying it or changing its access rights.
        // -1 is self, NULL has native semantics, other handles may be another process.
        TraceExitDiagnostic("TerminateProcess", code, reinterpret_cast<ULONG_PTR>(process), _ReturnAddress());
        const BOOL result = g_terminate(process, code);
        const DWORD error = GetLastError();
        TraceExitDiagnostic("TerminateProcess-return", result, error, _ReturnAddress());
        SetLastError(error);
        return result;
    }
    void NTAPI OnRtlExitUserProcess(LONG status)
    {
        TraceExitDiagnostic("RtlExitUserProcess", static_cast<DWORD>(status), 0, _ReturnAddress());
        g_rtlExit(status);
    }
    LONG NTAPI OnNtTerminateProcess(HANDLE process, LONG status)
    {
        TraceExitDiagnostic("NtTerminateProcess", static_cast<DWORD>(status), reinterpret_cast<ULONG_PTR>(process), _ReturnAddress());
        const LONG result = g_ntTerminate(process, status);
        const DWORD error = GetLastError();
        TraceExitDiagnostic("NtTerminateProcess-return", static_cast<DWORD>(result), 0, _ReturnAddress());
        SetLastError(error);
        return result;
    }
    void WINAPI OnPostQuitMessage(int code)
    {
        TraceExitDiagnostic("PostQuitMessage", static_cast<DWORD>(code), 0, _ReturnAddress());
        g_postQuit(code);
    }

    template<class Function>
    bool Install(const char* module, const char* name, Function hook, Function& original)
    {
        HMODULE owner = GetModuleHandleA(module);
        PBYTE target = owner ? reinterpret_cast<PBYTE>(GetProcAddress(owner, name)) : nullptr;
        original = target ? reinterpret_cast<Function>(DetourFunction(target, reinterpret_cast<PBYTE>(hook))) : nullptr;
        Text text = {};
        text.Add("[EXITDIAG] install api="); text.Add(name);
        text.Field(" target=", reinterpret_cast<ULONG_PTR>(target), 16);
        text.Field(" trampoline=", reinterpret_cast<ULONG_PTR>(original), 16);
        text.Add(original ? " status=installed\r\n" : " status=unavailable\r\n");
        Write(text);
        return original != nullptr;
    }
}

void TraceExitDiagnostic(const char* event, ULONG_PTR arg1, ULONG_PTR arg2, const void* caller)
{
    const DWORD error = GetLastError();
    if (g_file == INVALID_HANDLE_VALUE) { SetLastError(error); return; }
    const LONG sequence = InterlockedIncrement(&g_sequence);
    Text header = {};
    SYSTEMTIME time = {}; GetLocalTime(&time);
    header.Add("[EXITDIAG]"); header.Field(" seq=", sequence);
    header.Field(" pid=", g_pid); header.Field(" tid=", GetCurrentThreadId());
    header.Field(" tick=", GetTickCount64());
    header.Field(" year=", time.wYear); header.Field(" month=", time.wMonth); header.Field(" day=", time.wDay);
    header.Field(" hour=", time.wHour); header.Field(" minute=", time.wMinute);
    header.Field(" second=", time.wSecond); header.Field(" ms=", time.wMilliseconds);
    header.Add(" event="); header.Add(event);
    header.Field(" arg1=", arg1, 16); header.Field(" arg2=", arg2, 16);
    header.Field(" lastError=", error, 16);
    // Store caller even when stack capture cannot unwind an optimized x86 frame.
    Address(header, caller);
    if (InterlockedCompareExchange(&g_writing, 1, 0) != 0)
    {
        header.Add(" stack=skipped-concurrent-or-reentrant\r\n");
        Write(header); SetLastError(error); return;
    }
    header.Add("\r\n");
    Write(header); // Persist the reason BEFORE attempting a stack walk.
    void* frames[32] = {};
    const USHORT count = Capture(frames, 32);
    Text stack = {};
    for (USHORT i = 0; i < count; ++i)
    {
        stack.Add("[EXITDIAG]"); stack.Field(" seq=", sequence); stack.Field(" frame=", i);
        Address(stack, frames[i]); stack.Add("\r\n");
    }
    stack.Add("[EXITDIAG]"); stack.Field(" seq=", sequence); stack.Field(" stackFrames=", count);
    stack.Add("\r\n"); Write(stack);
    InterlockedExchange(&g_writing, 0);
    SetLastError(error);
}

DWORD InitializeExitDiagnostics(const wchar_t* path, HANDLE debugHandle)
{
    if (g_initialized) return 0;
    g_initialized = true;
    g_debug = debugHandle;
    g_pid = GetCurrentProcessId();
    // Append: a relaunch must not erase the only evidence of the prior exit.
    // Archive at 2 MiB on the NEXT launch. Keep one previous file.
    WIN32_FILE_ATTRIBUTE_DATA attr = {};
    if (GetFileAttributesExW(path, GetFileExInfoStandard, &attr) &&
        (attr.nFileSizeHigh || attr.nFileSizeLow >= 2 * 1024 * 1024))
    {
        wchar_t previous[32768]; unsigned n = 0;
        while (path[n] && n < 32758) { previous[n] = path[n]; ++n; }
        const wchar_t suffix[] = L".previous";
        for (unsigned i = 0; i < sizeof(suffix) / sizeof(wchar_t); ++i) previous[n + i] = suffix[i];
        MoveFileExW(path, previous, MOVEFILE_REPLACE_EXISTING);
    }
    g_file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (g_file == INVALID_HANDLE_VALUE)
    {
        if (g_debug != INVALID_HANDLE_VALUE) CloseHandle(g_debug);
        g_debug = INVALID_HANDLE_VALUE;
        return 0;
    }
    LARGE_INTEGER end = {};
    SetFilePointerEx(g_file, end, nullptr, FILE_END);
    CacheModules();
    TraceExitDiagnostic("session-start", g_moduleCount);
    DWORD installed = 0;
    if (Install("kernel32.dll", "ExitProcess", &OnExitProcess, g_exit)) installed |= 1;
    if (Install("kernel32.dll", "TerminateProcess", &OnTerminateProcess, g_terminate)) installed |= 2;
    if (Install("ntdll.dll", "RtlExitUserProcess", &OnRtlExitUserProcess, g_rtlExit)) installed |= 4;
    if (Install("ntdll.dll", "NtTerminateProcess", &OnNtTerminateProcess, g_ntTerminate)) installed |= 8;
    if (Install("user32.dll", "PostQuitMessage", &OnPostQuitMessage, g_postQuit)) installed |= 16;
    TraceExitDiagnostic("hooks-ready", installed);
    return installed;
}

void TraceExitWindowMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    const char* event = nullptr;
    switch (message)
    {
    case WM_CLOSE: event = "WM_CLOSE"; break;
    case WM_DESTROY: event = "WM_DESTROY"; break;
    case WM_NCDESTROY: event = "WM_NCDESTROY"; break;
    case WM_QUERYENDSESSION: event = "WM_QUERYENDSESSION"; break;
    case WM_ENDSESSION: event = "WM_ENDSESSION"; break;
    case WM_SYSCOMMAND: if ((wParam & 0xFFF0) == SC_CLOSE) event = "SC_CLOSE"; break;
    default: break;
    }
    if (!event) return;
    const LONG count = InterlockedIncrement(&g_windowEvents);
    if (count > 32) return;
    TraceExitDiagnostic(event, reinterpret_cast<ULONG_PTR>(hwnd), message, _ReturnAddress());
    TraceExitDiagnostic("window-parameters", wParam, static_cast<ULONG_PTR>(lParam));
    if (count == 32) TraceExitDiagnostic("window-event-limit", 32);
}
