# 浏览器访问聊天服务器

## 当前服务状态

✅ **Docker聊天服务器**: 运行中 (127.0.0.1:8080)

## 方式1: 使用Telnet (最简单)

Windows 上打开 PowerShell:

```powershell
# 安装Telnet（如果未安装）
dism /online /Enable-Feature /FeatureName:TelnetClient /All

# 连接聊天服务器
telnet 127.0.0.1 8080
```

## 方式2: 使用Python脚本

### 启动简单的Python聊天客户端

```bash
python -c "
import socket
import threading

def receive_messages(sock):
    while True:
        try:
            data = sock.recv(1024)
            if not data:
                break
            print(data.decode('utf-8'), end='')
        except:
            break

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect(('127.0.0.1', 8080))

threading.Thread(target=receive_messages, args=(sock,), daemon=True).start()

print('已连接！请输入用户名:')
while True:
    try:
        msg = input()
        sock.sendall((msg + '\\n').encode('utf-8'))
    except KeyboardInterrupt:
        sock.close()
        break
"
```

## 方式3: 使用WebSocket服务器

创建一个完整的WebSocket代理服务器：

```python
import asyncio
import websockets
import socket

# 聊天服务器地址
CHAT_HOST = '127.0.0.1'
CHAT_PORT = 8080

# 存储所有连接的WebSocket客户端
connected_clients = set()

async def chat_client_to_websocket(websocket, chat_sock):
    try:
        while True:
            data = chat_sock.recv(4096)
            if not data:
                break
            await websocket.send(data.decode('utf-8'))
    except:
        pass

async def websocket_to_chat_client(websocket, chat_sock):
    try:
        async for message in websocket:
            chat_sock.sendall(message.encode('utf-8'))
    except:
        pass

async def handle_client(websocket, path):
    connected_clients.add(websocket)
    print('新客户端连接')
    
    try:
        chat_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        chat_sock.connect((CHAT_HOST, CHAT_PORT))
        
        await asyncio.gather(
            chat_client_to_websocket(websocket, chat_sock),
            websocket_to_chat_client(websocket, chat_sock)
        )
    except Exception as e:
        print(f'错误: {e}')
    finally:
        connected_clients.remove(websocket)
        print('客户端断开连接')

async def main():
    server = await websockets.serve(handle_client, '127.0.0.1', 8765)
    print('WebSocket服务器已启动: ws://127.0.0.1:8765')
    await server.wait_closed()

if __name__ == '__main__':
    asyncio.run(main())
```

## 方式4: 使用nc (Netcat)

```bash
# 如果有Git Bash或WSL
nc 127.0.0.1 8080
```

## 测试聊天功能

连接后:

1. 输入用户名（例如：`alice`）
2. 输入消息（例如：`Hello!`）
3. 输入 `QUIT` 退出

## 快速测试脚本

创建 `test_client.py`:

```python
import socket

def test_chat_server():
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect(('127.0.0.1', 8080))
    
    print('=== 聊天服务器测试 ===')
    print('已连接到 127.0.0.1:8080')
    print('输入消息或"QUIT"退出\n')
    
    try:
        while True:
            msg = input('> ')
            if msg == 'QUIT':
                break
            sock.sendall((msg + '\n').encode('utf-8'))
            
            response = sock.recv(4096)
            if response:
                print(response.decode('utf-8'))
    except Exception as e:
        print(f'错误: {e}')
    finally:
        sock.close()
        print('连接已关闭')

if __name__ == '__main__':
    test_chat_server()
```

运行:
```bash
python test_client.py
```
