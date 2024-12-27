#include "MasterProcess.h"
#include <unistd.h>
#include<sys/types.h>

/**
* @brief 启动主进程，初始化信号处理并启动工作进程。
*
* 1. 初始化信号处理器并设置信号处理函数；
* 2. 设置主进程标题；
* 3. 从配置文件中读取工作进程数并启动相应数量的工作进程；
* 4. 进入等待信号的状态。
*/
void MasterProcess::start()
{
    // 初始化信号处理器
    if (signalHandler_->init_signal() == -1) {
        auto logger = Logger::GetInstance();
        logger->logSystem(LogLevel::ERROR, "信号初始化失败!");
        return;
    }

    // 设置需要处理的信号
    signalHandler_->unmask_and_set_handler(SIGTERM, handleSIGTERM);
    signalHandler_->unmask_and_set_handler(SIGINT, handleSIGINT);
    signalHandler_->unmask_and_set_handler(SIGCHLD, handleSIGCHLD);
    signalHandler_->unmask_and_set_handler(SIGHUP, handleSIGHUP);

    // 设置主进程标题
    set_process_title();

    // 从配置文件中读取工作进程数量
    auto config = Config::GetInstance();
    int worker_count = config->GetIntDefault("WorkerProcesses", 1);
    start_worker_processes(worker_count);

    // 等待信号处理
    wait_for_signal();
}

/**
* @brief 设置主进程标题。
*
* 设置主进程的进程标题，使其更可读性和可管理性。主要用于在进程列表中显示。
*/
void MasterProcess::set_process_title()
{
    // 设置主进程标题
    size_t size;
    int    i;
    size = sizeof(processName_);  //注意我这里用的是sizeof，所以字符串末尾的\0是被计算进来了
    size += g_argvneedmem;          //argv参数长度加进来    
    if (size < 1000) //长度小于这个才设置标题
    {
        char title[1000] = { 0 };
        strcpy(title, processName_.c_str()); //"master process"
        strcat(title, " ");  //跟一个空格分开一些，清晰    //"master process "
        for (i = 0; i < g_os_argc; i++)         //"master process ./nginx"
        {
            strcat(title, g_os_argv[i]);
        }//end for
        setproctitle(title); //设置标题
    }
}

/**
* @brief 启动并创建一个新的工作进程。
*
* @param num 工作进程编号，用于区分不同的工作进程。
*/
void MasterProcess::spawn_worker_process(int num)
{
    pid_t pid = fork();
    if (pid == -1) {
        auto logger = Logger::GetInstance();
        logger->logSystem(LogLevel::ERROR, "fork()创建子进程失败!");
        return;
    }

    if (pid == 0) {
        // 子进程处理
        try {
            // 创建 WorkerProcess 实例
            std::unique_ptr<WorkerProcess> worker = std::make_unique<WorkerProcess>();
            // 确保 worker 不为 nullptr
            if (worker) {
                worker->start();  // 启动工作进程
            }
            else {
                std::cerr << "Failed to create WorkerProcess instance." << std::endl;
                exit(1);
            }

        }
        catch (const std::exception& e) {
            std::cerr << "Exception in child process: " << e.what() << std::endl;
            exit(1);  // 发生异常时退出子进程
        }

        exit(0);  // 子进程退出
    }
    // 父进程继续执行
}

/**
 * @brief 处理 SIGTERM 信号。
 *
 * 当进程收到 SIGTERM 信号时，执行清理工作，通常用于平滑终止进程。
 * 在该函数中，会停止所有工作进程，释放资源等。
 *
 * @param signo 信号编号，表示接收到的信号类型（此时是 SIGTERM）。
 * @param siginfo 信号信息，包含发送信号的更多信息。
 * @param ucontext 上下文信息，包含当信号处理程序被调用时的寄存器状态等。
 */
void MasterProcess::handleSIGTERM(int signo, siginfo_t* siginfo, void* ucontext)
{
    auto logger = Logger::GetInstance();
    logger->logSystem(LogLevel::INFO, "收到 SIGTERM 信号，正在清理资源");
    // 这里执行清理工作，如停止工作进程的
    // 清理子进程、日志等资源
}

/**
 * @brief 处理 SIGINT 信号。
 *
 * 当进程收到 SIGINT 信号时，停止日志处理并进行资源释放，通常由用户通过中断（Ctrl+C）产生的。
 * 在该函数中，会停止所有工作进程、清理资源、关闭日志等。
 *
 * @param signo 信号编号，表示接收到的信号类型（此时是 SIGINT）。
 * @param siginfo 信号信息，包含发送信号的更多信息。
 * @param ucontext 上下文信息，包含当信号处理程序被调用时的寄存器状态等。
 */
void MasterProcess::handleSIGINT(int signo, siginfo_t* siginfo, void* ucontext)
{
    auto logger = Logger::GetInstance();
    logger->stoplogprocess();
    logger->logSystem(LogLevel::INFO, "收到 SIGINT 信号，正在退出程序");
    // 中断信号后执行资源释放和进程终止
    // 停止所有工作进程、清理资源等
}

/**
 * @brief 处理 SIGCHLD 信号。
 *
 * 当子进程结束时，父进程会收到 SIGCHLD 信号。该函数会处理子进程的退出状态，防止产生僵尸进程。
 * 函数使用 `waitpid` 来检查子进程状态并记录退出信息。
 *
 * @param signo 信号编号，表示接收到的信号类型（此时是 SIGCHLD）。
 * @param siginfo 信号信息，包含发送信号的更多信息。
 * @param ucontext 上下文信息，包含当信号处理程序被调用时的寄存器状态等。
 */
void MasterProcess::handleSIGCHLD(int signo, siginfo_t* siginfo, void* ucontext)
{
    pid_t pid;
    int status;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        if (WIFEXITED(status)) {
            std::cout << "Child process " << pid << " exited with status " << WEXITSTATUS(status) << std::endl;
        }
        else if (WIFSIGNALED(status)) {
            std::cout << "Child process " << pid << " killed by signal " << WTERMSIG(status) << std::endl;
        }
    }
}

/**
 * @brief 处理 SIGHUP 信号。
 *
 * 主进程接收到 SIGHUP 信号时，通常意味着需要重新加载配置文件或者执行其他指定的恢复操作。
 * 函数在该函数中加载新配置等的逻辑，或者进行其他需要的恢复操作。
 *
 * @param signo 信号编号，表示接收到的信号类型（此时是 SIGHUP）。
 * @param siginfo 信号信息，包含发送信号的更多信息。
 * @param ucontext 上下文信息，包含当信号处理程序被调用时的寄存器状态等。
 */
void MasterProcess::handleSIGHUP(int signo, siginfo_t* siginfo, void* ucontext)
{
    auto logger = Logger::GetInstance();
    logger->logSystem(LogLevel::INFO, "接收到 SIGHUP 信号，重新加载配置");
    // 在这里处理重新加载配置文件、执行恢复操作等
}

