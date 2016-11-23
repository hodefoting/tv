CFLAGS=-std=c99  -g  -O2

tv: *.c tfb.h glyphs.inc
	$(CC) $(CFLAGS) -o $@ *.c -lm `pkg-config --cflags --libs libjpeg libpng --static`
clean:
	rm tv
install:
	[ -f tv ] && install tv $(DESTDIR)$(PREFIX)/bin || true
