cmake_minimum_required(VERSION 3.10)
project(ChatServer)

set(CMAKE_CXX_STANDARD 11)

# 添加 base 子目录（必须在 add_executable 之前）
add_subdirectory(base)

# 源文件列表
set(SRCS
        Channel.cpp
        ChatSession.cpp
        Epoll.cpp
        EventLoop.cpp
        EventLoopThread.cpp
        EventLoopThreadPool.cpp
        Main.cpp
        Server.cpp
        Timer.cpp
        Util.cpp
        storage/ChatHistoryRedis.cpp
        # 新增源文件（如果 MemoryPool 是 header-only，无需添加）
)

# 包含头文件路径
include_directories(${PROJECT_SOURCE_DIR}/base)
include_directories(${PROJECT_SOURCE_DIR}/storage)

# 查找 hiredis
find_package(PkgConfig REQUIRED)
pkg_check_modules(HIREDIS REQUIRED hiredis)

add_executable(ChatServer ${SRCS})

# 链接库：使用 libserver_base 目标名称
target_link_libraries(ChatServer
        libserver_base          # 注意：是 libserver_base，不是 server_base
        ${HIREDIS_LIBRARIES}
        pthread
        rt
)

# 可选：确保 storage 目录的头文件路径
target_include_directories(ChatServer PRIVATE ${PROJECT_SOURCE_DIR}/storage)