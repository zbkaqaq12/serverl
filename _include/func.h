#pragma once

//设置可执行程序的标题相关函数
void   init_setproctitle();    //初始化环境变量
void   setproctitle(const char* title);  //设置进程标题