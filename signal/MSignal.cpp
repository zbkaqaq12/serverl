#include "MSignal.h"

/**
 * @brief 初始化信号系统，设置屏蔽，并将接收到的信号交给信号处理器处理。
 *
 * 该函数会初始化信号掩码集，设置屏蔽，当接收到信号（如 `SIGCHLD`、`SIGINT` 等），
 * 然后会给为信号列表中的信号注册相应的处理函数。
 *
 * @return int 返回 0 表示成功，返回 -1 表示失败。
 */
int MSignal::init_signal()
{
    sigemptyset(&set);

    // 设置屏蔽，接收到信号
    sigaddset(&set, SIGCHLD);     // 子进程状态改变
    sigaddset(&set, SIGALRM);     // 定时器超时
    sigaddset(&set, SIGIO);       // 异步I/O
    sigaddset(&set, SIGINT);      // 终端中断符
    sigaddset(&set, SIGHUP);      // 连接断开
    sigaddset(&set, SIGUSR1);     // 用户定义信号
    sigaddset(&set, SIGUSR2);     // 用户定义信号
    sigaddset(&set, SIGWINCH);    // 终端窗口大小改变
    sigaddset(&set, SIGTERM);     // 终止
    sigaddset(&set, SIGQUIT);     // 终端退出符
    
    // 设置信号掩码集
    if (sigprocmask(SIG_BLOCK, &set, nullptr) == -1) {
        globallogger->flog(LogLevel::ALERT, "sigprocmask()失败!");
        return -1;
    }

    for (auto& sig : signals_) {
        struct sigaction sa;
        std::memset(&sa, 0, sizeof(struct sigaction));

        if (sig.handler) {
            sa.sa_sigaction = sig.handler;
            sa.sa_flags = SA_SIGINFO;
        }
        else {
            sa.sa_handler = SIG_IGN;
        }

        if (sigaction(sig.signo, &sa, nullptr) == -1) {
            std::cerr << "Failed to register signal handler for " << sig.signame << std::endl;
            throw std::runtime_error("Signal registration failed");
        }
        else {
            globallogger->clog(LogLevel::NOTICE, "Signal handler for %s registered successfully", sig.signame);
            //std::cout << "Signal handler for " << sig.signame << " registered successfully" << std::endl;
        }
    }
    return 0;
}

/**
 * @brief 解除指定信号屏蔽并设置新的处理函数。
 *
 * 该函数解除对指定的信号，并设置相应的处理函数。如果信号已被屏蔽，会被解除并为其设置新的处理函数。
 *
 * @param signo 信号编号。
 * @param handler 信号处理函数。
 */
void MSignal::unmask_and_set_handler(int signo, SignalHandlerFunction handler)
{
    // 解除信号
    sigset_t current_set;
    sigemptyset(&current_set);
    sigaddset(&current_set, signo);  // 添加目标信号
    if (sigprocmask(SIG_UNBLOCK, &current_set, nullptr) == -1) {
        globallogger->flog(LogLevel::ALERT, "sigprocmask()解除信号失败!");
    }

    // 设置该信号的处理器
    struct sigaction sa;
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = handler;
    sigemptyset(&sa.sa_mask);
    if (sigaction(signo, &sa, nullptr) == -1) {
        globallogger->flog(LogLevel::ALERT, "sigaction()设置信号处理失败: %d", signo);
    }
}

/**
 * @brief 默认的信号处理函数。
 *
 * 该函数在接收到信号时被调用。它会输出接收到的信号编号，以及发送信号的进程 ID（如果有）。
 *
 * @param signo 信号编号。
 * @param siginfo 信号信息。
 * @param ucontext 上下文指针。
 */
void MSignal::defaultSignalHandler(int signo, siginfo_t* siginfo, void* ucontext)
{
    //delete globallogger;
    std::cout << "Received signal: " << signo << std::endl;
    if (siginfo) {
        std::cout << "Sent by PID: " << siginfo->si_pid << std::endl;
    }
}

/**
 * @brief 处理子进程退出的信号处理函数（`SIGCHLD`）。
 *
 * 该函数会处理 `SIGCHLD` 信号，清理已退出的子进程并显示子进程的退出状态。
 *
 * @param signo 信号编号。
 * @param siginfo 信号信息。
 * @param ucontext 上下文指针。
 */
void MSignal::reapChildProcess(int signo, siginfo_t* siginfo, void* ucontext)
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
 * @brief 向信号列表中添加一个信号及其处理函数。
 *
 * 该函数将信号编号、信号名称和信号处理函数作为参数，添加到信号列表 `signals_` 中。
 *
 * @param signo 信号编号。
 * @param signame 信号名称。
 * @param handler 信号处理函数。
 */
void MSignal::addSignal(int signo, const std::string& signame, SignalHandlerFunction handler)
{
    signals_.emplace_back(SignalInfo{ signo, signame, handler });
}
