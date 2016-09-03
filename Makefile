CC = gcc
CFLAGS = -O2 -DSVR4 -D_XOPEN_SOURCE=500

TARGET = ttyrun

all: $(TARGET)

ttyrun: ttyrun.o
	$(CC) $(LDFLAGS) $(CFLAGS) -o ttyrun ttyrun.o

clean:
	rm -f *.o ttyrun
