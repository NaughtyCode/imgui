#pragma once
#include "../framework/window_base.h"

class WindowPlots final : public WindowBase
{
public:
    using WindowBase::WindowBase;

protected:
    void Render() override;
};
