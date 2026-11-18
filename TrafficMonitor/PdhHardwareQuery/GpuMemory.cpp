#include "stdafx.h"
#include "GpuMemory.h"
#include "PdhGpuCommon.h"
#include <dxgi.h>

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

bool CPdhGPUMemory::GetGpuMemoryUsed(__int64& used, __int64& total)
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
    return SelectAdapter(used, total);
}

void CPdhGPUMemory::EnumerateAdapterTotals()
{
    //Load dxgi.dll by hand instead of calling CreateDXGIFactory1 directly: the DLL is delay-loaded on purpose,
    //and a direct call would keep the whole DXGI stack loaded for good, even in GDI render mode.
    HMODULE dxgi = LoadLibraryW(L"dxgi.dll");
    if (dxgi == nullptr)
        return;
    typedef HRESULT(WINAPI* CreateDXGIFactory1Fn)(REFIID, void**);
    auto create_factory = reinterpret_cast<CreateDXGIFactory1Fn>(GetProcAddress(dxgi, "CreateDXGIFactory1"));
    if (create_factory != nullptr)
    {
        IDXGIFactory1* factory{};
        if (SUCCEEDED(create_factory(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory))) && factory != nullptr)
        {
            IDXGIAdapter1* dxgi_adapter{};
            for (UINT i = 0; SUCCEEDED(factory->EnumAdapters1(i, &dxgi_adapter)) && dxgi_adapter != nullptr; i++)
            {
                DXGI_ADAPTER_DESC1 desc{};
                if (SUCCEEDED(dxgi_adapter->GetDesc1(&desc)))
                {
                    //Same packing as the LUID parsed from the PDH instance name (high << 32 | low)
                    unsigned long long luid = (static_cast<unsigned long long>(static_cast<unsigned long>(desc.AdapterLuid.HighPart)) << 32)
                        | static_cast<unsigned long>(desc.AdapterLuid.LowPart);
                    m_totals_cache.push_back({ luid, true, static_cast<unsigned long long>(desc.DedicatedVideoMemory), static_cast<unsigned long long>(desc.SharedSystemMemory) });
                }
                dxgi_adapter->Release();
                dxgi_adapter = nullptr;
            }
            factory->Release();
        }
    }
    //All COM objects are released above, so the DLL can be unloaded safely
    FreeLibrary(dxgi);
}

__int64 CPdhGPUMemory::GetAdapterTotal(const AdapterMemory& adapter)
{
    auto find_totals = [&]() -> const AdapterTotals*
    {
        for (const auto& totals : m_totals_cache)
        {
            if (totals.luid == adapter.key.luid)
                return &totals;
        }
        return nullptr;
    };
    const AdapterTotals* totals = find_totals();
    if (totals == nullptr)
    {
        //Enumerate at most once per new LUID. A LUID DXGI does not list is cached as "not found" so it is not retried every second.
        EnumerateAdapterTotals();
        totals = find_totals();
        if (totals == nullptr)
        {
            m_totals_cache.push_back({ adapter.key.luid, false, 0, 0 });
            return -1;
        }
    }
    if (!totals->found)
        return -1;
    unsigned long long value = adapter.has_dedicated ? totals->dedicated : totals->shared;
    return value == 0 ? -1 : static_cast<__int64>(value);
}

bool CPdhGPUMemory::SelectAdapter(__int64& used, __int64& total)
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
    total = GetAdapterTotal(*selected);
    return true;
}
