CFLAGS=-std=c99  -g   -march=native


tv: *.c tv.h glyphs.inc
	$(CC) $(CFLAGS) -o $@ *.c -lm `pkg-config --cflags --libs libjpeg libpng --static` 

o3: *.c tv.h glyphs.inc
	$(CC) $(CFLAGS) -o tv -O3 *.c -lm `pkg-config --cflags --libs libjpeg libpng --static` 


tv-static: gen-musl *.c
	./gen-musl
	mv tv tv-static
clean:
	rm tv
install:
	[ -f tv ] && install tv $(DESTDIR)$(PREFIX)/bin || true
