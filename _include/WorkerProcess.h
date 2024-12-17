#pragma once
#include <iostream>
#include <unistd.h>
#include <signal.h>
#include <cstring>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include"global.h"
#include"func.h"
#include"MSignal.h"
#include <sys/prctl.h>

/**
 * @brief 工作进程（子进程）的类
 *
 * 该类表示一个工作进程，负责初始化并处理信号。执行任务。工作进程会继承父进程的信号处理机制，并响应特定信号（如终止信号、用户定义信号等）
 */
class WorkerProcess
{
public:
    /**
     * @brief 构造函数，初始化 WorkerProcess 对象
     *
     * @param processName 进程名称，默认为 "workerserverl"
     */
    WorkerProcess(std::string processName = "workerserverl")
        :processName_(processName), signalHandler_(std::make_unique<MSignal>())
    {
    }

    /**
     * @brief 启动工作进程
     *
     * 初始化进程并开始工作。此函数负责初始化进程环境，并调用 `run()` 进入事件循环
     */
    void start() {
        // 子进程的初始化工作
        init_process();
        globallogger->clog(LogLevel::INFO, "Worker process init complete...");
        run();
    }
    
private:
    std::string processName_;  ///< 进程名称
    std::unique_ptr<MSignal> signalHandler_;  ///< 信号处理器对象

    void init_process();

    void set_process_title();

    void run();

    // 静态信号处理函数声明
    static void handleSIGTERM(int signo, siginfo_t* siginfo, void* ucontext);
    static void handleSIGINT(int signo, siginfo_t* siginfo, void* ucontext);
    static void handleSIGHUP(int signo, siginfo_t* siginfo, void* ucontext);
    static void handleSIGUSR1(int signo, siginfo_t* siginfo, void* ucontext);
    static void handleSIGUSR2(int signo, siginfo_t* siginfo, void* ucontext);
};

