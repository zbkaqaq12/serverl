#pragma once
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string>
#include "base/Logger.h"

/**
 * @class Daemon
 * @brief 守护进程类，用于将进程转换为守护进程
 * 
 * 通过 fork()、setsid() 和重定向标准流到 /dev/null 来实现进程转换为守护进程
 */
class Daemon {
public:
    /**
     * @brief 构造函数，初始化守护进程信息
     * 
     * @param pid 子进程的 PID
     * @param ppid 父进程的 PID
     */
    Daemon(pid_t pid, pid_t ppid)
        : pid_(pid), ppid_(ppid), fd_(-1) {}

    /**
     * @brief 初始化守护进程，创建子进程、分离终端和重定向流
     * 
     * 创建子进程并分离终端，成功后将标准输入、输出和错误重定向到 /dev/null
     * 
     * @return 返回 1 表示父进程，返回 0 表示子进程，返回 -1 表示初始化失败
     */
    int init() {
        auto logger = Logger::GetInstance();

        // 第一次 fork() 创建子进程
        pid_t pid = fork();
        if (pid == -1) {
            logger->logSystem(LogLevel::FATAL, "Failed to fork daemon process");
            return -1;
        }

        if (pid > 0) {
            return 1;  // 父进程直接退出
        }

        // 子进程
        if (setsid() == -1) {
            logger->logSystem(LogLevel::FATAL, "Failed to create new session for daemon process");
            return -1;
        }

        umask(0);  // 重置文件创建掩码，确保文件权限不受限制

        // 重定向标准流到 /dev/null
        if (!openNullDevice()) {
            return -1;
        }

        return 0;  // 子进程成功创建
    }

private:
    pid_t pid_;    // 子进程 PID
    pid_t ppid_;   // 父进程 PID
    int fd_;       // /dev/null 的文件描述符

    /**
     * @brief 打开 /dev/null 设备并重定向标准输入、输出和错误流
     * 
     * @return 操作成功返回 true，否则返回 false
     */
    bool openNullDevice() {
        auto logger = Logger::GetInstance();

        fd_ = open("/dev/null", O_RDWR);
        if (fd_ == -1) {
            logger->logSystem(LogLevel::ERROR, "Failed to open /dev/null");
            return false;
        }

        // 重定向标准输入、输出和错误流到 /dev/null
        if (!redirectStream(STDIN_FILENO) || 
            !redirectStream(STDOUT_FILENO) || 
            !redirectStream(STDERR_FILENO)) {
            return false;
        }

        return true;
    }

    /**
     * @brief 重定向标准流到 /dev/null
     * 
     * @param stream 要重定向的流，可以是 STDIN_FILENO、STDOUT_FILENO 或 STDERR_FILENO
     * @return 重定向成功返回 true，否则返回 false
     */
    bool redirectStream(int stream) {
        auto logger = Logger::GetInstance();

        if (dup2(fd_, stream) == -1) {
            logger->logSystem(LogLevel::ERROR, 
                "Failed to redirect stream %d to /dev/null", stream);
            return false;
        }
        return true;
    }
};
