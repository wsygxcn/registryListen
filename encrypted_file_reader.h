#pragma once
#include <windows.h>
#include <bcrypt.h>
#include <string>
#include <vector>

class IOutputWriter;

// 加密文件读取器：从 .key 文件读取密钥，解密 .enc 文件，返回明文数据
class EncryptedFileReader {
public:
    // 构造函数：指定 .enc 文件路径和输出器
    EncryptedFileReader(const std::wstring& encFilePath, IOutputWriter& output)
        : m_encFilePath(encFilePath), m_output(output) {}

    // 解密并返回明文数据，失败返回空 vector
    std::vector<char> Read();

private:
    // 从十六进制宽字符串解析为字节数组
    static bool HexToBytes(const std::wstring& hex, unsigned char* out, size_t outLen);

    // 获取对应的 .key 文件路径
    std::wstring GetKeyFilePath() const;

    std::wstring m_encFilePath;
    IOutputWriter& m_output;
};
