#pragma once
#include "window_base.h"

class WindowDemo final : public WindowBase
{
public:
    using WindowBase::WindowBase;

protected:
    void Render() override;
};
