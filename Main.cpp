// @Author Lin Ya (refactored for chat room)
#include <getopt.h>
#include <string>
#include "EventLoop.h"
#include "Server.h"
#include "base/Logging.h"
#include "../storage/ChatHistoryRedis.h"

int main(int argc, char *argv[]) {
  int threadNum = 4;
  int port = 8888;           // 改为常用聊天室端口
  std::string logPath = "./ChatServer.log";

  int opt;
  const char *str = "t:l:p:";
  while ((opt = getopt(argc, argv, str)) != -1) {
    switch (opt) {
      case 't': {
        threadNum = atoi(optarg);
        break;
      }
      case 'l': {
        logPath = optarg;
        if (logPath.size() < 2 || optarg[0] != '/') {
          printf("logPath should start with \"/\"\n");
          abort();
        }
        break;
      }
      case 'p': {
        port = atoi(optarg);
        break;
      }
      default:
        break;
    }
  }
  Logger::setLogFileName(logPath);

  // 初始化 Redis
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