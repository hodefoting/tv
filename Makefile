CFLAGS=-std=c99  -g 
FASTFLAGS=-march=native -ffast-math -O3 -funroll-loops  -fforce-addr -ftracer -fpeel-loops -fmerge-all-constants

tv: *.c tv.h glyphs.inc
	$(CC) $(CFLAGS) -o $@ *.c -lm `pkg-config --cflags --libs libjpeg libpng --static` 

o3: *.c tv.h glyphs.inc
	$(CC) $(CFLAGS) $(FASTFLAGS) -o tv *.c -lm `pkg-config --cflags --libs libjpeg libpng --static` 


tv-static: gen-musl *.c
	./gen-musl
	mv tv tv-static
clean:
	rm tv
install:
	[ -f tv ] && install tv $(DESTDIR)$(PREFIX)/bin || true
