#pragma once
#include"Config.h"
#include"Logger.h"
#include"CLogicSocket.h"
#include"CThreadPool.h"
#include <signal.h> 
#include<string>
#include<vector>

//一些比较通用的定义放在这里，比如typedef定义
//一些全局变量声明，供外部调用

//类型定义

//外部全局变量声明
extern size_t        g_argvneedmem;        //保存命令行参数需要的内存大小
extern size_t        g_envneedmem;         //环境变量占用的内存大小
extern int           g_os_argc;            //参数个数
extern char**        g_os_argv;            //原始命令行参数，在main中会被赋值
extern char*         gp_envmem;            //指向自己分配的env环境变量的内存
extern int           g_daemonized;         //守护进程标记，标记是否启用了守护进程模式，0：未启用，1：已启用

extern CLogicSocket  g_socket;             //socket全局对象
extern CThreadPool   g_threadpool;         //线程池全局对象

extern pid_t         ngx_pid;              //当前进程的pid
extern pid_t         ngx_parent;           //父进程的pid
extern int           ngx_process;          //进程类型，比如master/worker进程等
extern sig_atomic_t  ngx_reap;            //进程退出标志
extern int           g_stopEvent;          //程序退出标志，0不退出，1退出

extern Logger*       globallogger;         //日志管理器
extern Config*       globalconfig;         //配置管理器

