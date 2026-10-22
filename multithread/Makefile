# 历史多线程聊天室构建
#
#   make                 编译 server 和 client（默认严格：告警即错误）
#   make WERROR=         临时放宽，告警不再中断编译
#   make check           一次性列出所有错误（不会停在第一个）
#   make asan            编译到 build-asan/ 目录，不影响普通版
#   make clean           删除所有编译产物
#   make rebuild         重新编译
#
# 规则行必须以制表符开头。如果用编辑器改这个文件，
# 要确认那一行是 Tab 而不是空格，否则会报 "missing separator"。

CC      := gcc
LDFLAGS := -lpthread

# 默认就把告警当错误。想临时关掉：make WERROR=
WERROR  ?= -Werror

# 想加额外检查时用，例如：make EXTRA="-Wshadow -Wconversion"
EXTRA   ?=

CFLAGS  := -std=gnu11 -Wall -Wextra $(WERROR) $(EXTRA) -g

SERVER     := server
CLIENT     := client
ASAN_DIR   := build-asan
ASAN_FLAGS := -O1 -fsanitize=address,undefined -fno-omit-frame-pointer

all: $(SERVER) $(CLIENT)

$(SERVER): $(SERVER).o net.o protocol.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(CLIENT): $(CLIENT).o net.o protocol.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c net.h protocol.h
	$(CC) $(CFLAGS) -c -o $@ $<

# 一次性看到所有错误：-k 让 make 遇到错误也继续，
# 然后把 gcc 命令行过滤掉，只留报错和告警
check:
	@$(MAKE) -k all 2>&1 | grep -E "error:|warning:|Error [0-9]|undefined reference" || echo "===== 没有错误 ====="

# ASan 版编到单独目录，直接用 .c 一遍编完，不产生混用的 .o
# 这样在 make 和 make asan 之间来回切，永远不会冲突
asan:
	@mkdir -p $(ASAN_DIR)
	$(CC) $(CFLAGS) $(ASAN_FLAGS) -o $(ASAN_DIR)/$(SERVER) server.c net.c protocol.c $(LDFLAGS)
	$(CC) $(CFLAGS) $(ASAN_FLAGS) -o $(ASAN_DIR)/$(CLIENT) client.c net.c protocol.c $(LDFLAGS)
	@echo ""
	@echo "ASan 版编好了："
	@echo "  ./$(ASAN_DIR)/$(SERVER)"
	@echo "  ./$(ASAN_DIR)/$(CLIENT)"

clean:
	rm -f *.o $(SERVER) $(CLIENT)
	rm -rf $(ASAN_DIR)

rebuild: clean all

.PHONY: all check asan clean rebuild
