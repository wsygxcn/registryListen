#include <windows.h>
#include <string>

#include "timeout_controller.h"
#include "output_writer.h"
#include "privilege_manager.h"
#include "registry_enumerator.h"
#include "encrypted_file_saver.h"

int main() {
    // ---- 1. 组装输出层 ----
    ConsoleWriter consoleWriter;
    BufferWriter bufferWriter;
    CompositeWriter compositeWriter;
    compositeWriter.AddWriter(&consoleWriter);
    compositeWriter.AddWriter(&bufferWriter);

    // ---- 2. 权限检查与提权 ----
    if (!PrivilegeManager::IsRunningAsAdmin()) {
        consoleWriter.Write(L"当前未以管理员权限运行，正在请求提权...\n");
        if (PrivilegeManager::RelaunchAsAdmin(&consoleWriter)) {
            return 0;
        }
        consoleWriter.Write(L"无法获取管理员权限，程序将以普通权限继续运行。\n");
    } else {
        consoleWriter.Write(L"已以管理员权限运行。\n");
    }

    // ---- 3. 确定输出文件路径 ----
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring filePath(exePath);
    size_t pos = filePath.find_last_of(L"\\/");
    if (pos != std::wstring::npos) {
        filePath = filePath.substr(0, pos + 1);
    }
    filePath += L"registry_output.enc";

    // ---- 4. 注册表枚举 ----
    TimeoutController timeout(20000);
    RegistryEnumerator enumerator(compositeWriter, timeout);

    compositeWriter.Write(L"开始枚举注册表...\n");
    timeout.Start();
    enumerator.EnumerateAll();

    if (timeout.IsTimeout()) {
        compositeWriter.Write(L"\n枚举因超时提前终止。\n");
    } else {
        compositeWriter.Write(L"\n注册表枚举完成。\n");
    }

    // ---- 5. 加密存储 ----
    EncryptedFileSaver saver(filePath, consoleWriter);
    saver.Save(bufferWriter.GetBuffer());

    // ---- 6. 等待退出 ----
    consoleWriter.Write(L"按 Enter 键退出...\n");

    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    wchar_t buf[2];
    DWORD read;
    ReadConsoleW(hIn, buf, 1, &read, nullptr);

    return 0;
}
