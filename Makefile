OBJS=subs.o init.o graphx.o lab3d.o setup.o adlibemu.o oldlab3d.o demo.o

%.o: %.c lab3d.h
	gcc -DUSE_OSS -g -Wno-unused-result -fstrict-aliasing `sdl2-config --cflags` -O2 -o $@ -c $<

default: $(OBJS)
	gcc -o ken $(OBJS) `sdl2-config --libs` -lz -lSDL2_image -lGL -lGLU -lm

clean:
	rm -f $(OBJS)
