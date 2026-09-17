#!/usr/bin/env python3
"""Compile production exit tracing and exercise actual Windows process exits.

Each mode runs in a disposable child with a timeout. Checks API forwarding,
normal return/ExitProcess, direct native exits, hard self-termination, real DLL
detach, window filtering, PostQuitMessage semantics, last-error preservation,
and persistence in both raw file handles. No game or Steam required.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[1]
if os.name != 'nt':
    raise SystemExit('This integration test requires the Win32 MSVC environment.')

with tempfile.TemporaryDirectory(prefix='bbcf-exitdiag-') as folder:
    work = Path(folder)
    common = ['cl', '/nologo', '/MT', '/EHsc', '/std:c++17', '/O2', '/Oy-', '/W4',
              '/I' + str(repo / 'src'), '/I' + str(repo / 'depends/detour')]
    subprocess.run(common + ['/LD', str(repo / 'src/Core/ExitDiagnostics.cpp'),
                            str(repo / 'tools/exit_diagnostics_test_dll.cpp'),
                            '/Fe:exit_test.dll', '/link', '/SAFESEH:NO', '/IMPLIB:exit_test.lib',
                            '/LIBPATH:' + str(repo / 'depends/detour'), 'user32.lib'], cwd=work, check=True)
    subprocess.run(common + [str(repo / 'tools/exit_diagnostics_test.cpp'),
                            '/Fe:exit_test.exe', '/link', 'exit_test.lib', 'user32.lib'], cwd=work, check=True)
    cases = {
        'exit': (37, ['ExitProcess', 'DLL_PROCESS_DETACH', 'BBCF_IM_Shutdown']),
        'return': (38, ['DLL_PROCESS_DETACH', 'BBCF_IM_Shutdown']),
        'terminate': (39, ['TerminateProcess']),
        'rtl': (40, ['RtlExitUserProcess', 'DLL_PROCESS_DETACH']),
        'nt': (41, ['NtTerminateProcess']),
        'messages': (42, ['WM_CLOSE', 'SC_CLOSE', 'WM_DESTROY', 'PostQuitMessage', 'DLL_PROCESS_DETACH']),
        'invalid-handle': (43, ['TerminateProcess-return', 'DLL_PROCESS_DETACH']),
    }
    for mode, (expected, events) in cases.items():
        log, debug = work / (mode + '.log'), work / (mode + '.debug')
        result = subprocess.run([str(work / 'exit_test.exe'), mode, str(log), str(debug)], timeout=20)
        text = log.read_text() if log.exists() else '<missing log>'
        if result.returncode != expected:
            print(text)
            raise AssertionError((mode, result.returncode, expected))
        for event in events:
            assert f'event={event} ' in text, (mode, event, text)
        assert re.search(r'stackFrames=[1-9]', text), (mode, 'no stack frames', text)
        assert 'module=exit_test.exe' in text, (mode, 'missing caller module', text)
        assert 'WM_MOUSEMOVE' not in text
        assert log.read_bytes() == debug.read_bytes(), (mode, 'mirror mismatch')
        if mode in ('terminate', 'nt'):
            assert 'event=DLL_PROCESS_DETACH' not in text, mode
        if mode == 'messages':
            assert 'event=PostQuitMessage arg1=0x2A' in text
            assert 'event=window-parameters arg1=0x123 arg2=0x456' in text
        print(f'PASS {mode}: exit={expected}, events and flushed stacks retained')
    # A relaunch appends a second session; rollover preserves the previous file.
    append_log = work / 'exit.log'
    subprocess.run([str(work / 'exit_test.exe'), 'exit', str(append_log), str(work / 'append.debug')], timeout=20)
    assert append_log.read_text().count('event=session-start ') == 2
    rotate_log = work / 'rotate.log'
    rotate_log.write_bytes(b'x' * (2 * 1024 * 1024))
    subprocess.run([str(work / 'exit_test.exe'), 'exit', str(rotate_log), str(work / 'rotate.debug')], timeout=20)
    assert Path(str(rotate_log) + '.previous').stat().st_size == 2 * 1024 * 1024
    assert rotate_log.read_text().count('event=session-start ') == 1
    print('PASS append and rollover')
