#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>

// 超时控制
static ULONGLONG g_startTime = 0;
static const ULONGLONG TIMEOUT_MS = 20000;

bool IsTimeout() {
    return (GetTickCount64() - g_startTime) >= TIMEOUT_MS;
}

// ---- 输出：宽字符串转 UTF-8，通过 WriteFile 输出到 stdout ----

HANDLE g_hOut = nullptr;

void InitOutput() {
    g_hOut = GetStdHandle(STD_OUTPUT_HANDLE);
}

// 将宽字符串转为系统 ANSI 编码（中文 Windows 下为 GBK）并输出
void WriteStr(const wchar_t* str) {
    int len = WideCharToMultiByte(CP_ACP, 0, str, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return;
    std::vector<char> buf(len);
    WideCharToMultiByte(CP_ACP, 0, str, -1, buf.data(), len, nullptr, nullptr);
    DWORD written;
    WriteFile(g_hOut, buf.data(), len - 1, &written, nullptr);  // -1 去掉结尾的 \0
}

void WriteFmt(const wchar_t* fmt, ...) {
    wchar_t buf[4096];
    va_list args;
    va_start(args, fmt);
    wvsprintfW(buf, fmt, args);
    va_end(args);
    WriteStr(buf);
}

// ---- 管理员权限相关 ----

bool IsRunningAsAdmin() {
    BOOL isAdmin = FALSE;
    PSID adminGroup = nullptr;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;

    if (AllocateAndInitializeSid(&ntAuthority, 2,
                                 SECURITY_BUILTIN_DOMAIN_RID,
                                 DOMAIN_ALIAS_RID_ADMINS,
                                 0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(nullptr, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin;
}

bool RelaunchAsAdmin() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.lpVerb = L"runas";
    sei.lpFile = exePath;
    sei.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&sei)) {
        DWORD err = GetLastError();
        if (err == ERROR_CANCELLED) {
            WriteStr(L"用户取消了 UAC 提权请求。\n");
        } else {
            WriteFmt(L"提权失败，错误码: %lu\n", err);
        }
        return false;
    }
    return true;
}

// ---- 注册表枚举相关 ----

const wchar_t* GetValueTypeName(DWORD type) {
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

void PrintValue(HKEY hKey, const wchar_t* valueName, DWORD valueType, DWORD dataSize) {
    WriteStr(L"    ");
    WriteStr(valueName[0] ? valueName : L"(默认)");
    WriteStr(L"  [");
    WriteStr(GetValueTypeName(valueType));
    WriteStr(L"]");

    std::vector<BYTE> data(dataSize);
    if (RegQueryValueExW(hKey, valueName, nullptr, nullptr, data.data(), &dataSize) == ERROR_SUCCESS) {
        wchar_t numBuf[64];
        switch (valueType) {
            case REG_SZ:
            case REG_EXPAND_SZ:
                WriteStr(L" = \"");
                WriteStr(reinterpret_cast<wchar_t*>(data.data()));
                WriteStr(L"\"");
                break;
            case REG_DWORD:
                if (dataSize >= sizeof(DWORD)) {
                    wsprintfW(numBuf, L" = %lu", *reinterpret_cast<DWORD*>(data.data()));
                    WriteStr(numBuf);
                }
                break;
            case REG_QWORD:
                if (dataSize >= sizeof(DWORD64)) {
                    wsprintfW(numBuf, L" = %llu", *reinterpret_cast<DWORD64*>(data.data()));
                    WriteStr(numBuf);
                }
                break;
            case REG_BINARY:
                wsprintfW(numBuf, L" = (二进制数据, %lu 字节)", dataSize);
                WriteStr(numBuf);
                break;
            case REG_MULTI_SZ:
                WriteStr(L" = (多字符串)");
                break;
            default:
                WriteStr(L" = (未知类型)");
                break;
        }
    }
    WriteStr(L"\n");
}

void EnumerateRegistryKeys(HKEY hRootKey, const std::wstring& path,
                           int depth = 0, int maxDepth = 10) {
    if (depth > maxDepth) return;
    if (IsTimeout()) return;

    HKEY hKey;
    LONG result = RegOpenKeyExW(hRootKey, path.c_str(), 0, KEY_READ, &hKey);
    if (result != ERROR_SUCCESS) return;

    if (!path.empty()) {
        WriteStr(L"\n[");
        WriteStr(path.c_str());
        WriteStr(L"]\n");
    }

    DWORD valueIndex = 0;
    wchar_t valueName[16384];
    DWORD valueNameSize;
    DWORD valueType;
    DWORD dataSize;

    while (true) {
        if (IsTimeout()) break;

        valueNameSize = 16384;
        dataSize = 0;
        result = RegEnumValueW(hKey, valueIndex, valueName, &valueNameSize,
                               nullptr, &valueType, nullptr, &dataSize);
        if (result == ERROR_NO_MORE_ITEMS) break;
        if (result != ERROR_SUCCESS && result != ERROR_MORE_DATA) break;

        PrintValue(hKey, valueName, valueType, dataSize);
        valueIndex++;
    }

    DWORD subKeyIndex = 0;
    wchar_t subKeyName[256];
    DWORD subKeyNameSize;

    while (true) {
        if (IsTimeout()) break;

        subKeyNameSize = 256;
        result = RegEnumKeyExW(hKey, subKeyIndex, subKeyName, &subKeyNameSize,
                               nullptr, nullptr, nullptr, nullptr);
        if (result == ERROR_NO_MORE_ITEMS) break;
        if (result != ERROR_SUCCESS) {
            subKeyIndex++;
            continue;
        }

        std::wstring subPath = path.empty() ? subKeyName : path + L"\\" + subKeyName;
        EnumerateRegistryKeys(hRootKey, subPath, depth + 1, maxDepth);
        subKeyIndex++;
    }

    RegCloseKey(hKey);
}

int main() {
    InitOutput();

    if (!IsRunningAsAdmin()) {
        WriteStr(L"当前未以管理员权限运行，正在请求提权...\n");
        if (RelaunchAsAdmin()) {
            return 0;
        }
        WriteStr(L"无法获取管理员权限，程序将以普通权限继续运行。\n");
    } else {
        WriteStr(L"已以管理员权限运行。\n");
    }

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

    WriteStr(L"开始枚举注册表...\n");

    g_startTime = GetTickCount64();

    for (const auto& root : rootKeys) {
        if (IsTimeout()) {
            WriteFmt(L"\n已运行 %llu 秒，超时停止。\n", TIMEOUT_MS / 1000);
            break;
        }
        WriteStr(L"\n========== ");
        WriteStr(root.name);
        WriteStr(L" ==========\n");
        EnumerateRegistryKeys(root.hKey, L"");
    }

    if (IsTimeout()) {
        WriteStr(L"\n枚举因超时提前终止。\n");
    } else {
        WriteStr(L"\n注册表枚举完成。\n");
    }
    WriteStr(L"按 Enter 键退出...\n");

    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    wchar_t buf[2];
    DWORD read;
    ReadConsoleW(hIn, buf, 1, &read, nullptr);

    return 0;
}
