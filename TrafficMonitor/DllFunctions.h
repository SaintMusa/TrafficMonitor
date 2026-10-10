#pragma once
#include <shellscalingapi.h> // 包含::GetDpiForMonitor
#include <d3dcompiler.h> // 包含D3DCompile
#include <dcomp.h> // 包含DCompositionCreateDevice
#include <dxgi1_3.h> // 包含CreateDXGIFactory2
#include <tchar.h>
#include <utility>
#include <memory>
#include <mutex>

template <class FunctionPointer>
class CDllFunction
#ifdef WIN32
{
    // workaround for MSVC 19.34.31933 target x86

private:
    FunctionPointer m_p_function{nullptr};
    HMODULE m_h_dll;

public:
    CDllFunction(LPCTSTR dll_name, LPCSTR function_name) noexcept
    {
        m_h_dll = ::LoadLibrary(dll_name);
        if (m_h_dll != NULL)
        {
            m_p_function = (FunctionPointer)::GetProcAddress(m_h_dll, function_name);
            if (m_p_function == nullptr)
            {
                ::FreeLibrary(m_h_dll);
                m_h_dll = NULL;
            }
        }
    }
    ~CDllFunction() noexcept
    {
        if (m_h_dll != NULL)
        {
            ::FreeLibrary(m_h_dll);
            m_h_dll = NULL;
        }
    }
    template <class... Args>
    auto operator()(Args&&... args) const
    {
        return m_p_function(std::forward<Args>(args)...);
    }
    bool HasValue() const noexcept
    {
        return m_p_function != nullptr;
    }
}
#endif
;
/**
 * @brief 可以自动管理HMODULE生命周期，并自动尝试加载指定函数的类
 使用示例：
    定义：CDllFunction<decltype(&::GetDpiForMonitor)> GetDpiForMonitor{_T("Shcore.dll"), "GetDpiForMonitor"};
    使用：(某个类或者命名空间)::GetDpiForMonitor(所需的参数);
    当然，变量GetDpiForMonitor不能暴露在全局命名空间内。
 *
 * @tparam R 函数返回值
 * @tparam Args 函数参数
 */
template <class R, class... Args>
class CDllFunction<R (*)(Args...)>
{
    using FunctionPointer = R (*)(Args...);

private:
    FunctionPointer m_p_function{nullptr};
    HMODULE m_h_dll;

public:
    CDllFunction(LPCTSTR dll_name, LPCSTR function_name) noexcept
    {
        m_h_dll = ::LoadLibrary(dll_name);
        if (m_h_dll != NULL)
        {
            m_p_function = (FunctionPointer)::GetProcAddress(m_h_dll, function_name);
            if (m_p_function == nullptr)
            {
                ::FreeLibrary(m_h_dll);
                m_h_dll = NULL;
            }
        }
    }
    ~CDllFunction() noexcept
    {
        if (m_h_dll != NULL)
        {
            ::FreeLibrary(m_h_dll);
            m_h_dll = NULL;
        }
    }
    R operator()(Args... args) const
    {
        return m_p_function(args...);
    }
    bool HasValue() const noexcept
    {
        return m_p_function != nullptr;
    }
};

/**
 * @brief 延迟加载版本的CDllFunction：构造时不加载DLL，第一次调用HasValue()或operator()时才加载（线程安全）。
 用于避免在程序启动时（静态初始化阶段）就加载很少用到的DLL（如d3dcompiler_47.dll、dcomp.dll、dxgi.dll）。
 *
 * @tparam FunctionPointer 函数指针类型
 */
template <class FunctionPointer>
class CLazyDllFunction
{
private:
    LPCTSTR m_dll_name;
    LPCSTR m_function_name;
    mutable std::once_flag m_once_flag;
    mutable std::unique_ptr<CDllFunction<FunctionPointer>> m_p_function;

    const CDllFunction<FunctionPointer>& Get() const
    {
        std::call_once(m_once_flag,
                       [this]()
                       {
                           m_p_function = std::make_unique<CDllFunction<FunctionPointer>>(m_dll_name, m_function_name);
                       });
        return *m_p_function;
    }

public:
    constexpr CLazyDllFunction(LPCTSTR dll_name, LPCSTR function_name) noexcept
        : m_dll_name{dll_name}, m_function_name{function_name}
    {
    }
    template <class... Args>
    auto operator()(Args&&... args) const
    {
        return Get()(std::forward<Args>(args)...);
    }
    bool HasValue() const
    {
        return Get().HasValue();
    }
};

typedef HRESULT(WINAPI* _GetDpiForMonitor)(HMONITOR hmonitor, MONITOR_DPI_TYPE dpiType, UINT* dpiX, UINT* dpiY);

class CDllFunctions
{
public:
    CDllFunctions();
    ~CDllFunctions();

public:
    HRESULT GetDpiForMonitor(HMONITOR hmonitor, MONITOR_DPI_TYPE dpiType, UINT* dpiX, UINT* dpiY);
    // 这些函数所在的DLL在第一次使用时才会被加载
    static const CLazyDllFunction<decltype(&::D3DCompile)> D3DCompile;
    static const CLazyDllFunction<decltype(&::DCompositionCreateDevice)> DCompositionCreateDevice;
    static const CLazyDllFunction<decltype(&::CreateDXGIFactory2)> CreateDXGIFactory2;

private:
    _GetDpiForMonitor m_getDpiForMonitor{};

private:
    HMODULE m_shcore_module{};
};
