CFLAGS=-O2 -std=c99  -g

tv: *.c
	gcc $(CFLAGS) -o $@ tv.c -lm `pkg-config --cflags --libs libjpeg libpng`
clean:
	rm tv
install:
	[ -f tv ] && install tv $(DESTDIR)$(PREFIX)/bin || true
