CC = gcc
CFLAGS = -Wall -Werror -g -pthread
TARGET = acs
SRCS = acs.c
OBJS = $(SRCS:.c=.o)
RAND_GEN = inputGen.c
RAND_INPUT = randinput.txt

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $<

rand: $(TARGET) $(RAND_GEN)
	$(CC) $(CFLAGS) -o inputGen $(RAND_GEN)
	./inputGen > $(RAND_INPUT)

clean:
	rm -f $(TARGET) $(OBJS) inputGen $(RAND_INPUT)
