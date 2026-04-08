CC = cc
CFLAGS = -std=c11 -Wall -Wextra -pedantic -Iinclude

TARGET = mini_sql
TRACE_TARGET = mini_sql_trace
SRCS = \
	src/main.c \
	src/util.c \
	src/ast.c \
	src/tokenizer.c \
	src/parser.c \
	src/optimizer.c \
	src/storage.c \
	src/executor.c
OBJS = $(SRCS:.c=.o)
TRACE_SRCS = \
	src/demo_trace_main.c \
	src/util.c \
	src/ast.c \
	src/tokenizer.c \
	src/parser.c \
	src/optimizer.c \
	src/storage.c \
	src/executor.c \
	src/trace.c
TRACE_OBJS = $(TRACE_SRCS:.c=.o)

.PHONY: all clean

all: $(TARGET) $(TRACE_TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

$(TRACE_TARGET): $(TRACE_OBJS)
	$(CC) $(CFLAGS) -o $@ $(TRACE_OBJS)

clean:
	rm -f $(TARGET) $(TRACE_TARGET) src/*.o
