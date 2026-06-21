#pragma once
#include <windows.h>
#include <shellapi.h>
#include <string>
#include "output_writer.h"

// 权限管理模块：检测管理员权限，执行 UAC 提权
class PrivilegeManager {
public:
    // 检测当前进程是否以管理员权限运行
    static bool IsRunningAsAdmin() {
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

    // 尝试以管理员权限重新启动当前程序
    // 返回 true 表示提权请求已发起（原进程应退出）
    // 返回 false 表示提权失败或被用户取消
    static bool RelaunchAsAdmin(IOutputWriter* output = nullptr) {
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);

        SHELLEXECUTEINFOW sei = { sizeof(sei) };
        sei.lpVerb = L"runas";
        sei.lpFile = exePath;
        sei.nShow = SW_SHOWNORMAL;

        if (!ShellExecuteExW(&sei)) {
            DWORD err = GetLastError();
            if (output) {
                if (err == ERROR_CANCELLED) {
                    output->Write(L"用户取消了 UAC 提权请求。\n");
                } else {
                    output->WriteFmt(L"提权失败，错误码: %lu\n", err);
                }
            }
            return false;
        }
        return true;
    }
};
