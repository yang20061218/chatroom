# Linux epoll 聊天室

> 文档状态：2026-10-25，根目录为当前 epoll 主实现，`multithread/` 为历史归档。

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
- 客户端心跳与 30 秒/60 秒超时清理
- 已登录用户退出广播
- 统一 `client_close` 管理 fd、epoll 和连接数组
- 处理 `EPOLLERR`、`EPOLLHUP` 和 `EPOLLRDHUP` 异常断开
- 读、解析、业务、写入和超时逻辑已拆成独立函数
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
├── test_sticky.py    协议半包和粘包测试
├── test_load.py      多客户端登录和广播负载测试
├── test_slow_client.py 慢客户端背压测试
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

## 测试脚本

服务端启动在 `127.0.0.1:7777` 后，可以运行：

```bash
python3 test_sticky.py 1
python3 test_load.py
python3 test_slow_client.py
```

- `test_sticky.py`：验证半包、粘包、登录和超长长度
- `test_load.py`：验证 100 个登录客户端和广播帧数量
- `test_slow_client.py`：验证一个不读取数据的慢客户端不会阻塞服务端

## 测试结果

测试环境为 Ubuntu 虚拟机，服务端监听 `127.0.0.1:7777`。

- `WERROR` 编译通过
- 登录、重复昵称和双客户端聊天通过
- 11 字节协议、半包和粘包测试通过
- 1000 个空闲并发连接保持 10 秒，批量关闭后服务端继续运行
- 100 个登录客户端、10 个发送者、50 条聊天消息，4950 个广播帧送达
- `kill -9` 和 `kill -STOP` 心跳超时测试通过
- 慢客户端测试通过，服务端对其他客户端和新连接保持响应
- ASan/UBSan 负载测试未报告内存错误

当前写缓冲区为固定大小。高速发送且接收端来不及读取时会发生背压丢帧，后续需要通过有上限的发送队列或慢客户端关闭策略解决。

## epoll 服务端设计

**并发模型**：监听 fd 和客户端 fd 都注册到 epoll。事件到达后，由同一个线程依次处理整轮就绪事件，不创建每连接线程。

**连接状态**：`client` 结构体保存 fd、昵称、登录状态、最后活跃时间、读缓冲区、未解析长度、写缓冲区和未发送长度。

**读取**：可读事件中循环 `recv`，直到 `EAGAIN`。数据不足一个协议头或一条完整正文时保留半包，等待下一轮事件。

**解析**：从读缓冲区取出完整协议帧，按命令字分发。处理完当前帧后使用 `memmove` 保留剩余数据并继续解析。

**发送**：业务层只把完整协议帧复制进目标连接的写缓冲区，并注册 `EPOLLOUT`。可写事件中循环发送，遇到 `EAGAIN` 保留剩余数据；全部发完后取消 `EPOLLOUT`。

**异常断开**：客户端事件包含 `EPOLLRDHUP`，服务端同时处理 `EPOLLERR`、`EPOLLHUP` 和 `EPOLLRDHUP`。读事件先尽量读取剩余数据，异常或挂断事件最后统一进入 `client_close`。

**登录**：`CMD_LOGIN` 的 payload 按长度复制并补 `\0`，然后执行重名检查和昵称保存。登录结果打包后进入写缓冲区。

**聊天**：服务端只打包一次 `[昵称] : 正文`，再把同一帧写入其他已登录客户端的写缓冲区，跳过发送者自身。

### 核心函数职责

```text
set_nonblocking         设置非阻塞 fd
update_interest         根据写缓冲区维护 EPOLLIN/EPOLLOUT
find_client             按 fd 查找连接位置
accept_clients          接受新连接并注册 epoll
client_write_buf_add    完整协议帧进入写缓冲区
sendtoall               格式化和广播聊天消息
broadcast               广播系统消息
handle_message          处理登录、聊天、心跳和退出
process_messages        从读缓冲区解析完整协议帧
client_read             循环读取 socket 并调用解析
client_write            循环发送写缓冲区
client_close            统一关闭连接、删除数组并广播退出
check_timeout           清理心跳超时连接
```

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
- 写缓冲区满时会跳过该接收者，尚未实现慢客户端关闭策略
- 尚未完成慢客户端、异常断开和 1000 连接压测
- 多线程版本每连接一个线程，不适合把连接数堆得很高

## 后续计划

- [x] 自定义协议与粘包处理
- [x] 登录、昵称与聊天广播
- [x] 心跳消息
- [x] epoll 单线程事件循环
- [x] 非阻塞读写和连接级缓冲区
- [x] 写缓冲区与 `EPOLLOUT`
- [x] 统一连接关闭函数
- [x] 退出广播与心跳超时清理
- [x] `EPOLLERR/HUP/RDHUP` 错误事件处理
- [ ] 半包、粘包、慢客户端自动化测试
- [ ] 1000 连接压测
- [ ] C++ Reactor 重构

## 环境

Ubuntu、Linux、gcc、POSIX socket API、epoll。
