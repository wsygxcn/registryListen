#pragma once
#include <windows.h>
#include <bcrypt.h>
#include <string>
#include <vector>

class IOutputWriter;

// 加密文件保存器：接收字节数据，AES-256-GCM 加密后写入文件，输出密钥信息
class EncryptedFileSaver {
public:
    // 构造函数：指定输出文件路径和输出器（用于打印密钥信息）
    EncryptedFileSaver(const std::wstring& filePath, IOutputWriter& output)
        : m_filePath(filePath), m_output(output) {}

    // 对 data 进行 AES-256-GCM 加密并保存到文件
    // 返回 true 表示成功
    bool Save(const std::vector<char>& data);

    // 获取密钥文件路径（.enc 文件同目录下的 .key 文件）
    std::wstring GetKeyFilePath() const;

private:
    // 字节数组转十六进制宽字符串
    static std::wstring BytesToHex(const unsigned char* data, size_t len);

    std::wstring m_filePath;
    IOutputWriter& m_output;
};
