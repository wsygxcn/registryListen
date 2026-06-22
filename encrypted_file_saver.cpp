#include "encrypted_file_saver.h"
#include "output_writer.h"

#pragma comment(lib, "bcrypt.lib")

std::wstring EncryptedFileSaver::BytesToHex(const unsigned char* data, size_t len) {
    std::wstring result;
    result.reserve(len * 2);
    for (size_t i = 0; i < len; i++) {
        wchar_t hex[3];
        wsprintfW(hex, L"%02X", data[i]);
        result += hex;
    }
    return result;
}

bool EncryptedFileSaver::Save(const std::vector<char>& data) {
    if (data.empty()) return false;

    // 1. 生成随机 256-bit 密钥和 96-bit nonce
    unsigned char key[32];
    unsigned char nonce[12];
    NTSTATUS status;

    status = BCryptGenRandom(nullptr, key, sizeof(key), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (!BCRYPT_SUCCESS(status)) {
        m_output.Write(L"\n错误: 无法生成加密密钥。\n");
        return false;
    }

    status = BCryptGenRandom(nullptr, nonce, sizeof(nonce), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (!BCRYPT_SUCCESS(status)) {
        m_output.Write(L"\n错误: 无法生成 nonce。\n");
        return false;
    }

    // 2. 打开 AES-GCM 算法提供者
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0);
    if (!BCRYPT_SUCCESS(status)) {
        m_output.Write(L"\n错误: 无法打开 AES 算法提供者。\n");
        return false;
    }

    status = BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE,
                               (PUCHAR)BCRYPT_CHAIN_MODE_GCM, sizeof(BCRYPT_CHAIN_MODE_GCM), 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        m_output.Write(L"\n错误: 无法设置 GCM 模式。\n");
        return false;
    }

    // 3. 生成对称密钥
    BCRYPT_KEY_HANDLE hKey = nullptr;
    status = BCryptGenerateSymmetricKey(hAlg, &hKey, nullptr, 0, key, sizeof(key), 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        m_output.Write(L"\n错误: 无法生成对称密钥。\n");
        return false;
    }

    // 4. 加密（GCM 认证标签 16 字节）
    const ULONG tagSize = 16;
    std::vector<unsigned char> ciphertext(data.size());
    std::vector<unsigned char> authTag(tagSize);

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
    BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
    authInfo.pbNonce = nonce;
    authInfo.cbNonce = sizeof(nonce);
    authInfo.pbTag = authTag.data();
    authInfo.cbTag = tagSize;

    ULONG cbResult = 0;
    status = BCryptEncrypt(hKey,
                           (PUCHAR)data.data(), (ULONG)data.size(),
                           &authInfo,
                           nullptr, 0,
                           ciphertext.data(), (ULONG)ciphertext.size(),
                           &cbResult, 0);

    if (!BCRYPT_SUCCESS(status)) {
        BCryptDestroyKey(hKey);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        m_output.WriteFmt(L"\n错误: 加密失败 (0x%08X)。\n", status);
        return false;
    }

    BCryptDestroyKey(hKey);
    BCryptCloseAlgorithmProvider(hAlg, 0);

    // 5. 写入文件：[nonce(12)][tag(16)][ciphertext]
    HANDLE hFile = CreateFileW(
        m_filePath.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (hFile == INVALID_HANDLE_VALUE) {
        m_output.Write(L"\n警告: 无法创建加密输出文件。\n");
    } else {
        DWORD written;
        WriteFile(hFile, nonce, sizeof(nonce), &written, nullptr);
        WriteFile(hFile, authTag.data(), (DWORD)authTag.size(), &written, nullptr);
        WriteFile(hFile, ciphertext.data(), (DWORD)ciphertext.size(), &written, nullptr);
        CloseHandle(hFile);

        m_output.Write(L"\n加密文件已保存至: ");
        m_output.Write(m_filePath.c_str());
        m_output.Write(L"\n");
    }

    // 6. 输出密钥到控制台
    std::wstring keyHex = BytesToHex(key, sizeof(key));
    std::wstring nonceHex = BytesToHex(nonce, sizeof(nonce));

    m_output.Write(L"\n========== AES-256-GCM 加密信息 ==========\n");
    m_output.Write(L"密钥 (hex): ");
    m_output.Write(keyHex.c_str());
    m_output.Write(L"\nNonce (hex): ");
    m_output.Write(nonceHex.c_str());
    m_output.Write(L"\n==========================================\n");
    m_output.Write(L"请妥善保管以上密钥信息，解密时需要用到。\n");

    // 7. 保存密钥到 .key 文件
    std::wstring keyFilePath = GetKeyFilePath();
    HANDLE hKeyFile = CreateFileW(
        keyFilePath.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    if (hKeyFile != INVALID_HANDLE_VALUE) {
        // 格式: keyHex\nnonceHex
        std::string keyLine;
        int utf8Len = WideCharToMultiByte(CP_UTF8, 0, keyHex.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (utf8Len > 0) {
            std::vector<char> keyBuf(utf8Len);
            WideCharToMultiByte(CP_UTF8, 0, keyHex.c_str(), -1, keyBuf.data(), utf8Len, nullptr, nullptr);
            keyLine.assign(keyBuf.data(), utf8Len - 1); // exclude null terminator
        }
        keyLine += "\n";
        utf8Len = WideCharToMultiByte(CP_UTF8, 0, nonceHex.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (utf8Len > 0) {
            std::vector<char> nonceBuf(utf8Len);
            WideCharToMultiByte(CP_UTF8, 0, nonceHex.c_str(), -1, nonceBuf.data(), utf8Len, nullptr, nullptr);
            keyLine.append(nonceBuf.data(), utf8Len - 1);
        }
        keyLine += "\n";

        DWORD written;
        WriteFile(hKeyFile, keyLine.c_str(), (DWORD)keyLine.size(), &written, nullptr);
        CloseHandle(hKeyFile);
    }

    // 清零密钥缓冲区
    SecureZeroMemory(key, sizeof(key));
    SecureZeroMemory(nonce, sizeof(nonce));

    return true;
}

std::wstring EncryptedFileSaver::GetKeyFilePath() const {
    std::wstring keyPath = m_filePath;
    size_t dotPos = keyPath.find_last_of(L'.');
    if (dotPos != std::wstring::npos) {
        keyPath = keyPath.substr(0, dotPos);
    }
    keyPath += L".key";
    return keyPath;
}
