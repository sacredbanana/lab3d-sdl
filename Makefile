CC := gcc

BIN  = ken.bin
SRCS = $(wildcard *.c)
OBJS = $(SRCS:%.c=%.o)

WARNFLAGS = -Wall -Wno-pointer-sign -Wno-unused-result -Wno-unused-but-set-variable -Wno-unused-variable -Wno-unused-function
OPTFLAGS  = -O2 -fno-aggressive-loop-optimizations -fno-strict-aliasing


all: $(BIN)

clean:
	rm -f $(BIN) $(OBJS)

$(BIN): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(shell sdl2-config --libs) -lSDL2_image -lGL -lGLU -lm -lz

%.o: %.c
	$(CC) -DUSE_OSS -g $(WARNFLAGS) $(OPTFLAGS) $(CFLAGS) $(CPPFLAGS) $(shell sdl2-config --cflags) -o $@ -c $<
