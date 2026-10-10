#pragma once
#include "PdhQuery.h"

// GPU利用率监控类
class CPdhGPUUsage : public CPdhQuery
{
public:
    CPdhGPUUsage();
    ~CPdhGPUUsage();

    // 获取GPU利用率（0-100）
    // 算法同Windows任务管理器：每个引擎(luid, phys, eng)的利用率为所有进程之和，每个适配器的利用率为其所有引擎中的最大值，
    // 最终取所有适配器中的最大值
    // 返回值: true=成功, false=失败
    // 注意：首次调用通常返回false（PDH需要预热），建议间隔200ms以上再次调用
    bool GetGpuUsage(/*out*/ int& usage);

private:
    bool CalculateUsage(const PDH_FMT_COUNTERVALUE_ITEM* items, DWORD count, int& usage);    //根据各个引擎实例的利用率计算GPU利用率

    struct EngineUsage
    {
        unsigned long long luid;    //适配器LUID
        int phys;                   //物理适配器序号
        int eng;                    //引擎序号
        double usage;               //所有进程在该引擎上的利用率之和
    };
    std::vector<BYTE> m_buffer;             //PDH返回数据的缓冲区，在多次查询间复用
    std::vector<EngineUsage> m_engines;     //每次查询时的引擎列表，在多次查询间复用
};
