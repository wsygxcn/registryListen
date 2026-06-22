#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <map>
#include <set>

class IOutputWriter;

// 注册表差异对比器：解析两次枚举结果，输出新增/删除/修改的注册表项
class RegistryDiff {
public:
    // 构造函数：注入输出器
    explicit RegistryDiff(IOutputWriter& output) : m_output(output) {}

    // 对比当前结果和上次结果，输出差异
    // currentData: 本次枚举的 UTF-8 文本
    // previousData: 上次枚举的 UTF-8 文本（解密后）
    void Compare(const std::vector<char>& currentData,
                 const std::vector<char>& previousData);

private:
    // 键路径 -> 值条目集合（每个条目是 "ValueName [TYPE] = data"）
    using ValueSet = std::set<std::string>;
    using RegistryMap = std::map<std::string, ValueSet>;

    // 解析缓冲区为结构化数据
    static RegistryMap Parse(const std::vector<char>& data);

    // 输出差异节标题
    void PrintSectionHeader(const wchar_t* title);

    IOutputWriter& m_output;
};
