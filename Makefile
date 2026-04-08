CC = cc
CFLAGS = -std=c11 -Wall -Wextra -pedantic -Iinclude

TARGET = mini_sql
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

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

clean:
	rm -f $(TARGET) src/*.o
