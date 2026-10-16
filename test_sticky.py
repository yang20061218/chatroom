#!/usr/bin/env python3
"""
粘包测试脚本 —— 验证服务端的消息边界处理

用法：
    python3 test_sticky.py          依次跑全部用例
    python3 test_sticky.py 1        只跑第 1 个用例

协议：4 字节大端长度 + 正文

跑之前先开一个正常的客户端，用它观察广播结果。
"""

import socket
import struct
import sys
import time

HOST = "127.0.0.1"
PORT = 7777


def connect():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect((HOST, PORT))
    time.sleep(0.3)
    return s


def pack(text):
    """按协议打包：4 字节大端长度 + UTF-8 正文"""
    body = text.encode("utf-8")
    return struct.pack("!I", len(body)) + body


def case1_two_messages_one_send():
    print("用例 1：两条消息粘在一起，一次 send 发出")
    s = connect()
    s.sendall(pack("aaa") + pack("bbb"))
    time.sleep(2)
    s.close()
    print("  预期：观察端看到两行，各自带自己的 [clientN] 前缀")
    print("        错误的实现会把它们拼成一条，第二个前缀不见了")


def case2_header_split():
    print("用例 2：4 字节长度头拆成 2 + 2 两次发，正文再单独发")
    s = connect()
    data = pack("ccc")
    s.sendall(data[0:2])
    time.sleep(0.5)
    s.sendall(data[2:4])
    time.sleep(0.5)
    s.sendall(data[4:])
    time.sleep(2)
    s.close()
    print("  预期：观察端收到完整的 ccc，不会收到半截乱码")


def case3_long_body():
    print("用例 3：正文 3000 字节，拆成 5 次发送")
    s = connect()
    text = "d" * 3000
    data = pack(text)
    step = (len(data) + 4) // 5
    for i in range(0, len(data), step):
        s.sendall(data[i:i + step])
        time.sleep(0.2)
    time.sleep(3)
    s.close()
    print("  预期：观察端收到一条 3000 个 d 的消息")
    print("        如果只看到 1200 多个 d，说明服务端拼消息的缓冲区太小被截断了")


def case5_empty_body():
    print("用例 5：发一条长度为 0 的消息")
    s = connect()
    s.sendall(struct.pack("!I", 0))
    time.sleep(2)
    s.close()
    print("  预期：服务端拒绝这个连接，进程继续活着")


def case4_huge_length():
    print("用例 4：谎报 length = 2GB，然后什么都不发")
    s = connect()
    s.sendall(struct.pack("!I", 2 * 1024 * 1024 * 1024))
    time.sleep(2)
    s.close()
    print("  预期：服务端拒绝并断开这个连接，进程继续活着")
    print("        如果服务端崩了（Segmentation fault），说明长度没有做上限校验")


CASES = {
    "1": case1_two_messages_one_send,
    "2": case2_header_split,
    "3": case3_long_body,
    "4": case4_huge_length,
    "5": case5_empty_body,
}

# 第 4 个放最后：它有可能把服务端打挂，之后的用例就没法跑了
ORDER = ["1", "2", "3", "5", "4"]


def main():
    which = sys.argv[1] if len(sys.argv) > 1 else None

    if which and which not in CASES:
        print("没有这个用例，可选：", ", ".join(ORDER))
        return 1

    for key in ORDER:
        if which and key != which:
            continue
        CASES[key]()
        print()
        time.sleep(1)

    return 0


if __name__ == "__main__":
    sys.exit(main())
