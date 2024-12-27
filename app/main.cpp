#include "ServerCore.h"
#include <iostream>

extern char** environ;

int main(int argc, char* const* argv) {
    int exitcode = 0;

    try {
        // 初始化服务器核心
        ServerCore::getInstance().initialize(argc, argv, "Config.xml");
        
        // 运行服务器
        ServerCore::getInstance().run();
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        exitcode = -1;
    }

    // 清理资源
    ServerCore::getInstance().cleanup();
    return exitcode;
}
