#pragma once
#include <windows.h>
#include <string>
#include <vector>

class IOutputWriter;
class TimeoutController;

// 注册表枚举器：递归遍历注册表根键，通过 IOutputWriter 输出结果
class RegistryEnumerator {
public:
    RegistryEnumerator(IOutputWriter& output, TimeoutController& timeout)
        : m_output(output), m_timeout(timeout) {}

    // 枚举所有五大根键
    void EnumerateAll();

    // 枚举单个根键
    void EnumerateRootKey(HKEY hRootKey, const wchar_t* rootName);

private:
    // 获取值类型名称
    static const wchar_t* GetValueTypeName(DWORD type);

    // 打印单个值
    void PrintValue(HKEY hKey, const wchar_t* valueName, DWORD valueType, DWORD dataSize);

    // 递归枚举子键
    void EnumerateKeys(HKEY hRootKey, const std::wstring& path,
                       int depth = 0, int maxDepth = 10);

    IOutputWriter& m_output;
    TimeoutController& m_timeout;
};
