#include "MSignal.h"
#include "Daemon.h"
#include <sys/prctl.h>
#include <cstring>
#include <unistd.h>
#include <iostream>
#include <MasterProcess.h>
#include "func.h"

// 确保这些头文件包含完整的类定义
#include "Logger.h"       // 修改为正确的路径
#include "Config.h"    // 修改为正确的路径
#include "CLogicSocket.h" // 添加 CLogicSocket 的头文件
#include "CThreadPool.h" // 添加 ThreadPool 的头文件

// 声明外部变量
extern char** environ;

static void freeresource();

//和设置标题相关的全局量
size_t  g_argvneedmem = 0;        //保存命令行参数需要的内存大小
size_t  g_envneedmem = 0;         //环境变量占用内存大小
int     g_os_argc;                //参数个数 
char**  g_os_argv;                //原始命令行参数，在main中会被赋值
char*   gp_envmem = NULL;         //指向自己分配的env环境变量的内存，在ngx_init_setproctitle()函数中会被分配内存
int     g_daemonized = 0;         //守护进程标记，标记是否启用了守护进程模式，0：未启用，1：已启用

//socket相关
CLogicSocket g_socket;            //socket全局对象
CThreadPool  g_threadpool;        //线程池全局对象

//和进程相关的全局量
pid_t   severl_pid;               //当前进程的pid
pid_t   severl_parent;            //父进程的pid
int     severl_process;           //进程类型，如master,worker进程等
int     g_stopEvent;              //标志程序退出,0不退出1退出

// 全局单例对象
Logger* globallogger = Logger::GetInstance();
Config* globalconfig = Config::GetInstance();

int main(int argc, char* const* argv) {
    int exitcode = 0;             //退出代码，先给0表示正常退出
    int i;                        //临时变量

    //(0)先初始化变量
    g_stopEvent = 0;              //标记程序是否退出，0不退出          

    //(1)无论后续代码是否正常执行，这个变量都需要释放
    severl_pid = getpid();        //获取当前进程pid
    severl_parent = getppid();    //获取父进程pid

    //统计argv所占内存
    g_argvneedmem = 0;
    for (i = 0; i < argc; i++)    //argv = ./nginx -a -b -c asdfas
    {
        g_argvneedmem += strlen(argv[i]) + 1; //+1是给\0留空间
    }
    //统计环境变量所占内存。注意判断方式environ[i]是否为空，为空表示环境变量结束
    for (i = 0; environ[i]; i++)
    {
        g_envneedmem += strlen(environ[i]) + 1; //+1是因为末尾的\0，是占实际内存位置的，要算进来
    }

    g_os_argc = argc;            //保存参数个数
    g_os_argv = (char**)argv;    //保存参数指针

    //(2)初始化失败，需要直接退出
    if (globalconfig->Load("Config.xml") == false)
    {
        std::cerr << "Failed to open Config" << std::endl;
        throw std::runtime_error("Config open failed");
    }
    if (globallogger->log_init() == false)
    {
        std::cerr << "Failed to open Logger" << std::endl;
        throw std::runtime_error("Logger open failed");
    }

    //(3)一些初始化准备工作的资源，先初始化
    //(4)一些初始化函数的调用准备工作
    if (g_socket.Initialize() == false)  //初始化socket
    {
        globallogger->flog(LogLevel::ERROR, "the Socket initialize fail!");
        exitcode = -1;
        globallogger->clog(LogLevel::NOTICE, "程序退出，再见了!");
        freeresource();           //一系列的main返回前的释放动作    
        return exitcode;
    }

    //(5)一些设置函数的调用，准备程序
    init_setproctitle();          //把环境变量搬家
    
    //(6)创建守护进程
    if (globalconfig->GetIntDefault("Daemon", -1) == 1)
    { 
        Daemon daemon(severl_pid, severl_parent);
        int result = daemon.init();
        if (result == -1) {
            globallogger->flog(LogLevel::EMERG, "Daemon build fail!");
        }
        else if (result == 1) {
            globallogger->flog(LogLevel::EMERG, "Daemon parent process tui!");
        }
    }
    
    //(7)开始正式的主工作流程，主流程一般都在这个函数里循环，处理各种事务，直到程序退出，资源释放等等
    MasterProcess master;
    master.start();

    //(8)释放资源
    globallogger->clog(LogLevel::NOTICE, "程序退出，再见了!");
    freeresource();              //一系列的main返回前的释放动作    
    return exitcode;
}

//专门在程序执行末尾释放资源的函数，一系列的main返回前的释放动作
void freeresource()
{
    //(1)因为我们用了环境变量分配内存，所以这里需要释放
    if (gp_envmem)
    {
        delete[]gp_envmem;
        gp_envmem = NULL;
    }

    //(2)关闭日志文件
    globallogger->stoplogprocess();
}
