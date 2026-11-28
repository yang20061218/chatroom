#!/usr/bin/env python3
"""
粘包和半包回归测试，当前 11 字节协议。

协议头：
    magic(4) + version(1) + cmd(2) + length(4)

运行前先启动服务端，并在另一个终端登录一个普通客户端观察广播。

用法：
    python3 test_sticky.py
    python3 test_sticky.py 1
"""

import os
import socket
import struct
import sys
import time

HOST = "127.0.0.1"
PORT = 7777

MAGIC = b"1218"
VERSION = 1
HEAD_LEN = 11

CMD_CHAT = 1
CMD_HEART = 3
CMD_LOGIN = 4
CMD_LOGIN_RES = 5


def recv_exact(sock, size):
    data = b""
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise ConnectionError("connection closed")
        data += chunk
    return data


def recv_frame(sock):
    header = recv_exact(sock, HEAD_LEN)
    if header[:4] != MAGIC:
        raise ValueError("bad magic")
    if header[4] != VERSION:
        raise ValueError("bad version")
    cmd, length = struct.unpack("!HI", header[5:11])
    body = recv_exact(sock, length)
    return cmd, body


def pack(cmd, body):
    if isinstance(body, str):
        body = body.encode("utf-8")
    return MAGIC + bytes([VERSION]) + struct.pack("!HI", cmd, len(body)) + body


def unique_name(prefix):
    suffix = f"{int(time.time() * 1000) % 1000000:06d}"
    return f"{prefix}{suffix}"[:31]


def connect(name):
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(3)
    sock.connect((HOST, PORT))
    sock.sendall(pack(CMD_LOGIN, name))

    cmd, body = recv_frame(sock)
    if cmd != CMD_LOGIN_RES or body == b"no":
        sock.close()
        raise RuntimeError(f"login failed: {body!r}")
    return sock


def case1_two_messages_one_send():
    print("用例 1：两帧聊天消息一次发送")
    sock = connect(unique_name("sticky"))
    sock.sendall(pack(CMD_CHAT, "aaa") + pack(CMD_CHAT, "bbb"))
    time.sleep(1)
    sock.close()
    print("预期：观察客户端收到 aaa 和 bbb 两条独立广播")


def case2_header_split():
    print("用例 2：11 字节协议头和正文分段发送")
    sock = connect(unique_name("split"))
    frame = pack(CMD_CHAT, "ccc")
    sock.sendall(frame[:2])
    time.sleep(0.2)
    sock.sendall(frame[2:5])
    time.sleep(0.2)
    sock.sendall(frame[5:11])
    time.sleep(0.2)
    sock.sendall(frame[11:])
    time.sleep(1)
    sock.close()
    print("预期：观察客户端收到完整的 ccc")


def case3_long_body():
    print("用例 3：3000 字节正文分 5 段发送")
    sock = connect(unique_name("long"))
    text = "d" * 3000
    frame = pack(CMD_CHAT, text)
    step = (len(frame) + 4) // 5
    for offset in range(0, len(frame), step):
        sock.sendall(frame[offset:offset + step])
        time.sleep(0.1)
    time.sleep(1)
    sock.close()
    print("预期：观察客户端收到一条包含 3000 个 d 的广播")


def case4_huge_length():
    print("用例 4：谎报 2GB 正文长度")
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(3)
    sock.connect((HOST, PORT))
    header = MAGIC + bytes([VERSION]) + struct.pack("!HI", CMD_CHAT, 2 * 1024 * 1024 * 1024)
    sock.sendall(header)
    time.sleep(0.5)
    sock.close()
    print("预期：服务端拒绝该连接并继续运行")


def case5_empty_login():
    print("用例 5：空昵称登录")
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(2)
    sock.connect((HOST, PORT))
    sock.sendall(pack(CMD_LOGIN, b""))

    try:
        _, body = recv_frame(sock)
        if body not in (b"no", b"login failed,please try again"):
            raise AssertionError(f"unexpected login response: {body!r}")
    except (ConnectionError, socket.timeout):
        pass
    finally:
        sock.close()

    print("预期：服务端返回失败或关闭连接")


CASES = {
    "1": case1_two_messages_one_send,
    "2": case2_header_split,
    "3": case3_long_body,
    "4": case4_huge_length,
    "5": case5_empty_login,
}

ORDER = ["1", "2", "3", "5", "4"]


def main():
    selected = sys.argv[1] if len(sys.argv) > 1 else None
    if selected and selected not in CASES:
        print("可用用例：", ", ".join(ORDER))
        return 1

    for key in ORDER:
        if selected and key != selected:
            continue
        CASES[key]()
        print()
        time.sleep(0.5)
    return 0


if __name__ == "__main__":
    sys.exit(main())
