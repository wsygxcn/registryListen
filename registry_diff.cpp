#include "registry_diff.h"
#include "output_writer.h"

void RegistryDiff::PrintSectionHeader(const wchar_t* title) {
    m_output.Write(L"\n---------- ");
    m_output.Write(title);
    m_output.Write(L" ----------\n");
}

RegistryDiff::RegistryMap RegistryDiff::Parse(const std::vector<char>& data) {
    RegistryMap result;
    if (data.empty()) return result;

    // 将 UTF-8 数据转为宽字符串以便解析
    int wLen = MultiByteToWideChar(CP_UTF8, 0, data.data(), (int)data.size(), nullptr, 0);
    if (wLen <= 0) return result;
    std::vector<wchar_t> wbuf(wLen + 1);
    MultiByteToWideChar(CP_UTF8, 0, data.data(), (int)data.size(), wbuf.data(), wLen);
    wbuf[wLen] = L'\0';

    std::wstring currentRoot;
    std::wstring currentSubPath;

    // 逐行解析
    const wchar_t* p = wbuf.data();
    const wchar_t* lineStart = p;
    while (*p) {
        if (*p == L'\n' || (*p == L'\r' && *(p + 1) == L'\n')) {
            std::wstring line(lineStart, p - lineStart);
            if (*p == L'\r') p++; // skip \r
            p++; // skip \n
            lineStart = p;

            // 跳过空行
            if (line.empty()) continue;

            // 匹配根键标题: ========== HKEY_XXX ==========
            if (line.find(L"========== ") == 0) {
                size_t start = 11; // after "========== "
                size_t end = line.find(L" ==========");
                if (end != std::wstring::npos) {
                    currentRoot = line.substr(start, end - start);
                    currentSubPath.clear();
                }
                continue;
            }

            // 匹配子键路径: [path]
            if (line[0] == L'[' && line.back() == L']') {
                currentSubPath = line.substr(1, line.length() - 2);
                continue;
            }

            // 匹配值条目: "    name  [TYPE] = data"
            if (line.size() >= 4 && line[0] == L' ' && line[1] == L' ' && line[2] == L' ' && line[3] == L' ') {
                // 构建完整键路径
                std::wstring fullPath = currentRoot;
                if (!currentSubPath.empty()) {
                    fullPath += L"\\" + currentSubPath;
                }

                // 转为 UTF-8 存储
                int utf8Len = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), (int)line.size(), nullptr, 0, nullptr, nullptr);
                if (utf8Len > 0) {
                    std::vector<char> utf8Line(utf8Len);
                    WideCharToMultiByte(CP_UTF8, 0, line.c_str(), (int)line.size(), utf8Line.data(), utf8Len, nullptr, nullptr);
                    std::string entry(utf8Line.data(), utf8Len - 1); // exclude null

                    int utf8PathLen = WideCharToMultiByte(CP_UTF8, 0, fullPath.c_str(), (int)fullPath.size(), nullptr, 0, nullptr, nullptr);
                    std::vector<char> utf8Path(utf8PathLen);
                    WideCharToMultiByte(CP_UTF8, 0, fullPath.c_str(), (int)fullPath.size(), utf8Path.data(), utf8PathLen, nullptr, nullptr);
                    std::string pathKey(utf8Path.data(), utf8PathLen);

                    result[pathKey].insert(entry);
                }
            }
        } else {
            p++;
        }
    }

    return result;
}

void RegistryDiff::Compare(const std::vector<char>& currentData,
                            const std::vector<char>& previousData) {
    RegistryMap current = Parse(currentData);
    RegistryMap previous = Parse(previousData);

    // 收集所有键路径
    std::set<std::string> allKeys;
    for (const auto& kv : current) allKeys.insert(kv.first);
    for (const auto& kv : previous) allKeys.insert(kv.first);

    int addedKeys = 0, removedKeys = 0, modifiedKeys = 0;
    int totalAddedValues = 0, totalRemovedValues = 0;

    // 遍历所有键，找出差异
    for (const auto& key : allKeys) {
        bool inCurrent = current.count(key) > 0;
        bool inPrevious = previous.count(key) > 0;

        if (inCurrent && !inPrevious) {
            // 新增的键
            if (addedKeys == 0) PrintSectionHeader(L"新增的注册表键");
            addedKeys++;

            int utf8Len = MultiByteToWideChar(CP_UTF8, 0, key.c_str(), (int)key.size(), nullptr, 0);
            std::vector<wchar_t> wkey(utf8Len + 1);
            MultiByteToWideChar(CP_UTF8, 0, key.c_str(), (int)key.size(), wkey.data(), utf8Len);
            wkey[utf8Len] = L'\0';

            m_output.Write(L"[+] ");
            m_output.Write(wkey.data());
            m_output.WriteFmt(L"  (%zu 个值)\n", current[key].size());

            for (const auto& val : current[key]) {
                int vLen = MultiByteToWideChar(CP_UTF8, 0, val.c_str(), (int)val.size(), nullptr, 0);
                std::vector<wchar_t> wval(vLen + 1);
                MultiByteToWideChar(CP_UTF8, 0, val.c_str(), (int)val.size(), wval.data(), vLen);
                wval[vLen] = L'\0';
                m_output.Write(wval.data());
                m_output.Write(L"\n");
            }
            totalAddedValues += (int)current[key].size();

        } else if (!inCurrent && inPrevious) {
            // 删除的键
            if (removedKeys == 0) PrintSectionHeader(L"删除的注册表键");
            removedKeys++;

            int utf8Len = MultiByteToWideChar(CP_UTF8, 0, key.c_str(), (int)key.size(), nullptr, 0);
            std::vector<wchar_t> wkey(utf8Len + 1);
            MultiByteToWideChar(CP_UTF8, 0, key.c_str(), (int)key.size(), wkey.data(), utf8Len);
            wkey[utf8Len] = L'\0';

            m_output.Write(L"[-] ");
            m_output.Write(wkey.data());
            m_output.WriteFmt(L"  (%zu 个值)\n", previous[key].size());

            for (const auto& val : previous[key]) {
                int vLen = MultiByteToWideChar(CP_UTF8, 0, val.c_str(), (int)val.size(), nullptr, 0);
                std::vector<wchar_t> wval(vLen + 1);
                MultiByteToWideChar(CP_UTF8, 0, val.c_str(), (int)val.size(), wval.data(), vLen);
                wval[vLen] = L'\0';
                m_output.Write(wval.data());
                m_output.Write(L"\n");
            }
            totalRemovedValues += (int)previous[key].size();

        } else {
            // 键在两边都存在，对比值
            const auto& curVals = current[key];
            const auto& prevVals = previous[key];

            // 找出新增的值（在 current 中但不在 previous 中）
            ValueSet addedVals;
            for (const auto& v : curVals) {
                if (prevVals.find(v) == prevVals.end()) {
                    addedVals.insert(v);
                }
            }

            // 找出删除的值（在 previous 中但不在 current 中）
            ValueSet removedVals;
            for (const auto& v : prevVals) {
                if (curVals.find(v) == curVals.end()) {
                    removedVals.insert(v);
                }
            }

            if (!addedVals.empty() || !removedVals.empty()) {
                if (modifiedKeys == 0) PrintSectionHeader(L"修改的注册表键");
                modifiedKeys++;

                int utf8Len = MultiByteToWideChar(CP_UTF8, 0, key.c_str(), (int)key.size(), nullptr, 0);
                std::vector<wchar_t> wkey(utf8Len + 1);
                MultiByteToWideChar(CP_UTF8, 0, key.c_str(), (int)key.size(), wkey.data(), utf8Len);
                wkey[utf8Len] = L'\0';

                m_output.Write(L"[~] ");
                m_output.Write(wkey.data());
                m_output.Write(L"\n");

                for (const auto& v : addedVals) {
                    int vLen = MultiByteToWideChar(CP_UTF8, 0, v.c_str(), (int)v.size(), nullptr, 0);
                    std::vector<wchar_t> wval(vLen + 1);
                    MultiByteToWideChar(CP_UTF8, 0, v.c_str(), (int)v.size(), wval.data(), vLen);
                    wval[vLen] = L'\0';
                    m_output.Write(L"  + ");
                    m_output.Write(wval.data());
                    m_output.Write(L"\n");
                    totalAddedValues++;
                }

                for (const auto& v : removedVals) {
                    int vLen = MultiByteToWideChar(CP_UTF8, 0, v.c_str(), (int)v.size(), nullptr, 0);
                    std::vector<wchar_t> wval(vLen + 1);
                    MultiByteToWideChar(CP_UTF8, 0, v.c_str(), (int)v.size(), wval.data(), vLen);
                    wval[vLen] = L'\0';
                    m_output.Write(L"  - ");
                    m_output.Write(wval.data());
                    m_output.Write(L"\n");
                    totalRemovedValues++;
                }
            }
        }
    }

    // 输出汇总
    m_output.Write(L"\n========== 差异汇总 ==========\n");
    if (addedKeys == 0 && removedKeys == 0 && modifiedKeys == 0) {
        m_output.Write(L"注册表未发生变化。\n");
    } else {
        m_output.WriteFmt(L"新增键: %d 个 (共 %d 个值)\n", addedKeys, totalAddedValues);
        m_output.WriteFmt(L"删除键: %d 个 (共 %d 个值)\n", removedKeys, totalRemovedValues);
        m_output.WriteFmt(L"修改键: %d 个\n", modifiedKeys);
    }
    m_output.Write(L"==============================\n");
}
