#include "stdafx.h"
#include "GpuUsage.h"
#include "PdhGpuCommon.h"

///////////////////////////////////////////////////////////////////////////////////////////
// CPdhGPUUsage 实现
///////////////////////////////////////////////////////////////////////////////////////////
CPdhGPUUsage::CPdhGPUUsage()
: CPdhQuery(_T("\\GPU Engine(*)\\Utilization Percentage"))
{
}

CPdhGPUUsage::~CPdhGPUUsage()
{
}

bool CPdhGPUUsage::GetGpuUsage(int& usage)
{
    if (!isInitialized)
        return false;

    PdhCollectQueryData(query);
    PDH_FMT_COUNTERVALUE_ITEM* items{};
    DWORD count{};
    if (!PdhReadCounterArray(counter, m_buffer, items, count))
        return false;
    return CalculateUsage(items, count, usage);
}

bool CPdhGPUUsage::CalculateUsage(const PDH_FMT_COUNTERVALUE_ITEM* items, DWORD count, int& usage)
{
    //按引擎(luid, phys, eng)汇总所有进程的利用率（引擎数量很少，线性查找即可）
    m_engines.clear();
    for (DWORD i = 0; i < count; i++)
    {
        if (!PdhItemValid(items[i]))
            continue;
        unsigned long long luid{};
        int phys{}, eng{};
        if (!ParseGpuInstanceName(items[i].szName, luid, phys, eng) || eng < 0)
            continue;
        auto iter = std::find_if(m_engines.begin(), m_engines.end(), [&](const EngineUsage& e) {
            return e.eng == eng && e.phys == phys && e.luid == luid;
        });
        if (iter == m_engines.end())
            m_engines.push_back({ luid, phys, eng, items[i].FmtValue.doubleValue });
        else
            iter->usage += items[i].FmtValue.doubleValue;
    }
    if (m_engines.empty())
        return false;

    //每个适配器的利用率为其所有引擎中的最大值（同Windows任务管理器的处理），显示的利用率为所有适配器中的最大值，
    //这样空闲的虚拟适配器不会抬高结果，而繁忙的适配器仍会显示出来。两次取最大值等价于对所有引擎直接取最大值
    double max_value = 0;
    for (const auto& engine : m_engines)
    {
        if (engine.usage > max_value)
            max_value = engine.usage;
    }

    usage = static_cast<int>(max_value + 0.5);  // 四舍五入
    usage = min(max(usage, 0), 100);        // 限制在0-100范围
    return true;
}
