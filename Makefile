CC      := gcc
LDFLAGS := -lpthread

# 默认只开告警。想严格模式（告警即错误）就执行：make WERROR=-Werror
WERROR  ?=
CFLAGS  := -Wall -Wextra $(WERROR) -g

# 自动找出目录下所有 .c 文件，每个编译成一个同名可执行文件。
# 所以源文件叫什么名字都不用改这里。
SRCS := $(wildcard *.c)
BINS := $(SRCS:.c=)

all: $(BINS)

%: %.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(BINS)

rebuild: clean all

.PHONY: all clean rebuild
