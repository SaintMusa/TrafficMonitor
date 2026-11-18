#pragma once
#include "PdhQuery.h"

// GPU显存占用监控类
// 数据来自PDH计数器 \GPU Adapter Memory(*)\Dedicated Usage 和 \GPU Adapter Memory(*)\Shared Usage（实例名为luid_..._phys_N），
// 这些计数器没有提供显存总量，因此无法得到占用百分比。
// 显示的适配器按以下规则选择：
//  1. 独立显存占用曾达到DEDICATED_THRESHOLD的适配器视为“有独立显存”（独立显卡，或带有显存的核显）。该标记在适配器存在期间保持不变，避免显卡空闲时来回切换。
//     只要存在这样的适配器，就只在它们中选择，并显示其独立显存占用（Dedicated Usage）。
//  2. 否则（核显等没有独立显存的适配器）在所有适配器中选择，并显示其共享显存占用（Shared Usage）。
//     只占用很少显存的适配器（如虚拟显示器驱动）因占用量小而不会被选中。
//  3. 同一类别中选择占用量最大的适配器，之后保持选择不变，除非另一个适配器的占用量明显更大（见SWITCH_MARGIN），或者原适配器消失。
class CPdhGPUMemory : public CPdhQuery
{
public:
    CPdhGPUMemory();
    ~CPdhGPUMemory();

    // Get VRAM usage in bytes. total is the capacity of the same adapter (dedicated VRAM, or shared system memory
    // for adapters without dedicated VRAM), or -1 if DXGI could not report it.
    // 获取显存占用（单位为字节）
    // 返回值: true=成功, false=失败
    bool GetGpuMemoryUsed(/*out*/ __int64& used, /*out*/ __int64& total);

private:
    static const unsigned long long DEDICATED_THRESHOLD = 64ull * 1024 * 1024;  //独立显存占用达到该值才认为适配器有独立显存
    static const unsigned long long SWITCH_MARGIN = 128ull * 1024 * 1024;       //切换适配器时，新适配器的占用量至少要比当前适配器的2倍再多出该值

    struct AdapterKey
    {
        unsigned long long luid;    //适配器LUID
        int phys;                   //物理适配器序号
        bool operator==(const AdapterKey& other) const { return luid == other.luid && phys == other.phys; }
    };
    struct AdapterMemory
    {
        AdapterKey key;
        unsigned long long dedicated;   //独立显存占用（字节）
        unsigned long long shared;      //共享显存占用（字节）
        bool has_dedicated;             //是否有独立显存
        unsigned long long Score() const { return has_dedicated ? dedicated : shared; }
    };
    AdapterMemory* FindAdapter(const AdapterKey& key);
    bool SelectAdapter(__int64& used, __int64& total);      //从m_adapters中选择要显示的适配器，返回其显存占用

    //Total memory of one adapter as reported by DXGI
    struct AdapterTotals
    {
        unsigned long long luid;
        bool found;                         //false = DXGI did not list this LUID; remembered so we do not retry every second
        unsigned long long dedicated;       //DedicatedVideoMemory
        unsigned long long shared;          //SharedSystemMemory
    };
    //Loads dxgi.dll on demand (it is delay-loaded on purpose, so it must not stay loaded in GDI render mode),
    //enumerates the adapters, then unloads it again. Fills m_totals_cache for every adapter it finds.
    void EnumerateAdapterTotals();
    __int64 GetAdapterTotal(const AdapterMemory& adapter);

    std::vector<AdapterTotals> m_totals_cache;  //cache by LUID; each new LUID triggers at most one enumeration
    HCOUNTER m_shared_counter = nullptr;    //Shared Usage计数器（与基类的Dedicated Usage计数器在同一个查询中）
    std::vector<BYTE> m_buffer;             //PDH返回数据的缓冲区，在多次查询间复用
    std::vector<AdapterMemory> m_adapters;  //每次查询时的适配器列表，在多次查询间复用
    std::vector<AdapterKey> m_dedicated_adapters;   //已确认有独立显存的适配器
    bool m_has_selected = false;            //是否已选择适配器
    AdapterKey m_selected{};                //当前选择的适配器
};
