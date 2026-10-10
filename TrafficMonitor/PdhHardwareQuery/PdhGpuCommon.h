#pragma once
#include <vector>
#include <Pdh.h>
#include <PdhMsg.h>

// GPU相关PDH计数器的公共函数

// 读取通配符计数器的所有实例及其数值（PDH_FMT_DOUBLE）
// buffer由调用方长期持有并在多次调用间复用，避免每次查询都分配内存；items指向buffer中的数据，下次调用前有效
inline bool PdhReadCounterArray(HCOUNTER counter, std::vector<BYTE>& buffer, PDH_FMT_COUNTERVALUE_ITEM*& items, DWORD& count)
{
    items = nullptr;
    count = 0;
    for (int retry = 0; retry < 3; retry++)
    {
        DWORD size = static_cast<DWORD>(buffer.size());
        PDH_STATUS status = PdhGetFormattedCounterArray(counter, PDH_FMT_DOUBLE, &size, &count,
            buffer.empty() ? nullptr : reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM*>(buffer.data()));
        if (status == ERROR_SUCCESS)
        {
            items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM*>(buffer.data());
            return count > 0;
        }
        if (status != PDH_MORE_DATA)
            return false;
        buffer.resize(static_cast<size_t>(size) + size / 4 + 1024);     //多留一些余量，实例数量略有增加时不必再次扩容
    }
    return false;
}

// 实例的数值是否有效
inline bool PdhItemValid(const PDH_FMT_COUNTERVALUE_ITEM& item)
{
    return item.FmtValue.CStatus == PDH_CSTATUS_VALID_DATA || item.FmtValue.CStatus == PDH_CSTATUS_NEW_DATA;
}

// 解析GPU计数器的实例名，格式如：
//   GPU Engine:         pid_11188_luid_0x00000000_0x0000f248_phys_0_eng_0_engtype_3d
//   GPU Adapter Memory: luid_0x00000000_0x0000f248_phys_0
// luid为适配器的LUID（高32位与低32位合并为64位），phys为物理适配器序号，eng为引擎序号（实例名中不含引擎时为-1）
// 不使用正则表达式，只依次查找luid_0x、_phys_、_eng_这几个标记，前面的pid和后面的engtype（可能为空或带有序号，如Compute_0）都不参与解析
inline bool ParseGpuInstanceName(const wchar_t* name, unsigned long long& luid, int& phys, int& eng)
{
    const wchar_t* p = wcsstr(name, L"luid_0x");
    if (p == nullptr)
        return false;
    p += 5;     //指向“0x”
    wchar_t* end{};
    unsigned long luid_high = wcstoul(p, &end, 16);
    if (end == p || *end != L'_')
        return false;
    p = end + 1;
    unsigned long luid_low = wcstoul(p, &end, 16);
    if (end == p)
        return false;
    luid = (static_cast<unsigned long long>(luid_high) << 32) | luid_low;

    if (wcsncmp(end, L"_phys_", 6) != 0)
        return false;
    p = end + 6;
    long value = wcstol(p, &end, 10);
    if (end == p)
        return false;
    phys = static_cast<int>(value);

    eng = -1;
    if (wcsncmp(end, L"_eng_", 5) == 0)
    {
        p = end + 5;
        value = wcstol(p, &end, 10);
        if (end != p)
            eng = static_cast<int>(value);
    }
    return true;
}
