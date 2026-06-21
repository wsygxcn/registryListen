#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <cstdarg>

// 输出写入器接口（策略模式）：抽象所有输出目标
class IOutputWriter {
public:
    virtual ~IOutputWriter() = default;

    // 写入宽字符串（不换行）
    virtual void Write(const wchar_t* str) = 0;

    // 格式化写入
    void WriteFmt(const wchar_t* fmt, ...) {
        wchar_t buf[4096];
        va_list args;
        va_start(args, fmt);
        wvsprintfW(buf, fmt, args);
        va_end(args);
        Write(buf);
    }
};

// 控制台输出：将宽字符串转为 ANSI/GBK 编码输出到 stdout
class ConsoleWriter : public IOutputWriter {
public:
    ConsoleWriter() : m_hOut(GetStdHandle(STD_OUTPUT_HANDLE)) {}

    void Write(const wchar_t* str) override {
        int len = WideCharToMultiByte(CP_ACP, 0, str, -1, nullptr, 0, nullptr, nullptr);
        if (len <= 0) return;
        std::vector<char> buf(len);
        WideCharToMultiByte(CP_ACP, 0, str, -1, buf.data(), len, nullptr, nullptr);
        DWORD written;
        WriteFile(m_hOut, buf.data(), len - 1, &written, nullptr);
    }

private:
    HANDLE m_hOut;
};

// 内存缓冲区输出：将宽字符串转为 UTF-8 追加到内存
class BufferWriter : public IOutputWriter {
public:
    void Write(const wchar_t* str) override {
        int utf8Len = WideCharToMultiByte(CP_UTF8, 0, str, -1, nullptr, 0, nullptr, nullptr);
        if (utf8Len <= 0) return;
        size_t oldSize = m_buffer.size();
        m_buffer.resize(oldSize + utf8Len - 1);
        WideCharToMultiByte(CP_UTF8, 0, str, -1, m_buffer.data() + oldSize, utf8Len, nullptr, nullptr);
    }

    const std::vector<char>& GetBuffer() const { return m_buffer; }
    bool IsEmpty() const { return m_buffer.empty(); }

private:
    std::vector<char> m_buffer;
};

// 组合输出器（组合模式）：将输出同时转发到多个写入器
class CompositeWriter : public IOutputWriter {
public:
    void AddWriter(IOutputWriter* writer) {
        m_writers.push_back(writer);
    }

    void Write(const wchar_t* str) override {
        for (auto* w : m_writers) {
            w->Write(str);
        }
    }

private:
    std::vector<IOutputWriter*> m_writers;
};
