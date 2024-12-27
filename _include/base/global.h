#pragma once
#include "CThreadPool.h"
#include "CSocket.h"
#include <signal.h>


// 进程标题相关
extern size_t        g_argvneedmem;    // 保存命令行参数需要的内存大小
extern size_t        g_envneedmem;     // 环境变量占用的内存大小
extern int          g_os_argc;         // 参数个数
extern char**       g_os_argv;         // 原始命令行参数
extern char*        gp_envmem;         // 环境变量内存

// 进程状态相关
extern int          g_daemonized;      // 守护进程标记(0:未启用,1:已启用)
extern pid_t        severl_pid;           // 当前进程pid
extern pid_t        severl_parent;        // 父进程pid
extern int          severl_process;       // 进程类型(master/worker)
extern sig_atomic_t ngx_reap;          // 进程退出标志
extern int          g_stopEvent;       // 程序退出标志(0:不退出,1:退出)

// 全局对象
extern CSocket g_socket;          // 网络对象
extern CThreadPool* g_threadpool;      // 改为指针类型



