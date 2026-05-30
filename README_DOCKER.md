# WebServer Docker 部署指南

## 前置要求

- Docker Desktop for Windows 已安装
- WSL2 已启用（Docker Desktop 需要）

## 快速启动

### 1. 启动Docker Desktop

在Windows开始菜单中找到并启动 **Docker Desktop**。

等待Docker服务启动完成（约1-2分钟）。

### 2. 验证Docker安装

打开PowerShell终端，运行：

```powershell
docker --version
docker-compose --version
```

### 3. 构建并启动服务

在项目根目录（d:\C++\WebServer）执行：

```powershell
# 使用docker-compose启动所有服务
docker-compose up -d

# 查看服务状态
docker-compose ps

# 查看日志
docker-compose logs -f chatserver
```

### 4. 停止服务

```powershell
docker-compose down
```

## 手动构建（可选）

如果需要单独构建Docker镜像：

```powershell
docker build -t chatserver:latest .
docker run -d -p 8080:8080 -p 6379:6379 --name chat-server chatserver:latest
```

## 服务说明

- **chatserver**: C++ Web服务器，监听8080端口
- **redis**: Redis数据库，监听6379端口

## 配置说明

可以在 `docker-compose.yml` 中修改环境变量：

- `SERVER_PORT`: 服务器端口（默认8080）
- `REDIS_HOST`: Redis主机地址（默认redis）
- `REDIS_PORT`: Redis端口（默认6379）

## 常见问题

### 1. Docker启动失败

确保已启用WSL2和Hyper-V：

```powershell
wsl --status
systeminfo | findstr /C:"Hyper-V"
```

### 2. 端口被占用

修改 `docker-compose.yml` 中的端口映射，避免与本地服务冲突。

### 3. 构建失败

检查CMakeLists.txt配置，确保所有依赖正确。
