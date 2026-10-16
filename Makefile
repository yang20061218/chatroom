# 聊天室项目
#
#   make                  编译出 multconn_mutex_server 和 multconn_mutex_client
#   make clean            删掉所有编译产物
#   make rebuild          重新编译
#   make WERROR=-Werror   严格模式，有告警就编译失败
#
# 规则行必须以制表符开头。如果用编辑器改这个文件，
# 要确认那一行是 Tab 而不是空格，否则会报 "missing separator"。

CC      := gcc
LDFLAGS := -lpthread
WERROR  ?=
CFLAGS  := -Wall -Wextra $(WERROR) -g

SERVER  := server
CLIENT  := client

all: $(SERVER) $(CLIENT)

# 两个可执行文件都依赖 net.o，链接时必须带上，
# 否则会报 undefined reference to 'tcp_listen' / 'tcp_connect'
$(SERVER): $(SERVER).o net.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(CLIENT): $(CLIENT).o net.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# net.h 也列进来：头文件改了，相关的 .o 要重新编译，
# 否则会出现"头文件改了但程序行为没变"的怪事
%.o: %.c net.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o $(SERVER) $(CLIENT)

rebuild: clean all

.PHONY: all clean rebuild
