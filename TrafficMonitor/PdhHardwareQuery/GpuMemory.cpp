#include "stdafx.h"
#include "GpuMemory.h"
#include "PdhGpuCommon.h"

///////////////////////////////////////////////////////////////////////////////////////////
// CPdhGPUMemory 实现
///////////////////////////////////////////////////////////////////////////////////////////
CPdhGPUMemory::CPdhGPUMemory()
: CPdhQuery(_T("\\GPU Adapter Memory(*)\\Dedicated Usage"))
{
    if (isInitialized)
    {
        //在同一个查询中添加共享显存计数器
        if (PdhAddEnglishCounter(query, _T("\\GPU Adapter Memory(*)\\Shared Usage"), NULL, &m_shared_counter) == ERROR_SUCCESS)
            PdhCollectQueryData(query);
        else
            isInitialized = false;
    }
}

CPdhGPUMemory::~CPdhGPUMemory()
{
}

CPdhGPUMemory::AdapterMemory* CPdhGPUMemory::FindAdapter(const AdapterKey& key)
{
    for (auto& adapter : m_adapters)
    {
        if (adapter.key == key)
            return &adapter;
    }
    return nullptr;
}

bool CPdhGPUMemory::GetGpuMemoryUsed(__int64& used)
{
    if (!isInitialized)
        return false;

    PdhCollectQueryData(query);
    m_adapters.clear();

    //独立显存占用
    PDH_FMT_COUNTERVALUE_ITEM* items{};
    DWORD count{};
    if (PdhReadCounterArray(counter, m_buffer, items, count))
    {
        for (DWORD i = 0; i < count; i++)
        {
            AdapterKey key{};
            int eng{};
            if (!PdhItemValid(items[i]) || !ParseGpuInstanceName(items[i].szName, key.luid, key.phys, eng))
                continue;
            AdapterMemory* adapter = FindAdapter(key);
            if (adapter == nullptr)
            {
                m_adapters.push_back({ key, 0, 0, false });
                adapter = &m_adapters.back();
            }
            adapter->dedicated = static_cast<unsigned long long>(max(items[i].FmtValue.doubleValue, 0.0));
        }
    }
    //共享显存占用
    if (PdhReadCounterArray(m_shared_counter, m_buffer, items, count))
    {
        for (DWORD i = 0; i < count; i++)
        {
            AdapterKey key{};
            int eng{};
            if (!PdhItemValid(items[i]) || !ParseGpuInstanceName(items[i].szName, key.luid, key.phys, eng))
                continue;
            AdapterMemory* adapter = FindAdapter(key);
            if (adapter == nullptr)
            {
                m_adapters.push_back({ key, 0, 0, false });
                adapter = &m_adapters.back();
            }
            adapter->shared = static_cast<unsigned long long>(max(items[i].FmtValue.doubleValue, 0.0));
        }
    }
    return SelectAdapter(used);
}

bool CPdhGPUMemory::SelectAdapter(__int64& used)
{
    if (m_adapters.empty())
        return false;

    //标记有独立显存的适配器（该标记保持不变）
    for (auto& adapter : m_adapters)
    {
        if (adapter.dedicated >= DEDICATED_THRESHOLD && std::find(m_dedicated_adapters.begin(), m_dedicated_adapters.end(), adapter.key) == m_dedicated_adapters.end())
            m_dedicated_adapters.push_back(adapter.key);
        adapter.has_dedicated = std::find(m_dedicated_adapters.begin(), m_dedicated_adapters.end(), adapter.key) != m_dedicated_adapters.end();
    }

    //选择占用量最大的适配器（有独立显存的适配器优先）
    const AdapterMemory* best = &m_adapters.front();
    for (const auto& adapter : m_adapters)
    {
        if (adapter.has_dedicated != best->has_dedicated ? adapter.has_dedicated : adapter.Score() > best->Score())
            best = &adapter;
    }
    //当前选择的适配器仍然存在且类别相同时，除非另一个适配器的占用量明显更大，否则保持选择不变
    const AdapterMemory* selected = m_has_selected ? FindAdapter(m_selected) : nullptr;
    if (selected == nullptr || selected->has_dedicated != best->has_dedicated || best->Score() > selected->Score() * 2 + SWITCH_MARGIN)
        selected = best;
    m_has_selected = true;
    m_selected = selected->key;

    used = static_cast<__int64>(selected->Score());
    return true;
}
