#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>  //env
#include <string.h>
#include"global.h"


/**
 * @brief 初始化可执行程序的环境变量内存，为程序分配新的内存空间并保存环境变量到新内存中
 *
 * 该函数会为环境变量分配内存，并将原有环境变量复制到新分配的内存中，
 * 以便为设置可执行程序标题预留充足内存空间。
 *
 * @note 该函数不处理内存分配失败的任何错误，内存分配失败会导致程序异常退出
 */
extern void init_setproctitle()
{
    //这里我们不判断penvmen == NULL，有些人会用new返回NULL，有些会报异常，不管怎样，如果在这里需要的地方new失败了，我们无法正常处理，程序失败直接退出，意思就是说不用判断返回值了 
    gp_envmem = new char[g_envneedmem];
    memset(gp_envmem, 0, g_envneedmem);  //内存要清空防止出现问题

    char* ptmp = gp_envmem;
    //把原来的内存内容搬到新地方来
    for (int i = 0; environ[i]; i++)
    {
        size_t size = strlen(environ[i]) + 1; //需要+1因为末尾有\0，注意字符串末尾都有\0，strlen是不包含字符串末尾的\0的
        strcpy(ptmp, environ[i]);      //把原环境变量内容拷贝到新地方（新内存）
        environ[i] = ptmp;            //然后还要更新环境变量指向这段新内存
        ptmp += size;
    }
    return;
}

/**
 * @brief 设置可执行程序的标题
 *
 * 该函数会覆盖原有的命令行参数，设置新的可执行程序标题。所有命令行参数会被清空，仅保留标题覆盖
 *
 * @param title 新的程序标题字符串
 *
 * @note 如果标题长度过长，导致无法同时存储所有命令行参数，函数将不会执行任何操作
 *       标题长度不应超过原始 `argv` 和 `environ` 占用内存总和
 */
extern void setproctitle(const char* title)
{
    //设置标题，因为环境变量和命令行参数都不需要用了，所以直接被标题覆盖掉
    //注意：我们的标题长度，不会超过原始的环境变量和原始命令行参数的总和

    //(1)计算新标题长度
    size_t ititlelen = strlen(title);

    //(2)计算总的原始的argv所占内存总长度【包括各种参数】    
    size_t esy = g_argvneedmem + g_envneedmem; //argv和environ内存总和
    if (esy <= ititlelen)
    {
        //标题太长，比argv和environ总和都不对，注意字符串末尾多了个 \0，所以这块判断是 <=，也就是=都不行
        return;
    }

    //空间够用，继续执行下面的操作    

    //(3)设置后续的命令行参数为空，表示只有argv[]中只有一个元素了，防止后续argv被滥用，因为很多判断是用argv[] == NULL来做结束标记的
    g_os_argv[1] = NULL;

    //(4)把标题弄进来，注意原来的命令行参数都会被覆盖掉，不要再使用这些命令行参数,因为g_os_argv[1]已经被设置为NULL了
    char* ptmp = g_os_argv[0]; //让ptmp指向g_os_argv所指向的内存
    strcpy(ptmp, title);
    ptmp += ititlelen; //跳过标题

    //(5)把剩余的原argv以及environ所占的内存全部清0，否则在ps命令中可能会残留一些没有被覆盖的内容
    size_t cha = esy - ititlelen;  //内存总和减去标题字符串长度（包含字符串末尾的\0），剩余的大小，要memset的
    memset(ptmp, 0, cha);
    return;
}