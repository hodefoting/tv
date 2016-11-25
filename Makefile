CFLAGS=-std=c99  -g  -O3

tv: *.c tv.h glyphs.inc
	$(CC) $(CFLAGS) -o $@ *.c -lm `pkg-config --cflags --libs libjpeg libpng --static` 
tv-static: gen-musl *.c
	./gen-musl
	mv tv tv-static
clean:
	rm tv
install:
	[ -f tv ] && install tv $(DESTDIR)$(PREFIX)/bin || true
