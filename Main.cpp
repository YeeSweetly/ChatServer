// Main.cpp  —  修复版
// 修复点：
//   Redis 初始化移至进程启动阶段，异步线程池建立后再使用；
//   init 失败时打印警告但不 abort，历史功能降级处理。

#include <getopt.h>
#include <string>
#include "EventLoop.h"
#include "Server.h"
#include "base/Logging.h"
#include "storage/ChatHistoryRedis.h"

int main(int argc, char *argv[]) {
    int threadNum    = 4;
    int port         = 8888;
    std::string logPath = "./ChatServer.log";

    int opt;
    const char *str = "t:l:p:";
    while ((opt = getopt(argc, argv, str)) != -1) {
        switch (opt) {
            case 't': threadNum = atoi(optarg); break;
            case 'l':
                logPath = optarg;
                if (logPath.size() < 2 || optarg[0] != '/') {
                    printf("logPath should start with \"/\"\n");
                    abort();
                }
                break;
            case 'p': port = atoi(optarg); break;
            default: break;
        }
    }

    Logger::setLogFileName(logPath);

    // Redis 初始化：失败时历史功能降级，不影响聊天主流程
    if (!ChatHistoryRedis::getInstance().init("127.0.0.1", 6379)) {
        LOG << "Redis init failed, history feature disabled.";
    }

#ifndef _PTHREADS
    LOG << "_PTHREADS is not defined !";
#endif

    EventLoop mainLoop;
    Server myChatServer(&mainLoop, threadNum, port);
    myChatServer.start();
    mainLoop.loop();
    return 0;
}
