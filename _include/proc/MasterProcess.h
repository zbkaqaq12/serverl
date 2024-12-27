#pragma once
#include <iostream>
#include <unistd.h>
#include <signal.h>
#include <cstring>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include"global.h"
#include <sys/prctl.h>
#include"WorkerProcess.h"
#include"func.h"
#include"MSignal.h"
#include"Config.h"
#include"base/Logger.h"

/**
 * @class MasterProcess
 * @brief 主进程类，用于管理和控制进程中的子进程
 *
 * 该类负责初始化信号处理、设置进程标题、创建工作进程，并在接收到信号时进行相关操作
 */
class MasterProcess
{
public:
    /**
     * @brief 构造函数，初始化主进程对象
     *
     * @param processName 进程的名称，默认为 "masterserverl"
     */
    MasterProcess(std::string processName = "masterserverl")
        : processName_(processName), signalHandler_(std::make_unique<MSignal>())
    {}

    void start();
private:
    void set_process_title();

    /**
     * @brief 创建指定数量的工作进程
     *
     * @param worker_count 工作进程的数量
     */
    void start_worker_processes(int worker_count) {
        for (int i = 0; i < worker_count; ++i) {
            spawn_worker_process(i);
        }
    }

    void spawn_worker_process(int num);

    /**
     * @brief 主进程等待信号，保持运行状态，直到接收到信号
     */
    void wait_for_signal() {
        // 主进程等待信号
        while (true) {
            pause();
        }
    }

    // 自定义信号处理函数
    static void handleSIGTERM(int signo, siginfo_t* siginfo, void* ucontext);
    static void handleSIGINT(int signo, siginfo_t* siginfo, void* ucontext);
    static void handleSIGCHLD(int signo, siginfo_t* siginfo, void* ucontext);      
    static void handleSIGHUP(int signo, siginfo_t* siginfo, void* ucontext);

private:
    std::string processName_;
    std::unique_ptr<MSignal> signalHandler_;
};

