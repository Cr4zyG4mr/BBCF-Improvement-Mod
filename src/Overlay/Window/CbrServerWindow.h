#pragma once

#include "IWindow.h"
#include "Hooks/hooks_cbr_runtime.h"

class CbrServerWindow : public IWindow
{
public:
    CbrServerWindow(const std::string& windowTitle, bool windowClosable,
        ImGuiWindowFlags windowFlags = 0)
        : IWindow(windowTitle, windowClosable, windowFlags)
    {
        // WindowContainer is constructed after the title-screen hooks have found the
        // game and its code image is stable, making this a safe one-time place to
        // install the CBR input hooks without racing the very early DLL startup path.
        EnsureCbrRuntimeHooksInstalled();
    }
    ~CbrServerWindow() override = default;

    void Update() override;

protected:
    void Draw() override;

private:
    void DrawImGuiSection();
};
