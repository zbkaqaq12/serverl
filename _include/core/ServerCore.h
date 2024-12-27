#pragma once
#include "Logger.h"
#include "global.h"
#include "Config.h"
#include "CThreadPool.h"
#include "MasterProcess.h"
#include "Daemon.h"
#include <memory>
#include <stdexcept>


class ServerCore {
public:
    static ServerCore& getInstance() {
        static ServerCore instance;
        return instance;
    }

    // 初始化服务器
    void initialize(int argc, char* const* argv, const std::string& configPath) {
        try {
            // 1. 初始化基本变量
            initBasicVars(argc, argv);
            
            // 2. 初始化核心组件
            initConfig(configPath);
            initLogger();
            
            // 3. 初始化网络组件
            if (!g_socket.Initialize()) {
                throw std::runtime_error("Failed to initialize socket");
            }

            // 4. 设置进程标题
            initProcessTitle();
            
            // 5. 初始化守护进程（如果需要）
            initDaemon();
            

            initialized_ = true;
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("Failed to initialize server core: ") + e.what());
        }
    }

    // 运行服务器
    void run() {
        checkInitialized();
        
        try {
            // 创建并启动主进程
            MasterProcess master;
            master.start();
        } catch (const std::exception& e) {
            getLogger()->logSystem(LogLevel::FATAL, "Server failed: %s", e.what());
            throw;
        }
    }

    // 清理资源
    void cleanup() {
        if (initialized_) {
            // 清理日志系统
            if (auto logger = Logger::GetInstance()) {
                logger->stoplogprocess();
            }

            // 释放环境变量内存
            if (gp_envmem) {
                delete[] gp_envmem;
                gp_envmem = nullptr;
            }

            initialized_ = false;
        }
    }

    // 获取器方法
    Config* getConfig() const { 
        checkInitialized();
        return Config::GetInstance(); 
    }

    Logger* getLogger() const { 
        checkInitialized();
        return Logger::GetInstance(); 
    }

    // 添加进程相关的访问器
    pid_t getPid() const { return severl_pid; }
    pid_t getParentPid() const { return severl_parent; }
    int getProcessType() const { return severl_process; }
    void setProcessType(int type) { 
        severl_process = type;  // 同步更新全局变量
    }

    ~ServerCore() {
        cleanup();
    }

private:
    ServerCore() = default;
    ServerCore(const ServerCore&) = delete;
    ServerCore& operator=(const ServerCore&) = delete;

    void initBasicVars(int argc, char* const* argv) {
        g_stopEvent = 0;
        severl_pid = getpid();
        severl_parent = getppid();
        severl_process = 0;  // 初始为master进程


        // 计算需要的内存
        g_argvneedmem = 0;
        for (int i = 0; i < argc; i++) {
            g_argvneedmem += strlen(argv[i]) + 1;
        }

        for (int i = 0; environ[i]; i++) {
            g_envneedmem += strlen(environ[i]) + 1;
        }

        g_os_argc = argc;
        g_os_argv = (char**)argv;
    }

    void initConfig(const std::string& configPath) {
        if (!Config::GetInstance()->Load(configPath.c_str())) {
            throw std::runtime_error("Failed to load configuration");
        }
    }

    void initLogger() {
        if (!Logger::GetInstance()->log_init()) {
            throw std::runtime_error("Failed to initialize logger");
        }
    }

    void initProcessTitle() {
        // 实现进程标题设置
        init_setproctitle();
    }

    void initDaemon() {
        auto config = getConfig();
        if (config->GetIntDefault("Proc.Daemon", 0) == 1) {
            Daemon daemon(severl_pid, severl_parent);
            int result = daemon.init();
            if (result == -1) {
                getLogger()->logSystem(LogLevel::FATAL, "Daemon initialization failed");
                throw std::runtime_error("Daemon initialization failed");
            }
        }
    }

    void checkInitialized() const {
        if (!initialized_) {
            throw std::runtime_error("ServerCore not initialized");
        }
    }

    bool initialized_ = false;
    pid_t severl_pid;
    pid_t severl_parent;
    int   severl_process;
};

// 全局访问器函数
inline Config* getConfig() {
    return ServerCore::getInstance().getConfig();
}

inline Logger* getLogger() {
    return ServerCore::getInstance().getLogger();
} 

