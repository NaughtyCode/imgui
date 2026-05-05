#pragma once
#include "../framework/window_base.h"

class WindowSettings final : public WindowBase
{
public:
    using WindowBase::WindowBase;

protected:
    void Render() override;
};
