#pragma once
#include <windows.h>

// 超时控制模块：封装超时检测逻辑，消除全局变量
class TimeoutController {
public:
    explicit TimeoutController(ULONGLONG timeoutMs = 20000)
        : m_timeoutMs(timeoutMs), m_startTime(0), m_started(false) {}

    void Start() {
        m_startTime = GetTickCount64();
        m_started = true;
    }

    bool IsTimeout() const {
        if (!m_started) return false;
        return (GetTickCount64() - m_startTime) >= m_timeoutMs;
    }

    ULONGLONG ElapsedMs() const {
        if (!m_started) return 0;
        return GetTickCount64() - m_startTime;
    }

    ULONGLONG TimeoutMs() const { return m_timeoutMs; }

private:
    ULONGLONG m_timeoutMs;
    ULONGLONG m_startTime;
    bool m_started;
};
