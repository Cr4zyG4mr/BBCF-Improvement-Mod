#include "hooks_cbr_runtime.h"

#include "hooks_cbr.h"
#include "HookManager.h"

#include "Core/interfaces.h"
#include "Core/logger.h"
#include "Game/gamestates.h"

namespace
{
    DWORD g_p1InputJmpBack = 0;
    DWORD g_p2InputJmpBack = 0;
    DWORD g_p1OverwriteNetplayJmpBack = 0;
    DWORD g_p2ReadNetplayJmpBack = 0;

    bool g_installAttempted = false;
    bool g_installed = false;

    bool IsCbrRuntimeStateReady()
    {
        if (g_gameVals.pGameMode == nullptr || g_gameVals.pMatchState == nullptr)
            return false;

        const int gameMode = *g_gameVals.pGameMode;
        if (gameMode != GameMode_Training &&
            gameMode != GameMode_Versus &&
            gameMode != GameMode_Online)
        {
            return false;
        }

        if (g_interfaces.player1.IsCharDataNullPtr() ||
            g_interfaces.player2.IsCharDataNullPtr())
        {
            return false;
        }

        // RecordCbrHelperData walks this list whenever CBR is active. Keeping the
        // hook inert until the entity list exists avoids the early-match timing
        // crash that is much easier to hit under Wine/Proton.
        if (g_gameVals.pEntityList == nullptr || g_gameVals.entityCount <= 0)
            return false;

        return true;
    }

    void __declspec(naked) CbrP1InputHook()
    {
        static char* addr = nullptr;
        static int playerNr = -1;

        __asm
        {
            // Original bytes.
            movzx edi, ax
            mov [esi], di

            mov [addr], esi
            mov playerNr, ebx

            // CBRLogic is normal C++. Preserve the exact machine state BBCF
            // expects when control returns to the original function.
            pushfd
            pushad
        }

        if (IsCbrRuntimeStateReady())
            CBRLogic(addr, 5, true, true, playerNr);

        __asm
        {
            popad
            popfd
            jmp [g_p1InputJmpBack]
        }
    }

    void __declspec(naked) CbrP2InputHook()
    {
        static char* addr = nullptr;
        static int playerNr = -1;

        __asm
        {
            // This intentionally mirrors the legacy CBR hook. The hook replaces
            // a branch site and writes the packed P2 input before continuing.
            mov [esi], ax
            mov [addr], esi
            mov playerNr, ebx
            pushfd
            pushad
        }

        if (IsCbrRuntimeStateReady())
            CBRLogic(addr, 4, true, true, playerNr);

        __asm
        {
            popad
            popfd
            jmp [g_p2InputJmpBack]
        }
    }

    void __declspec(naked) CbrP1OverwriteNetplayHook()
    {
        static char* addr = nullptr;
        static int playerNum = -1;
        static int input = 5;

        __asm
        {
            mov input, eax
            mov playerNum, edi
            mov [addr], esi
            pushfd
            pushad
        }

        if (IsCbrRuntimeStateReady())
        {
            const uintptr_t address = reinterpret_cast<uintptr_t>(addr);
            if ((address & 0x00000678) == static_cast<uintptr_t>(0x00000678))
                input = CBRLogic(input, 6, playerNum, 0, true, true);

            if ((address & 0x00000684) == static_cast<uintptr_t>(0x00000684))
                input = CBRLogic(input, 6, playerNum, 1, true, true);
        }

        __asm
        {
            popad
            popfd

            // Original bytes, with EAX/EBX carrying the possibly replaced CBR input.
            mov eax, input
            cmp dword ptr [esi + 04h], 00h
            movzx ebx, ax
            jmp [g_p1OverwriteNetplayJmpBack]
        }
    }

    void __declspec(naked) CbrP2ReadNetplayHook()
    {
        static int playerNum = -1;
        static int input = 5;

        __asm
        {
            // Original input read.
            movzx ebx, word ptr [eax + edx * 4 + 00062D8Ch]
            mov input, ebx
            mov playerNum, edi
            pushfd
            pushad
        }

        if (IsCbrRuntimeStateReady())
        {
            const bool useNetplayMemory =
                *g_gameVals.pGameMode == GameMode_Online && input != 0;
            input = CBRLogic(input, 0, playerNum, -1, true, false, useNetplayMemory);
        }

        __asm
        {
            popad
            popfd
            mov ebx, input
            jmp [g_p2ReadNetplayJmpBack]
        }
    }

    void RollBackInstalledHooks()
    {
        if (g_p2ReadNetplayJmpBack != 0)
            HookManager::DeactivateHook("CBRRuntime.P2ReadNetplay");
        if (g_p1OverwriteNetplayJmpBack != 0)
            HookManager::DeactivateHook("CBRRuntime.P1OverwriteNetplay");
        if (g_p2InputJmpBack != 0)
            HookManager::DeactivateHook("CBRRuntime.P2Input");
        if (g_p1InputJmpBack != 0)
            HookManager::DeactivateHook("CBRRuntime.P1Input");

        g_p1InputJmpBack = 0;
        g_p2InputJmpBack = 0;
        g_p1OverwriteNetplayJmpBack = 0;
        g_p2ReadNetplayJmpBack = 0;
    }
}

bool EnsureCbrRuntimeHooksInstalled()
{
    if (g_installed)
        return true;
    if (g_installAttempted)
        return false;

    g_installAttempted = true;
    LOG(1, "[CBR] Installing runtime input hooks.\n");

    g_p1InputJmpBack = HookManager::SetHook(
        "CBRRuntime.P1Input",
        "\x0F\xB7\x00\x66\x89\x00\xE9\x00\x00\x00\x00\x53",
        "xx?xx?x????x",
        6,
        CbrP1InputHook);
    if (g_p1InputJmpBack == 0)
    {
        LOG(0, "[CBR] Failed to install P1 input hook.\n");
        RollBackInstalledHooks();
        return false;
    }

    g_p2InputJmpBack = HookManager::SetHook(
        "CBRRuntime.P2Input",
        "\xE9\x00\x00\x00\x00\xE8\x00\x00\x00\x00\x8B\xC8\xE8\x00\x00\x00\x00\x85\xC0\x75\x00\xE8\x00\x00\x00\x00\x8B\xC8\xE8\x00\x00\x00\x00\x85\xC0\x75\x00\xE8\x00\x00\x00\x00\x8B\xC8\xE8\x00\x00\x00\x00\x53",
        "x????x????xxx????xxx?x????xxx????xxx?x????xxx????x",
        5,
        CbrP2InputHook);
    if (g_p2InputJmpBack == 0)
    {
        LOG(0, "[CBR] Failed to install P2 input hook.\n");
        RollBackInstalledHooks();
        return false;
    }

    // The legacy P1InputNetplay hook did not run CBR logic at all and its original
    // implementation even jumped through the wrong continuation address. Leaving
    // that game code untouched is safer than installing a no-op interception.

    g_p1OverwriteNetplayJmpBack = HookManager::SetHook(
        "CBRRuntime.P1OverwriteNetplay",
        "\x83\x7E\x04\x00\x0F\xB7",
        "xxx?xx",
        7,
        CbrP1OverwriteNetplayHook);
    if (g_p1OverwriteNetplayJmpBack == 0)
    {
        LOG(0, "[CBR] Failed to install P1 netplay overwrite hook.\n");
        RollBackInstalledHooks();
        return false;
    }

    g_p2ReadNetplayJmpBack = HookManager::SetHook(
        "CBRRuntime.P2ReadNetplay",
        "\x0F\xB7\x00\x00\x00\x00\x00\x00\xE8\x00\x00\x00\x00\x8B\xC8\xE8\x00\x00\x00\x00\x85\xC0",
        "xx??????x????xxx????xx",
        8,
        CbrP2ReadNetplayHook);
    if (g_p2ReadNetplayJmpBack == 0)
    {
        LOG(0, "[CBR] Failed to install P2 netplay read hook.\n");
        RollBackInstalledHooks();
        return false;
    }

    g_installed = true;
    LOG(1, "[CBR] Runtime input hooks installed successfully.\n");
    return true;
}

bool IsCbrRuntimeHooksInstalled()
{
    return g_installed;
}
