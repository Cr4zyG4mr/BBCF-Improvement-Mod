# Experimental process-exit diagnostics

The Windows session on 2026-09-17 initialized an online match at 11:27:21.638,
continued exchanging Steam packets, then reached DLL cleanup at 11:27:23.375.
No exception record or Windows Reliability Monitor entry identified the reason.
This instrumentation records the path into shutdown; it does not fix the cause.

## Captured evidence

With `GenerateDebugLogs=1`, initialization installs five optional API detours:
`ExitProcess`, `TerminateProcess`, `RtlExitUserProcess`, `NtTerminateProcess`,
and `PostQuitMessage`. Each installation reports its target, trampoline, and
success. `installedMask=0x1F` means all five installed; another mask means partial
coverage (bits in the listed order). Failure to open the diagnostic file leaves
all five uninstalled. Missing exports or failed detours leave those APIs alone.

`[EXITDIAG]` records include PID, thread, local timestamp components, monotonic
milliseconds, sequence, saved last-error value, arguments, immediate caller when
available, and a best-effort 32-frame stack. For exit APIs, `arg1` is the requested
exit code/status. For termination APIs, `arg2` is the raw target handle: `-1` is
the current-process pseudo-handle; other values are not automatically self.
`NtTerminateProcess(NULL, ...)` has native semantics and is retained separately.
Returned termination calls log their result; return values and last-error values
are preserved. The original APIs always execute with their original arguments.

The existing game window-message callback also records `SC_CLOSE`, `WM_CLOSE`,
`WM_DESTROY`, `WM_NCDESTROY`, `WM_QUERYENDSESSION`, and `WM_ENDSESSION`, up to 32
messages per process, with a limit marker. It does not consume or modify messages.
`window-parameters` holds wParam/lParam; the preceding event identifies the window
and message. `PostQuitMessage` is traced independently, since WM_QUIT is a thread
queue message and is not delivered to a window procedure.

DLL detach is recorded before existing cleanup. Its `arg1` is lpReserved
(nonzero indicates process termination; zero indicates DLL unloading); `arg2`
is the existing clean-shutdown flag. `BBCF_IM_Shutdown` gets a separate stack.
Neither marker proves the exit was intentional or rules out a handled exception.

## Files to send

Send `BBCF_IM/DEBUG.txt` and `BBCF_IM/ExitDiagnostics.log` after a recurrence,
plus any crash bundle. The sidecar appends across launches. At 2 MiB, the next
launch moves it to `ExitDiagnostics.log.previous`; include that too if present.
The sidecar records session PID and build commit/time so sessions can be matched.
No extra gameplay test is required merely to confirm logging: normal startup
reports installed hooks, and a normal exit also produces useful control evidence.

The exit writer uses fixed buffers, pre-opened Windows file handles, WriteFile,
and FlushFileBuffers. DEBUG is mirrored through a duplicate of its existing handle,
without acquiring its C++ mutex or CRT FILE lock. It does not call the crash bundle
writer or symbol loader. The event header is written before walking the stack;
concurrent/reentrant calls retain their header but skip their stack. A startup
module map supplies names/RVAs, with allocation-base/offset fallback for later
modules. Keep the matching build's DLL/PDB for offline symbol resolution.

## Limits

- External forced termination, direct syscalls, and an unhooked fast-fail cannot
  be guaranteed to run this code. These need an external monitor if no exit event
  is captured. Existing exception/crash handling remains intact.
- The stack walk can be short or empty with optimized x86 frames or corruption.
  A recorded exit caller may be an error handler, not the original faulty code.
- Messages bypassing the existing window callback, or WM_QUIT posted through APIs
  other than PostQuitMessage, may not have a message event.
- Diagnostics begin during normal hook setup, not at the earliest DLL load.
- Writes and stack capture are best effort. Disk stalls, severe corruption, or a
  kill during a write can still prevent complete evidence.
- This adds no frame polling or network queries. Hooking and exit-time I/O can
  still alter timing. Normal exits are logged too and are not labeled crashes.

## Verification

`python tools/test_exit_diagnostics.py` runs under the Win32 MSVC environment in
GitHub Actions. It compiles the production diagnostic code with the shipped
Detours library into a test DLL. Disposable child processes exercise explicit
ExitProcess, return from main, self-TerminateProcess, direct RtlExitUserProcess,
direct NtTerminateProcess, PostQuitMessage/WM_QUIT retrieval, window filtering,
and invalid-handle failure/last-error propagation. It verifies real DLL detach,
exit codes, usable stack/module records, identical flushed DEBUG/sidecar output,
append across relaunch, and rollover. Each child has a 20-second timeout.

References: [ExitProcess](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-exitprocess),
[TerminateProcess](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-terminateprocess),
[PostQuitMessage](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-postquitmessage).
