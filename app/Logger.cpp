#include "Logger.h"
#include"global.h"

//静态成员赋值
Logger* Logger::m_instance = nullptr;

/**
 * @brief 初始化日志系统，包括文件名、日志级别、日志文件等
 *
 * 检查配置是否已初始化，设置日志文件路径、日志级别，创建控制台和文件日志处理器
 *
 * @return true 如果初始化成功
 * @return false 如果初始化失败，如无法打开日志文件等
 */
bool Logger::log_init()
{
    if (globalconfig == nullptr) {
        throw std::runtime_error("Config is not initialized");
        return false;
    }
    
    logFile_ = globalconfig->GetString("LogFile");
    logLevel_ = static_cast<LogLevel>(globalconfig->GetIntDefault("LogLevel", -1));

    std::ofstream logFileStream(logFile_, std::ios::app);  // 以追加模式打开日志文件
    if (!logFileStream.is_open()) {
        // 如果文件无法打开，返回 false
        return false;
    }
    logFileStream.close();  // 检查完成后立即关闭文件
    consoleHandler_ = std::make_shared<ConsoleLogHandler>();
    fileHandler_ = std::make_shared<FileLogHandler>(logFile_);
    if (fileHandler_ == nullptr)
    {
        return false;
    }
    return true;
}

/**
 * @brief 记录日志到日志文件
 *
 * 根据日志级别和格式化字符串，格式化日志信息并将日志信息写入文件
 *
 * @param level 日志级别
 * @param fmt 格式化字符串
 * @param ... 可变参数，日志内容
 */
void Logger::flog(LogLevel level, const char* fmt, ...)
{
    if (level > logLevel_) return;
    va_list args;
    va_start(args, fmt);
    std::string message = format(fmt, args);
    va_end(args);

    if (fileHandler_) {
        fileHandler_->handleLog(level, message);  // 发送到文件处理器
    }
}

/**
 * @brief 记录日志到控制台
 *
 * 根据日志级别和格式化字符串，格式化日志信息并将日志信息输出到控制台
 *
 * @param level 日志级别
 * @param fmt 格式化字符串
 * @param ... 可变参数，日志内容
 */
void Logger::clog(LogLevel level, const char* fmt, ...)
{
    if (level > logLevel_) return;
    va_list args;
    va_start(args, fmt);
    std::string message = format(fmt, args);
    va_end(args);

    if (consoleHandler_) {
        consoleHandler_->handleLog(level, message);  // 发送到控制台处理器
    }
}
