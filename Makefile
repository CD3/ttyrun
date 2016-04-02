CC = gcc
CFLAGS = -O2 -DSVR4

TARGET = ttyrun

all: $(TARGET)

ttyrun: ttyrun.o
	$(CC) $(LDFLAGS) $(CFLAGS) -o ttyrun ttyrun.o

clean:
	rm -f *.o ttyrun
