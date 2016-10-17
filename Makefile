CFLAGS=-O2 -std=c99  -g -p

sxv: *.c
	gcc $(CFLAGS) -o $@ sxv.c -lm `pkg-config --cflags --libs libjpeg`
clean:
	rm sxv
install:
	[ -f sxv ] && install sxv $(DESTDIR)$(PREFIX)/bin || true
