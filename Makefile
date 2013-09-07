OBJS=subs.o init.o graphx.o lab3d.o setup.o adlibemu.o oldlab3d.o demo.o

WARNFLAGS=-Wall -Wno-pointer-sign -Wno-unused-result -Wno-unused-but-set-variable

default: ken.bin

%.o: %.c lab3d.h
	gcc -DUSE_OSS -g $(WARNFLAGS) -fstrict-aliasing `sdl2-config --cflags` -O2 -o $@ -c $<


ken.bin: $(OBJS)
	gcc -o ken.bin $(OBJS) `sdl2-config --libs` -lz -lSDL2_image -lGL -lGLU -lm

clean:
	rm -f $(OBJS)
