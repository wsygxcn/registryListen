#include "encrypted_file_reader.h"
#include "output_writer.h"

#pragma comment(lib, "bcrypt.lib")

bool EncryptedFileReader::HexToBytes(const std::wstring& hex, unsigned char* out, size_t outLen) {
    if (hex.length() != outLen * 2) return false;
    for (size_t i = 0; i < outLen; i++) {
        wchar_t high = hex[i * 2];
        wchar_t low = hex[i * 2 + 1];
        auto hexVal = [](wchar_t c) -> int {
            if (c >= L'0' && c <= L'9') return c - L'0';
            if (c >= L'A' && c <= L'F') return c - L'A' + 10;
            if (c >= L'a' && c <= L'f') return c - L'a' + 10;
            return -1;
        };
        int h = hexVal(high);
        int l = hexVal(low);
        if (h < 0 || l < 0) return false;
        out[i] = (unsigned char)((h << 4) | l);
    }
    return true;
}

std::wstring EncryptedFileReader::GetKeyFilePath() const {
    std::wstring keyPath = m_encFilePath;
    size_t dotPos = keyPath.find_last_of(L'.');
    if (dotPos != std::wstring::npos) {
        keyPath = keyPath.substr(0, dotPos);
    }
    keyPath += L".key";
    return keyPath;
}

std::vector<char> EncryptedFileReader::Read() {
    // 1. 读取 .key 文件
    std::wstring keyFilePath = GetKeyFilePath();
    HANDLE hKeyFile = CreateFileW(
        keyFilePath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    if (hKeyFile == INVALID_HANDLE_VALUE) {
        return {};
    }

    DWORD keyFileSize = GetFileSize(hKeyFile, nullptr);
    if (keyFileSize == INVALID_FILE_SIZE || keyFileSize == 0) {
        CloseHandle(hKeyFile);
        return {};
    }

    std::vector<char> keyFileData(keyFileSize + 1);
    DWORD bytesRead;
    ReadFile(hKeyFile, keyFileData.data(), keyFileSize, &bytesRead, nullptr);
    CloseHandle(hKeyFile);
    keyFileData[bytesRead] = '\0';

    // 解析 key 和 nonce（每行一个 hex 字符串）
    std::string content(keyFileData.data(), bytesRead);
    size_t nl1 = content.find('\n');
    if (nl1 == std::string::npos) return {};
    size_t nl2 = content.find('\n', nl1 + 1);
    if (nl2 == std::string::npos) return {};

    std::string keyHexStr = content.substr(0, nl1);
    std::string nonceHexStr = content.substr(nl1 + 1, nl2 - nl1 - 1);

    // 去除可能的 \r
    auto trimCR = [](std::string& s) {
        if (!s.empty() && s.back() == '\r') s.pop_back();
    };
    trimCR(keyHexStr);
    trimCR(nonceHexStr);

    // 转为宽字符串
    int wLen = MultiByteToWideChar(CP_UTF8, 0, keyHexStr.c_str(), -1, nullptr, 0);
    std::vector<wchar_t> keyHexW(wLen);
    MultiByteToWideChar(CP_UTF8, 0, keyHexStr.c_str(), -1, keyHexW.data(), wLen);

    wLen = MultiByteToWideChar(CP_UTF8, 0, nonceHexStr.c_str(), -1, nullptr, 0);
    std::vector<wchar_t> nonceHexW(wLen);
    MultiByteToWideChar(CP_UTF8, 0, nonceHexStr.c_str(), -1, nonceHexW.data(), wLen);

    unsigned char key[32];
    unsigned char nonce[12];
    if (!HexToBytes(keyHexW.data(), key, sizeof(key))) return {};
    if (!HexToBytes(nonceHexW.data(), nonce, sizeof(nonce))) return {};

    // 2. 读取 .enc 文件
    HANDLE hEncFile = CreateFileW(
        m_encFilePath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    if (hEncFile == INVALID_HANDLE_VALUE) return {};

    DWORD encFileSize = GetFileSize(hEncFile, nullptr);
    if (encFileSize == INVALID_FILE_SIZE || encFileSize <= 12 + 16) {
        // 至少需要 nonce(12) + tag(16) + 一些密文
        CloseHandle(hEncFile);
        return {};
    }

    std::vector<unsigned char> encData(encFileSize);
    ReadFile(hEncFile, encData.data(), encFileSize, &bytesRead, nullptr);
    CloseHandle(hEncFile);

    // 文件格式: [nonce(12)][tag(16)][ciphertext]
    const size_t nonceOffset = 12;
    const size_t tagOffset = nonceOffset + 16;
    const size_t ciphertextSize = encFileSize - tagOffset;

    unsigned char* fileNonce = encData.data();
    unsigned char* fileTag = encData.data() + nonceOffset;
    unsigned char* ciphertext = encData.data() + tagOffset;

    // 3. 打开 AES-GCM 算法提供者
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0);
    if (!BCRYPT_SUCCESS(status)) return {};

    status = BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE,
                               (PUCHAR)BCRYPT_CHAIN_MODE_GCM, sizeof(BCRYPT_CHAIN_MODE_GCM), 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return {};
    }

    // 4. 生成对称密钥
    BCRYPT_KEY_HANDLE hKey = nullptr;
    status = BCryptGenerateSymmetricKey(hAlg, &hKey, nullptr, 0, key, sizeof(key), 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return {};
    }

    // 5. 解密
    std::vector<unsigned char> plaintext(ciphertextSize);

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
    BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
    authInfo.pbNonce = fileNonce;
    authInfo.cbNonce = 12;
    authInfo.pbTag = fileTag;
    authInfo.cbTag = 16;

    ULONG cbResult = 0;
    status = BCryptDecrypt(hKey,
                           ciphertext, (ULONG)ciphertextSize,
                           &authInfo,
                           nullptr, 0,
                           plaintext.data(), (ULONG)plaintext.size(),
                           &cbResult, 0);

    BCryptDestroyKey(hKey);
    BCryptCloseAlgorithmProvider(hAlg, 0);

    // 清零密钥
    SecureZeroMemory(key, sizeof(key));

    if (!BCRYPT_SUCCESS(status)) {
        m_output.WriteFmt(L"警告: 解密上次结果失败 (0x%08X)，可能密钥不匹配。\n", status);
        return {};
    }

    // 转为 vector<char>
    std::vector<char> result(plaintext.begin(), plaintext.end());
    return result;
}
