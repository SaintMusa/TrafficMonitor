#include "stdafx.h"
#include "SupportedRenderEnums.h"
#include "TaskBarDlgDrawCommon.h"

void CSupportedRenderEnums::Probe() const
{
    if (m_is_probed)
        return;
    m_is_probed = true;
    if (CDCompositionSupport::CheckSupport() && CDxgi1Support2::CheckSupport())
    {
        m_enums[D2D1_WITH_DCOMPOSITION_INDEX] = true;
    }
    if (!m_is_d2d1_disabled && CTaskBarDlgDrawCommonSupport::CheckSupport())
    {
        m_enums[D2D1_INDEX] = true;
    }
}

void CSupportedRenderEnums::EnableDefaultOnly() noexcept
{
    m_enums = decltype(m_enums){1};
    // 只启用默认渲染方式时无需再检测D2D等的支持情况，从而避免加载相关DLL
    m_is_probed = true;
}

void CSupportedRenderEnums::DisableD2D1() noexcept
{
    m_is_d2d1_disabled = true;
    m_enums[D2D1_INDEX] = false;
}

auto CSupportedRenderEnums::GetAutoFitEnum() const noexcept
    -> DrawCommonHelper::RenderType
{
    Probe();
    std::size_t i;
    for (i = m_enums.size() - 1; i > 0; --i)
    {
        if (m_enums[i])
        {
            break;
        }
    }
    switch (i)
    {
    case D2D1_INDEX:
        return DrawCommonHelper::RenderType::D2D1;
    case D2D1_WITH_DCOMPOSITION_INDEX:
        return DrawCommonHelper::RenderType::D2D1_WITH_DCOMPOSITION;
    case DEFAULT_INDEX:
        return DrawCommonHelper::RenderType::DEFAULT;
    default:
        return DrawCommonHelper::RenderType::DEFAULT;
    }
}

auto CSupportedRenderEnums::GetMaxSupportedRenderEnum() const noexcept
    -> DrawCommonHelper::RenderType
{
    Probe();
    if (m_enums[D2D1_WITH_DCOMPOSITION_INDEX])
    {
        return DrawCommonHelper::RenderType::D2D1_WITH_DCOMPOSITION;
    }
    else if (m_enums[D2D1_INDEX])
    {
        return DrawCommonHelper::RenderType::D2D1;
    }
    else if (m_enums[DEFAULT_INDEX])
    {
        return DrawCommonHelper::RenderType::DEFAULT;
    }
    return DrawCommonHelper::RenderType::DEFAULT;
}

bool CSupportedRenderEnums::IsD2D1WithDCompositionEnabled() const noexcept
{
    Probe();
    return m_enums[D2D1_WITH_DCOMPOSITION_INDEX];
}

bool CSupportedRenderEnums::IsD2D1Enabled() const noexcept
{
    Probe();
    return m_enums[D2D1_INDEX];
}