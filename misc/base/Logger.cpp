#include "Logger.h"
#include "global.h"
#include "Config.h"

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
    auto config = Config::GetInstance();
    if (!config) {
        return false;
    }

    try {
        // 获取日志配置
        logFile_ = config->GetString("Log.LogFile");
        logLevel_ = static_cast<LogLevel>(config->GetIntDefault("Log.LogLevel", static_cast<int>(LogLevel::INFO)));
        
        // 解析文件大小限制
        std::string maxSizeStr = config->GetString("Log.MaxFileSize");
        max_file_size_ = parseFileSize(maxSizeStr);
        max_files_ = config->GetIntDefault("Log.MaxFiles", 10);

        // 确保日志目录存在
        std::filesystem::path logPath(logFile_);
        std::filesystem::create_directories(logPath.parent_path());

        // 初始化日志处理器
        consoleHandler_ = std::make_shared<ConsoleLogHandler>();
        fileHandler_ = std::make_shared<FileLogHandler>(logFile_);
        
        if (!fileHandler_) {
            return false;
        }

        // 记录启动日志
        logSystem(LogLevel::INFO, "Logger initialized successfully");
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize logger: " << e.what() << std::endl;
        return false;
    }
}

/**
 * @brief 解析文件大小字符串（如 "100M"）
 */
size_t Logger::parseFileSize(const std::string& sizeStr) {
    if (sizeStr.empty()) {
        return 100 * 1024 * 1024; // 默认 100MB
    }

    size_t size = 0;
    char unit = 'M'; // 默认单位为 MB

    try {
        // 分离数字和单位
        std::string numStr;
        for (char c : sizeStr) {
            if (std::isdigit(c)) {
                numStr += c;
            } else {
                unit = std::toupper(c);
                break;
            }
        }

        size = std::stoull(numStr);

        // 根据单位转换
        switch (unit) {
            case 'G': return size * 1024 * 1024 * 1024;
            case 'M': return size * 1024 * 1024;
            case 'K': return size * 1024;
            default:  return size;
        }
    } catch (const std::exception&) {
        return 100 * 1024 * 1024; // 解析失败返回默认值
    }
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

/**
 * @brief 轮转日志文件
 *
 * 如果日志文件存在，则删除最老的日志文件，并重命名现有的日志文件
 */
void Logger::rotateLogFile() {
    if (!std::filesystem::exists(logFile_)) {
        return;
    }

    // 删除最老的日志文件
    std::string oldestLog = logFile_ + "." + std::to_string(max_files_);
    if (std::filesystem::exists(oldestLog)) {
        std::filesystem::remove(oldestLog);
    }

    // 重命名现有的日志文件
    for (int i = max_files_ - 1; i >= 0; --i) {
        std::string oldName = logFile_ + (i > 0 ? "." + std::to_string(i) : "");
        std::string newName = logFile_ + "." + std::to_string(i + 1);
        if (std::filesystem::exists(oldName)) {
            std::filesystem::rename(oldName, newName);
        }
    }

    // 关闭当前日志文件并创建新文件
    if (fileHandler_) {
        fileHandler_->stoplog();
        fileHandler_ = std::make_shared<FileLogHandler>(logFile_);
    }
}

/**
 * @brief 检查日志文件大小
 *
 * 如果日志文件存在，则检查其大小是否超过最大文件大小
 *
 * @return true 如果文件大小超过最大文件大小
 * @return false 如果文件不存在或文件大小未超过最大文件大小
 */
bool Logger::checkRotate() {
    if (!std::filesystem::exists(logFile_)) {
        return false;
    }
    
    auto size = std::filesystem::file_size(logFile_);
    return size >= max_file_size_;
}

/**
 * @brief 格式化日志消息
 *
 * 根据日志级别、上下文和消息，格式化日志消息
 *
 * @param level 日志级别
 * @param ctx 日志上下文
 * @param msg 日志消息
 * @return 格式化后的日志消息
 */
std::string Logger::formatLogMessage(LogLevel level, const LogContext& ctx, const std::string& msg) {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    localtime_r(&time, &tm);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << " "
        << "[" << logLevelToString(level) << "] "
        << "[" << ctx.request_id << "] "
        << ctx.method << " " << ctx.endpoint << " "
        << "from " << ctx.client_ip << " "
        << "(" << ctx.user_agent << ") "
        << "status=" << ctx.response_code << " "
        << "time=" << std::fixed << std::setprecision(3) << ctx.process_time << "ms "
        << msg;

    return oss.str();
}

/**
 * @brief 记录API请求日志
 *
 * @param level 日志级别
 * @param ctx 请求上下文
 * @param fmt 格式化字符串
 * @param ... 可变参数
 */
void Logger::logRequest(LogLevel level, const LogContext& ctx, const char* fmt, ...) {
    if (level > logLevel_) return;

    // 格式化用户消息
    va_list args;
    va_start(args, fmt);
    std::string userMessage = format(fmt, args);
    va_end(args);

    // 检查是否需要轮转日志文件
    if (checkRotate()) {
        rotateLogFile();
    }

    // 格式化完整日志消息
    std::string fullMessage = formatLogMessage(level, ctx, userMessage);

    // 输出到文件和控制台
    if (fileHandler_) {
        fileHandler_->handleLog(level, fullMessage);
    }
    if (consoleHandler_) {
        consoleHandler_->handleLog(level, fullMessage);
    }
}

/**
 * @brief 记录性能日志
 *
 * @param endpoint API端点
 * @param process_time 处理时间(毫秒)
 */
void Logger::logPerformance(const std::string& endpoint, double process_time) {
    LogContext ctx;
    ctx.endpoint = endpoint;
    ctx.process_time = process_time;

    std::ostringstream msg;
    msg << "Performance alert: Endpoint " << endpoint 
        << " took " << process_time << "ms to process";

    // 根据处理时间决定日志级别
    LogLevel level = LogLevel::INFO;
    if (process_time > 1000) {  // 超过1秒
        level = LogLevel::WARN;
    }
    if (process_time > 3000) {  // 超过3秒
        level = LogLevel::ERROR;
    }

    logRequest(level, ctx, msg.str().c_str());
}

/**
 * @brief 记录安全相关日志
 *
 * @param level 日志级别
 * @param client_ip 客户端IP
 * @param event 安全事件描述
 */
void Logger::logSecurity(LogLevel level, const std::string& client_ip, const std::string& event) {
    LogContext ctx;
    ctx.client_ip = client_ip;
    
    std::ostringstream msg;
    msg << "Security event: " << event;

    logRequest(level, ctx, msg.str().c_str());
}
