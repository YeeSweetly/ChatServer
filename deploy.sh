#!/bin/bash
# ChatServer 部署脚本
# 适用于 Ubuntu/Debian 系统

set -e

echo "========================================"
echo "ChatServer 部署脚本"
echo "========================================"

# 1. 更新系统和安装依赖
echo ""
echo "[1/5] 更新系统并安装依赖..."
apt-get update && apt-get install -y \
    build-essential \
    cmake \
    libhiredis-dev \
    libevent-dev \
    redis-server \
    git

# 2. 配置系统参数
echo ""
echo "[2/5] 配置系统参数..."

# 设置文件描述符限制
cat >> /etc/security/limits.conf << EOF
* soft nofile 100000
* hard nofile 100000
EOF

# 设置内核参数
cat >> /etc/sysctl.conf << EOF
net.core.somaxconn = 65535
net.ipv4.tcp_max_syn_backlog = 10240
net.ipv4.tcp_tw_reuse = 1
net.ipv4.tcp_tw_recycle = 1
net.ipv4.tcp_fin_timeout = 30
EOF

# 立即生效
sysctl -p

# 3. 克隆代码
echo ""
echo "[3/5] 克隆代码..."
if [ ! -d "/opt/ChatServer" ]; then
    git clone https://github.com/YeeSweetly/ChatServer.git /opt/ChatServer
else
    cd /opt/ChatServer && git pull
fi

# 4. 编译项目
echo ""
echo "[4/5] 编译项目..."
cd /opt/ChatServer
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# 5. 启动服务
echo ""
echo "[5/5] 启动服务..."

# 启动 Redis
echo "启动 Redis..."
redis-server --daemonize yes

# 启动 ChatServer（使用所有CPU核心的2倍线程数）
THREADS=$(( $(nproc) * 2 ))
echo "启动 ChatServer，使用 ${THREADS} 线程..."
nohup ./ChatServer -t ${THREADS} -p 8888 -l /var/log/ChatServer.log > /dev/null 2>&1 &

echo ""
echo "========================================"
echo "部署完成！"
echo "========================================"
echo "服务器端口: 8888"
echo "日志文件: /var/log/ChatServer.log"
echo "连接测试: telnet localhost 8888"
echo ""
echo "要停止服务：killall ChatServer"