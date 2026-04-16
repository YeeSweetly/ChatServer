// tests/StressClient.cpp
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <cstring>
#include <arpa/inet.h>
#include <unistd.h>
#include <netinet/tcp.h>

std::atomic<long> total_sent{0};
std::atomic<long> total_recv{0};
std::atomic<long> total_latency_ms{0};
std::atomic<int> connect_fail{0};

// 读取一行数据（以 \n 结尾）
bool read_line(int sock, std::string& line, int timeout_ms = 5000) {
    char buf[4096];
    line.clear();
    auto start = std::chrono::steady_clock::now();
    while (true) {
        int n = recv(sock, buf, sizeof(buf) - 1, 0);
        if (n <= 0) return false;
        buf[n] = '\0';
        line += buf;
        size_t pos = line.find('\n');
        if (pos != std::string::npos) {
            line.resize(pos + 1);
            return true;
        }
        // 超时保护
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count() > timeout_ms) {
            return false;
        }
    }
}

void run_client(int client_id, const char* ip, int port, int msg_count) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return;

    // 禁用 Nagle 算法，降低延迟
    int flag = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &addr.sin_addr);
    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
        connect_fail++;
        close(sock);
        return;
    }

    // 发送用户名
    char username[32];
    snprintf(username, sizeof(username), "user%d\n", client_id);
    send(sock, username, strlen(username), 0);

    // 接收欢迎消息（一行）
    std::string welcome;
    if (!read_line(sock, welcome)) {
        close(sock);
        return;
    }

    // 开始发送消息并等待广播
    for (int i = 0; i < msg_count; ++i) {
        char msg[128];
        snprintf(msg, sizeof(msg), "msg_%d_from_%d\n", i, client_id);
        auto start = std::chrono::steady_clock::now();
        send(sock, msg, strlen(msg), 0);
        total_sent++;

        // 等待广播回复（一行）
        std::string reply;
        if (read_line(sock, reply)) {
            auto end = std::chrono::steady_clock::now();
            int latency = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            total_latency_ms += latency;
            total_recv++;
        } else {
            // 接收失败，退出循环
            break;
        }
    }
    close(sock);
}

int main(int argc, char* argv[]) {
    if (argc != 6) {
        std::cout << "Usage: " << argv[0] << " <ip> <port> <threads> <conn_per_thread> <msg_per_conn>\n";
        return 1;
    }
    const char* ip = argv[1];
    int port = std::stoi(argv[2]);
    int threads = std::stoi(argv[3]);
    int conn_per_thread = std::stoi(argv[4]);
    int msg_per_conn = std::stoi(argv[5]);

    std::vector<std::thread> pool;
    int total_conn = threads * conn_per_thread;
    int client_id = 0;

    auto start_time = std::chrono::steady_clock::now();

    for (int t = 0; t < threads; ++t) {
        pool.emplace_back([=, &client_id]() {
            for (int c = 0; c < conn_per_thread; ++c) {
                int id = client_id++;
                run_client(id, ip, port, msg_per_conn);
            }
        });
    }

    for (auto& th : pool) th.join();

    auto end_time = std::chrono::steady_clock::now();
    double total_seconds = std::chrono::duration<double>(end_time - start_time).count();

    std::cout << "=== Stress Test Result ===" << std::endl;
    std::cout << "Total connections: " << total_conn << std::endl;
    std::cout << "Connect failures: " << connect_fail.load() << std::endl;
    std::cout << "Total messages sent: " << total_sent.load() << std::endl;
    std::cout << "Total messages received: " << total_recv.load() << std::endl;
    std::cout << "Total time: " << total_seconds << " s" << std::endl;
    if (total_recv.load() > 0) {
        double avg_latency = total_latency_ms.load() / (double)total_recv.load();
        std::cout << "Average latency: " << avg_latency << " ms" << std::endl;
    }
    double qps = total_sent.load() / total_seconds;
    std::cout << "Throughput (send QPS): " << qps << " msg/s" << std::endl;

    return 0;
}