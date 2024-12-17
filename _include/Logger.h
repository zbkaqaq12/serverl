#pragma once
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <chrono>
#include <iomanip>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <memory>
#include <stdexcept>
#include <cstdarg>
#include <functional>
#include <ctime>
#include <filesystem>

/**
 * @sa LogLevel
 * @brief 定义日志的不同级别
 */
enum class LogLevel {
    EMERG = 0,   // 紧急情况
    ALERT,       // 严重警告
    CRIT,        // 关键错误
    ERROR,       // 一般错误
    WARN,        // 警告信息
    NOTICE,      // 提示信息
    INFO,        // 一般信息
    DEBUG        // 调试信息
};

/**
 * @brief 将 LogLevel 转换为对应的字符串表示形式
 *
 * @param level 日志级别
 * @return const char* 对应日志级别的字符串表示形式
 */
inline const char* logLevelToString(LogLevel level) {
    switch (level) {
    case LogLevel::EMERG: return "emerg";
    case LogLevel::ALERT: return "alert";
    case LogLevel::CRIT: return "crit";
    case LogLevel::ERROR: return "error";
    case LogLevel::WARN: return "warn";
    case LogLevel::NOTICE: return "notice";
    case LogLevel::INFO: return "info";
    case LogLevel::DEBUG: return "debug";
    default: return "unknown";
    }
}

/**
 * @sa LogHandler
 * @brief 日志处理器的基类，用于处理不同类型的日志输出
 *
 * 提供一个虚函数接口，供不同的日志处理器实现。
 */
class LogHandler{
public:
    virtual ~LogHandler() = default;
    virtual void handleLog(LogLevel, const std::string& message) = 0;
    virtual void stoplog() {};
};

/**
 * @sa ConsoleLogHandler
 * @brief 控制台日志处理器，将日志输出到标准控制台
 */
class ConsoleLogHandler :public LogHandler {
public:
    /**
     * @brief 处理日志消息，输出到控制台
     *
     * @param level 日志级别
     * @param message 日志消息
     */
    void handleLog(LogLevel level, const std::string& message) override
    {
        // 获取当前时间
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::tm tm;
        localtime_r(&time, &tm);

        // 格式化时间
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y/%m/%d %H:%M:%S");

        // 输出日志消息
        std::cout << oss.str() << " [" << logLevelToString(level) << "] " << message << std::endl;
    }

    void stoplog() override {}
};

/**
 * @sa FileLogHandler
 * @brief 文件日志处理器，将日志输出到指定的日志文件中
 */
class FileLogHandler :public LogHandler {
public:
    /**
     * @brief 构造函数，初始化日志文件处理器，并启动异步日志处理线程
     *
     * @param fileName 日志文件的文件名
     */
    explicit FileLogHandler(const std::string& fileName)
        : logFile_(fileName, std::ios::app), stopLogging_(false)
    {
        // 启动异步日志处理线程
        logThread_ = std::thread(&FileLogHandler::processLogQueue, this);
    }

    /**
     * @brief 停止日志处理，停止异步日志处理线程
     */
    void stoplog()
    {
        stopLogging_ = true;
        // Push a special stop message to the queue
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            logQueue_.push({ LogLevel::INFO, "" });  // Empty string as a stop signal
        }
        cv_.notify_all();
        if (logThread_.joinable()) {
            logThread_.join();
        }
    }

    ~FileLogHandler() {
        stoplog();
    }

    /**
     * @brief 处理日志消息，输出到日志文件
     *
     * @param level 日志级别
     * @param message 日志消息
     */
    void handleLog(LogLevel level, const std::string& message) override {
        // 将日志消息添加到队列中，等待异步日志处理线程处理
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            logQueue_.push({ level, message });
        }
        cv_.notify_all();
    }

private:
    /**
    * @brief 异步日志处理线程，处理日志队列中的日志消息，输出到日志文件
    */
    void processLogQueue() {
        while (!stopLogging_) {
            std::unique_lock<std::mutex> lock(queueMutex_);
            cv_.wait(lock, [this] { return !logQueue_.empty() || stopLogging_; });

            while (!logQueue_.empty()) {
                auto logItem = logQueue_.front();
                logQueue_.pop();
                lock.unlock();

                if (logItem.second.empty()) {
                    break;  // Stop signal
                }

                LogLevel level = logItem.first;
                const std::string& message = logItem.second;

                // 获取当前时间
                auto now = std::chrono::system_clock::now();
                auto time = std::chrono::system_clock::to_time_t(now);
                std::tm tm;
                localtime_r(&time, &tm);

                // 格式化时间
                std::ostringstream oss;
                oss << std::put_time(&tm, "%Y/%m/%d %H:%M:%S");

                // 输出日志消息到日志文件
                if (logFile_.is_open()) {
                    logFile_ << oss.str() << " [" << logLevelToString(level) << "] " << message << std::endl;
                }

                lock.lock();
            }
        }

        if (logFile_.is_open()) {
            logFile_.close();
        }
    }

    std::ofstream logFile_; /**< 日志文件流对象 */
    std::queue<std::pair<LogLevel, std::string>> logQueue_; /**< 日志队列 */
    std::mutex queueMutex_; /**< 用于同步日志队列的互斥锁 */
    std::condition_variable cv_; /**< 用于同步日志队列的条件变量 */
    std::atomic<bool> stopLogging_; /**< 控制日志处理停止的标志 */
    std::thread logThread_; /**< 异步日志处理线程 */
};


/**
 * @sa Logger
 * @brief 日志管理类，提供日志记录的功能
 *
 * 提供控制台日志和文件日志的处理功能，并支持多线程异步处理
 */
class Logger {
private:
    Logger()
    {
        
    }

public:

    ~Logger() {
    }

    static Logger* GetInstance()
    {
        if (m_instance == nullptr)
        {
            if (m_instance == nullptr)
            {
                m_instance = new Logger();
                static Garhuishou c1;
            }
        }
        return m_instance;
    }

    // 删除复制构造函数和赋值操作符
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    class Garhuishou  // 垃圾回收类，用于释放对象
    {
    public:
        ~Garhuishou()
        {
            if (Logger::m_instance)
            {
                delete Logger::m_instance;
                Logger::m_instance = nullptr;
            }
        }
    };

    bool log_init();

    // 获取日志级别
    /*LogLevel getLogLevel() const {
        return logLevel_;
    }*/
    
    // 记录日志
    void flog(LogLevel level, const char* fmt, ...);  // 格式化日志模式
    void clog(LogLevel level, const char* fmt, ...);   // 控制台模式

    // 停止异步日志处理线程
    void stoplogprocess()
    {
        fileHandler_->stoplog();
    }
    

private:
    // 异步日志处理线程
    //void processLogQueue();

    // 格式化字符串
    std::string format(const char* fmt, va_list args) {
        int size = std::vsnprintf(nullptr, 0, fmt, args) + 1;
        std::unique_ptr<char[]> buffer(new char[size]);
        std::vsnprintf(buffer.get(), size, fmt, args);
        return std::string(buffer.get());
    }

    static Logger* m_instance; /**< Logger 实例 */
    std::string logFile_; /**< 日志文件名 */
    LogLevel logLevel_; /**< 日志级别 */
    std::shared_ptr<LogHandler> consoleHandler_; /**< 控制台日志处理器 */
    std::shared_ptr<LogHandler> fileHandler_; /**< 文件日志处理器 */
    
};


