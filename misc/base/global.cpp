#include "global.h"
#include "net/CSocket.h"

CSocket g_socket;
int g_stopEvent = 0;
size_t g_argvneedmem = 0;
size_t g_envneedmem = 0;
int g_os_argc = 0;
char** g_os_argv = nullptr;
char* gp_envmem = nullptr;
CThreadPool* g_threadpool = nullptr;