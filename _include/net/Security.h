#ifndef _SECURITY_H_
#define _SECURITY_H_

#include <sys/time.h>  // for gettimeofday, struct timeval
#include <stdint.h>    // for uint32_t, uint64_t
#include <cstddef>     // for NULL
#include <atomic>      // for std::atomic
#include "base/Logger.h"  // for logging

class Security {
public:
    Security(uint32_t shortInterval = 100,    // 短时间窗口(毫秒)
            uint32_t shortMaxRequests = 10,   // 短时间窗口最大请求数
            uint32_t longInterval = 60,       // 长时间窗口(秒)
            uint32_t longMaxRequests = 1000,  // 长时间窗口最大请求数
            uint32_t maxSendCount = 1000)     // 最大发送队列长度
        : SHORT_INTERVAL(shortInterval)
        , SHORT_MAX_REQUESTS(shortMaxRequests)
        , LONG_INTERVAL(longInterval)
        , LONG_MAX_REQUESTS(longMaxRequests)
        , MAX_SEND_COUNT(maxSendCount)
        , sendCount(0)
        , sequence(0) {
        reset();
    }

    bool checkFlood() {
        struct timeval now;
        gettimeofday(&now, NULL);
        uint64_t currentTimeMs = now.tv_sec * 1000 + now.tv_usec / 1000;
        uint64_t currentTimeSec = now.tv_sec;

        // 短时间窗口检查（毫秒级）
        if (currentTimeMs - lastShortCheckTime < SHORT_INTERVAL) {
            shortRequestCount++;
            if (shortRequestCount > SHORT_MAX_REQUESTS) {
                Logger::GetInstance()->logSystem(LogLevel::WARN, 
                    "Short interval flood attack detected: %u requests in %u ms", 
                    shortRequestCount, SHORT_INTERVAL);
                return true;
            }
        } else {
            shortRequestCount = 1;
            lastShortCheckTime = currentTimeMs;
        }

        // 长时间窗口检查（秒级）
        if (currentTimeSec - lastLongCheckTime >= LONG_INTERVAL) {
            longRequestCount = 1;
            lastLongCheckTime = currentTimeSec;
        } else {
            longRequestCount++;
            if (longRequestCount > LONG_MAX_REQUESTS) {
                Logger::GetInstance()->logSystem(LogLevel::WARN, 
                    "Long interval flood attack detected: %u requests in %u s", 
                    longRequestCount, LONG_INTERVAL);
                return true;
            }
        }

        return false;
    }

    // 发送队列相关方法
    bool checkSendQueueOverflow() const {
        return sendCount.load() > MAX_SEND_COUNT;
    }

    void incrementSendCount() {
        ++sendCount;
    }

    void decrementSendCount() {
        if (sendCount > 0) {
            --sendCount;
        }
    }

    uint32_t getSendCount() const {
        return sendCount.load();
    }

    // 序列号相关方法
    uint64_t getSequence() const {
        return sequence;
    }

    void incrementSequence() {
        ++sequence;
    }

    void reset() {
        shortRequestCount = 0;
        longRequestCount = 0;
        lastShortCheckTime = 0;
        lastLongCheckTime = 0;
        sendCount = 0;
        incrementSequence(); // 序列号+1表示新的连接
    }

private:
    // Flood检测相关
    const uint32_t SHORT_INTERVAL;      // 短时间窗口(毫秒)
    const uint32_t SHORT_MAX_REQUESTS;  // 短时间窗口最大请求数
    const uint32_t LONG_INTERVAL;       // 长时间窗口(秒)
    const uint32_t LONG_MAX_REQUESTS;   // 长时间窗口最大请求数
    const uint32_t MAX_SEND_COUNT;      // 最大发送队列长度

    uint32_t shortRequestCount;         // 短时间窗口请求计数
    uint32_t longRequestCount;          // 长时间窗口请求计数
    uint64_t lastShortCheckTime;        // 上次短时间窗口检查时间(毫秒)
    uint64_t lastLongCheckTime;         // 上次长时间窗口检查时间(秒)

    std::atomic<uint32_t> sendCount;    // 发送队列计数
    uint64_t sequence;                  // 连接序列号
};

#endif // _SECURITY_H_