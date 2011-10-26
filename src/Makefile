CC      = gcc
C_FLAGS = -Wall -O -pedantic
L_FLAGS = -lz -lpthread -lcrypt

O_FILES = socket.o io.o strings.o update.o utils.o interpret.o help.o \
	  action_safe.o mccp.o save.o

all: $(O_FILES)
	rm -f SocketMud
	$(CC) $(L_FLAGS) -o SocketMud $(O_FILES)

.c.o: all
	$(CC) -c $(C_FLAGS) $<

clean:
	rm -f *.o
	rm -f SocketMud
