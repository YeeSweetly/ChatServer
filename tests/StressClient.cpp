// tests/StressClient.cpp  —  修复版
// 修复点：
//   1. stop_flag 改为每连接私有，不再全局共享
//   2. 每个线程内的连接改为真正并发（每连接独立子线程）
//   3. 消息解析兼容服务器回包格式 "[username] msg_N_ts"
//   4. 延迟统计正确

#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <cstring>
#include <algorithm>
#include <arpa/inet.h>
#include <unistd.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/time.h>
#include <mutex>

std::atomic<long> total_sent{0};
std::atomic<long> total_recv{0};
std::atomic<long> total_latency_us{0};
std::atomic<int>  connect_fail{0};
std::atomic<int>  client_id_gen{0};

std::vector<long> latency_samples;
std::mutex        latency_mutex;

inline long current_us() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return tv.tv_sec * 1000000L + tv.tv_usec;
}

// ----------------------------------------------------------------
// 接收线程：每连接独立的 stop 标志，正确解析服务器回包格式
// 服务器回包格式: "[username] msg_<seq>_<timestamp_us>\n"
// ----------------------------------------------------------------
void recv_loop(int sock, std::atomic<bool>* stop_flag) {
    char buf[4096];
    std::string buffer;

    while (!stop_flag->load(std::memory_order_relaxed)) {
        int n = recv(sock, buf, sizeof(buf) - 1, 0);
        if (n <= 0) {
            if (n == 0) break;
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
            break;
        }
        buf[n] = '\0';
        buffer += buf;

        size_t pos;
        while ((pos = buffer.find('\n')) != std::string::npos) {
            std::string line = buffer.substr(0, pos);
            buffer.erase(0, pos + 1);

            // 服务器回包格式: "[username] msg_<seq>_<ts>" 或 "OK ..." 等控制消息
            // 找到最后一个 '_' 后面的数字串作为时间戳
            // 格式保证：msg_<seq>_<ts_us>，ts 一定是最后一段
            size_t msg_pos = line.find("msg_");
            if (msg_pos == std::string::npos) continue;  // 控制消息，跳过

            std::string payload = line.substr(msg_pos);  // "msg_N_timestamp"
            size_t first = payload.find('_');
            if (first == std::string::npos) continue;
            size_t second = payload.find('_', first + 1);
            if (second == std::string::npos) continue;

            long sent_us = 0;
            try {
                sent_us = std::stol(payload.substr(second + 1));
            } catch (...) {
                continue;
            }

            long now_us  = current_us();
            long latency = now_us - sent_us;
            if (latency > 0 && latency < 10000000L) {  // 过滤异常值（>10s）
                total_recv++;
                total_latency_us += latency;

                std::lock_guard<std::mutex> lock(latency_mutex);
                latency_samples.push_back(latency);
            }
        }
    }
}

// ----------------------------------------------------------------
// 单连接逻辑：建立连接、发送用户名、收发消息、关闭
// stop_flag 独立分配，生命周期由本函数管理
// ----------------------------------------------------------------
void run_client(int client_id, const char* ip, int port, int duration_sec) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { connect_fail++; return; }

    int flag = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

    // 接收超时（驱动 recv_loop 定期检查 stop_flag）
    struct timeval tv { 1, 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    inet_pton(AF_INET, ip, &addr.sin_addr);

    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
        connect_fail++;
        close(sock);
        return;
    }

    // 先用阻塞模式发送用户名
    char username[64];
    snprintf(username, sizeof(username), "user%d\n", client_id);
    if (send(sock, username, strlen(username), 0) <= 0) {
        close(sock);
        return;
    }

    // 每连接独立的 stop_flag，发送完毕后通知接收线程退出
    std::atomic<bool> stop_flag{false};

    // 切换为非阻塞
    int fl = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, fl | O_NONBLOCK);

    // 启动本连接的接收线程
    std::thread recv_th(recv_loop, sock, &stop_flag);

    // 发送循环
    auto start = std::chrono::steady_clock::now();
    int  seq   = 0;
    while (true) {
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration<double>(now - start).count() > duration_sec)
            break;

        long now_us = current_us();
        char msg[128];
        snprintf(msg, sizeof(msg), "msg_%d_%ld\n", seq++, now_us);
        ssize_t ret = send(sock, msg, strlen(msg), 0);
        if (ret < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
                continue;
            }
            break;
        } else if (ret == 0) {
            break;
        }
        total_sent++;
    }

    stop_flag.store(true);
    shutdown(sock, SHUT_RDWR);
    recv_th.join();
    close(sock);
}

int main(int argc, char* argv[]) {
    signal(SIGPIPE, SIG_IGN);

    if (argc != 6) {
        std::cout << "Usage: " << argv[0]
                  << " <ip> <port> <threads> <conn_per_thread> <duration_sec>\n"
                  << "  e.g.: " << argv[0] << " 127.0.0.1 8080 20 50 120\n"
                  << "  NOTE: total connections = threads * conn_per_thread\n"
                  << "        each thread runs conn_per_thread connections CONCURRENTLY\n";
        return 1;
    }

    const char* ip          = argv[1];
    int port                = std::stoi(argv[2]);
    int threads             = std::stoi(argv[3]);
    int conn_per_thread     = std::stoi(argv[4]);
    int duration_sec        = std::stoi(argv[5]);
    int total_conn          = threads * conn_per_thread;

    std::cout << "Starting " << total_conn << " connections ("
              << threads << " threads x " << conn_per_thread << " conn/thread)"
              << " for " << duration_sec << " seconds...\n";

    std::vector<std::thread> pool;
    auto start_time = std::chrono::steady_clock::now();

    for (int t = 0; t < threads; ++t) {
        pool.emplace_back([=]() {
            // 每个线程内，conn_per_thread 个连接并发运行（各开子线程）
            std::vector<std::thread> inner;
            inner.reserve(conn_per_thread);
            for (int c = 0; c < conn_per_thread; ++c) {
                int id = client_id_gen.fetch_add(1);
                inner.emplace_back(run_client, id, ip, port, duration_sec);
            }
            for (auto& th : inner) th.join();
        });
    }

    for (auto& th : pool) th.join();

    auto end_time = std::chrono::steady_clock::now();
    double seconds = std::chrono::duration<double>(end_time - start_time).count();

    // 计算延迟百分位
    std::sort(latency_samples.begin(), latency_samples.end());
    long p50 = 0, p99 = 0;
    if (!latency_samples.empty()) {
        p50 = latency_samples[latency_samples.size() * 50 / 100];
        p99 = latency_samples[latency_samples.size() * 99 / 100];
    }

    long recv = total_recv.load();
    long sent = total_sent.load();

    std::cout << "\n========== Stress Test Result ==========\n";
    std::cout << "Total connections:  " << total_conn                  << "\n";
    std::cout << "Connect failures:   " << connect_fail.load()         << "\n";
    std::cout << "Duration:           " << seconds                     << " sec\n";
    std::cout << "Total sent:         " << sent                        << "\n";
    std::cout << "Total recv:         " << recv                        << "\n";
    std::cout << "Send QPS:           " << sent / seconds              << " msg/s\n";
    std::cout << "Recv QPS:           " << recv / seconds              << " msg/s\n";
    std::cout << "Average latency:    "
              << (recv > 0 ? total_latency_us.load() / recv / 1000.0 : 0) << " ms\n";
    std::cout << "P50 latency:        " << p50 / 1000.0               << " ms\n";
    std::cout << "P99 latency:        " << p99 / 1000.0               << " ms\n";
    std::cout << "=========================================\n";

    return 0;
}
