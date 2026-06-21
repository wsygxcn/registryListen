#include "registry_enumerator.h"
#include "output_writer.h"
#include "timeout_controller.h"

const wchar_t* RegistryEnumerator::GetValueTypeName(DWORD type) {
    switch (type) {
        case REG_SZ:        return L"REG_SZ";
        case REG_EXPAND_SZ: return L"REG_EXPAND_SZ";
        case REG_BINARY:    return L"REG_BINARY";
        case REG_DWORD:     return L"REG_DWORD";
        case REG_MULTI_SZ:  return L"REG_MULTI_SZ";
        case REG_QWORD:     return L"REG_QWORD";
        default:            return L"REG_UNKNOWN";
    }
}

void RegistryEnumerator::PrintValue(HKEY hKey, const wchar_t* valueName,
                                     DWORD valueType, DWORD dataSize) {
    m_output.Write(L"    ");
    m_output.Write(valueName[0] ? valueName : L"(默认)");
    m_output.Write(L"  [");
    m_output.Write(GetValueTypeName(valueType));
    m_output.Write(L"]");

    std::vector<BYTE> data(dataSize);
    if (RegQueryValueExW(hKey, valueName, nullptr, nullptr, data.data(), &dataSize) == ERROR_SUCCESS) {
        wchar_t numBuf[64];
        switch (valueType) {
            case REG_SZ:
            case REG_EXPAND_SZ:
                m_output.Write(L" = \"");
                m_output.Write(reinterpret_cast<wchar_t*>(data.data()));
                m_output.Write(L"\"");
                break;
            case REG_DWORD:
                if (dataSize >= sizeof(DWORD)) {
                    wsprintfW(numBuf, L" = %lu", *reinterpret_cast<DWORD*>(data.data()));
                    m_output.Write(numBuf);
                }
                break;
            case REG_QWORD:
                if (dataSize >= sizeof(DWORD64)) {
                    wsprintfW(numBuf, L" = %llu", *reinterpret_cast<DWORD64*>(data.data()));
                    m_output.Write(numBuf);
                }
                break;
            case REG_BINARY:
                wsprintfW(numBuf, L" = (二进制数据, %lu 字节)", dataSize);
                m_output.Write(numBuf);
                break;
            case REG_MULTI_SZ:
                m_output.Write(L" = (多字符串)");
                break;
            default:
                m_output.Write(L" = (未知类型)");
                break;
        }
    }
    m_output.Write(L"\n");
}

void RegistryEnumerator::EnumerateKeys(HKEY hRootKey, const std::wstring& path,
                                        int depth, int maxDepth) {
    if (depth > maxDepth) return;
    if (m_timeout.IsTimeout()) return;

    HKEY hKey;
    LONG result = RegOpenKeyExW(hRootKey, path.c_str(), 0, KEY_READ, &hKey);
    if (result != ERROR_SUCCESS) return;

    if (!path.empty()) {
        m_output.Write(L"\n[");
        m_output.Write(path.c_str());
        m_output.Write(L"]\n");
    }

    // 枚举值
    DWORD valueIndex = 0;
    wchar_t valueName[16384];
    DWORD valueNameSize;
    DWORD valueType;
    DWORD dataSize;

    while (true) {
        if (m_timeout.IsTimeout()) break;

        valueNameSize = 16384;
        dataSize = 0;
        result = RegEnumValueW(hKey, valueIndex, valueName, &valueNameSize,
                               nullptr, &valueType, nullptr, &dataSize);
        if (result == ERROR_NO_MORE_ITEMS) break;
        if (result != ERROR_SUCCESS && result != ERROR_MORE_DATA) break;

        PrintValue(hKey, valueName, valueType, dataSize);
        valueIndex++;
    }

    // 枚举子键
    DWORD subKeyIndex = 0;
    wchar_t subKeyName[256];
    DWORD subKeyNameSize;

    while (true) {
        if (m_timeout.IsTimeout()) break;

        subKeyNameSize = 256;
        result = RegEnumKeyExW(hKey, subKeyIndex, subKeyName, &subKeyNameSize,
                               nullptr, nullptr, nullptr, nullptr);
        if (result == ERROR_NO_MORE_ITEMS) break;
        if (result != ERROR_SUCCESS) {
            subKeyIndex++;
            continue;
        }

        std::wstring subPath = path.empty() ? subKeyName : path + L"\\" + subKeyName;
        EnumerateKeys(hRootKey, subPath, depth + 1, maxDepth);
        subKeyIndex++;
    }

    RegCloseKey(hKey);
}

void RegistryEnumerator::EnumerateRootKey(HKEY hRootKey, const wchar_t* rootName) {
    m_output.Write(L"\n========== ");
    m_output.Write(rootName);
    m_output.Write(L" ==========\n");
    EnumerateKeys(hRootKey, L"");
}

void RegistryEnumerator::EnumerateAll() {
    struct RootKeyInfo {
        HKEY hKey;
        const wchar_t* name;
    };

    RootKeyInfo rootKeys[] = {
        { HKEY_CLASSES_ROOT,   L"HKEY_CLASSES_ROOT" },
        { HKEY_CURRENT_USER,   L"HKEY_CURRENT_USER" },
        { HKEY_LOCAL_MACHINE,  L"HKEY_LOCAL_MACHINE" },
        { HKEY_USERS,          L"HKEY_USERS" },
        { HKEY_CURRENT_CONFIG, L"HKEY_CURRENT_CONFIG" },
    };

    for (const auto& root : rootKeys) {
        if (m_timeout.IsTimeout()) {
            m_output.WriteFmt(L"\n已运行 %llu 秒，超时停止。\n",
                              m_timeout.TimeoutMs() / 1000);
            break;
        }
        EnumerateRootKey(root.hKey, root.name);
    }
}
