# Linux epoll 聊天室

> 文档状态：2026-10-22，根目录为当前 epoll 主实现，`multithread/` 为历史归档。

用 C 写的 TCP 聊天室。当前主版本位于仓库根目录，采用单线程 `epoll` 事件循环；旧的多线程实现保留在 `multithread/`，用于对照两种并发模型。

## 仓库版本

| 位置 | 实现 | 状态 |
| --- | --- | --- |
| 仓库根目录 | 单线程 `epoll` 事件循环 | 当前主版本 |
| `multithread/` | 一连接一线程 | 历史版本，保留对照 |

两个版本共用相同的客户端、11 字节应用层协议和基础网络函数。

## 当前功能

- 多客户端连接与昵称登录
- 重名检查
- 登录成功与失败响应
- `[昵称] : 正文` 聊天广播
- 11 字节协议头与 TCP 粘包、半包处理
- 每条连接独立的读写缓冲区
- `EPOLLOUT` 驱动部分发送
- 非阻塞 `accept`、`recv` 和 `send`
- 客户端心跳与服务端活跃时间更新
- 连接数上限保护

## 协议

协议头固定 11 字节，后面跟变长 payload：

| 偏移 | 长度 | 字段 | 说明 |
| --- | --- | --- | --- |
| 0 | 4 | magic | 固定 `"1218"` |
| 4 | 1 | version | 当前为 1 |
| 5 | 2 | cmd | 命令字，网络字节序 |
| 7 | 4 | length | payload 字节数，网络字节序 |
| 11 | - | payload | 正文 |

命令字：

| 值 | 名称 | 说明 |
| --- | --- | --- |
| 1 | `CMD_CHAT` | 聊天消息 |
| 2 | `CMD_EXIT` | 主动退出 |
| 3 | `CMD_HEART` | 心跳，更新活跃时间 |
| 4 | `CMD_LOGIN` | 登录请求，payload 为昵称 |
| 5 | `CMD_LOGIN_RES` | 登录响应 |

## 分层

```text
server.c / client.c     业务层
       ↓
protocol.c              协议打包与解析
       ↓
net.c                   建连与 socket 基础操作
```

- `net.c` 负责监听、连接和 fd
- `protocol.c` 负责 11 字节头和正文
- `server.c` 负责连接状态、登录、广播和 epoll 事件
- `client.c` 保持阻塞式读写，因此服务端事件循环不会影响客户端原有交互

## 编译

编译当前 epoll 版本：

```bash
make
```

严格模式：

```bash
make WERROR=-Werror
```

编译旧的多线程版本：

```bash
make -C multithread
make -C multithread WERROR=-Werror
```

手动编译当前版本：

```bash
gcc -Wall -Wextra -g -o server server.c net.c protocol.c
gcc -Wall -Wextra -g -o client client.c net.c protocol.c -lpthread
```

## 运行

启动 epoll 服务端：

```bash
./server
```

另开终端启动客户端：

```bash
./client
```

客户端启动后输入昵称，登录成功后发送消息，其他在线用户会看到：

```text
[昵称] : 内容
```

运行旧的多线程版本：

```bash
./multithread/server
./multithread/client
```

## 目录结构

```text
.
├── README.md         项目总说明
├── .gitignore        Git 忽略规则
├── .gitattributes    Git 换行规则
├── server.c          当前 epoll 服务端
├── client.c          客户端
├── net.c             网络基础函数
├── net.h
├── protocol.c        协议打包与解析
├── protocol.h        协议常量与函数声明
├── Makefile
├── test_sticky.py    粘包测试脚本
└── multithread/
    ├── server.c      一连接一线程服务端
    ├── client.c
    ├── net.c
    ├── net.h
    ├── protocol.c
    ├── protocol.h
    ├── Makefile
    ├── test_sticky.py
    └── README.md
```

每个文件的职责都在上面的目录说明和 [multithread/README.md](multithread/README.md) 中列出。

## epoll 服务端设计

**并发模型**：监听 fd 和客户端 fd 都注册到 epoll。事件到达后，由同一个线程依次处理整轮就绪事件，不创建每连接线程。

**连接状态**：`client` 结构体保存 fd、昵称、登录状态、最后活跃时间、读缓冲区、未解析长度、写缓冲区和未发送长度。

**读取**：可读事件中循环 `recv`，直到 `EAGAIN`。数据不足一个协议头或一条完整正文时保留半包，等待下一轮事件。

**解析**：从读缓冲区取出完整协议帧，按命令字分发。处理完当前帧后使用 `memmove` 保留剩余数据并继续解析。

**发送**：业务层只把完整协议帧复制进目标连接的写缓冲区，并注册 `EPOLLOUT`。可写事件中循环发送，遇到 `EAGAIN` 保留剩余数据；全部发完后取消 `EPOLLOUT`。

**登录**：`CMD_LOGIN` 的 payload 按长度复制并补 `\0`，然后执行重名检查和昵称保存。登录结果打包后进入写缓冲区。

**聊天**：服务端只打包一次 `[昵称] : 正文`，再把同一帧写入其他已登录客户端的写缓冲区，跳过发送者自身。

## 旧的多线程实现

`multithread/` 是最初的一连接一线程版本：

- 每个连接对应一个独立线程
- 读循环可以直接使用阻塞式 `protocol_recv`
- 广播前复制在线 fd 快照
- 心跳由单独线程检查
- 连接数组由互斥锁保护

保留这个目录是为了观察阻塞模型到事件驱动模型的差异，不作为后续主要开发方向。

## 已知限制

- `protocol_pack` 使用 `strlen` 计算正文长度，只支持文本消息
- epoll 服务端读缓冲区为 4 KB，超过该长度的消息会被拒绝
- 连接关闭流程仍有重复代码，尚未统一封装
- 退出广播、错误事件处理和完整超时清理尚未完成
- 尚未完成慢客户端、异常断开和 1000 连接压测
- 多线程版本每连接一个线程，不适合把连接数堆得很高

## 后续计划

- [x] 自定义协议与粘包处理
- [x] 登录、昵称与聊天广播
- [x] 心跳消息
- [x] epoll 单线程事件循环
- [x] 非阻塞读写和连接级缓冲区
- [x] 写缓冲区与 `EPOLLOUT`
- [ ] 统一连接关闭函数
- [ ] 退出广播与错误事件处理
- [ ] 半包、粘包、慢客户端自动化测试
- [ ] 1000 连接压测
- [ ] C++ Reactor 重构

## 环境

Ubuntu、Linux、gcc、POSIX socket API、epoll。
