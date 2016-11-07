CFLAGS=-std=c99  -g 

tv: *.c tfb.h
	$(CC) $(CFLAGS) -o $@ *.c -lm `pkg-config --cflags --libs libjpeg libpng --static`
clean:
	rm tv
install:
	[ -f tv ] && install tv $(DESTDIR)$(PREFIX)/bin || true
