#!/usr/bin/env python3
"""Test the actual diagnostic counters and wrapper after applying workflow patches.

The disposable wrapper harness replaces platform dependencies, not production
logic. Linux alone substitutes the small SEH pointer-read helper; the Windows
test compiles that helper unchanged. The full DLL build validates the real SDK.
"""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[1]
source = repo / 'src/SteamApiWrapper'

with tempfile.TemporaryDirectory(prefix='bbcfn-netdiag-') as folder:
    work = Path(folder)
    for name in ('SteamNetworkingWrapper.h', 'SteamNetworkDiagnostics.h'):
        shutil.copyfile(source / name, work / name)
    implementation = (source / 'SteamNetworkingWrapper.cpp').read_text()
    if os.name != 'nt':
        implementation = implementation.replace('__try { return value ? *value : -1; }',
                                                'return value ? *value : -1;')
        implementation = implementation.replace('__except (EXCEPTION_EXECUTE_HANDLER) { return -1; }', '')
    (work / 'SteamNetworkingWrapper.cpp').write_text(implementation)

    def write(name, text):
        path = work / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)

    write('Windows.h', '''#pragma once
#include <cstdint>
#define interface struct
#define EXCEPTION_EXECUTE_HANDLER 1
using DWORD = std::uint32_t;
extern unsigned long long mockTick;
extern DWORD mockError;
inline unsigned long long GetTickCount64() { return mockTick; }
inline DWORD GetLastError() { return mockError; }
inline void SetLastError(DWORD value) { mockError = value; }
''')
    if os.name != 'nt':
        write('intrin.h', '#pragma once\ninline void* _ReturnAddress() { return nullptr; }\n')
    write('isteamclient.h', '#pragma once\n')
    api = '''#pragma once
#include <cstdint>
using uint32 = std::uint32_t;
using uint16 = std::uint16_t;
using SNetListenSocket_t = uint32;
using SNetSocket_t = uint32;
enum EP2PSend { Unreliable = 0, NoDelay = 1, Reliable = 2, Buffered = 3 };
enum ESNetSocketConnectionType { NotConnected = 0 };
struct CSteamID {
    unsigned long long id;
    CSteamID(unsigned long long value = 0) : id(value) {}
    unsigned long long ConvertToUint64() const { return id; }
};
struct P2PSessionState_t {
    unsigned char m_bConnectionActive = 0, m_bConnecting = 0, m_eP2PSessionError = 0, m_bUsingRelay = 0;
    int m_nBytesQueuedForSend = 0, m_nPacketsQueuedForSend = 0;
};
struct ISteamNetworking {
'''
    # Default implementations only supply the unused API surface. The test's
    # Native overrides exercise production forwarding for the observed methods.
    for return_type, name, arguments in re.findall(
            r'^\t(bool|SNetListenSocket_t|SNetSocket_t|ESNetSocketConnectionType|int) (\w+)\(([^\n]*)\);',
            (source / 'SteamNetworkingWrapper.h').read_text(), re.M):
        api += f'    virtual {return_type} {name}({arguments}) {{ return {{}}; }}\n'
    write('isteamnetworking.h', api + '};\n')
    write('Core/interfaces.h', '''#pragma once
struct TestGameValues { int* pGameMode = nullptr; int* pGameState = nullptr; int* pMatchState = nullptr; };
extern TestGameValues g_gameVals;
''')
    write('Core/logger.h', '''#pragma once
#define LOG(...) do {} while (false)
bool IsLoggingEnabled();
#if defined(__GNUC__)
__attribute__((format(printf, 1, 2)))
#endif
void ForceLog(const char*, ...);
''')
    write('Core/utils.h', '''#pragma once
#include <cstdint>
#include <cstddef>
inline void WriteToProtectedMemory(uintptr_t, char*, size_t) {}
''')
    write('Game/gamestates.h', '#pragma once\n')
    write('Overlay/Logger/ImGuiLogger.h', '#pragma once\n')

    for name, extra in [('network_diagnostics_test', []),
                        ('network_wrapper_test', [str(work / 'SteamNetworkingWrapper.cpp')])]:
        binary = work / (name + ('.exe' if os.name == 'nt' else ''))
        test = str(repo / 'tools' / (name + '.cpp'))
        if os.name == 'nt':
            command = ['cl', '/nologo', '/std:c++17', '/EHsc', '/W3', '/I' + str(work),
                       test, *extra, '/Fe:' + str(binary)]
        else:
            command = ['g++', '-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                       '-Wno-unused-parameter', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                       '-I', str(work), test, *extra, '-pthread', '-o', str(binary)]
        subprocess.run(command, cwd=work, check=True)
        subprocess.run([str(binary)], cwd=work, check=True)
