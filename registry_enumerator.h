#pragma once
#include <windows.h>
#include <string>
#include <vector>

class IOutputWriter;

// 注册表枚举器：递归遍历注册表根键，通过 IOutputWriter 输出结果
class RegistryEnumerator {
public:
    explicit RegistryEnumerator(IOutputWriter& output)
        : m_output(output) {}

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
};
