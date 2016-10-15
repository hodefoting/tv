CFLAGS=-O2 -std=c99 

sxv: *.c
	gcc $(CFLAGS) -o $@ sxv.c -lm
clean:
	rm sxv
install:
	[ -f sxv ] && install sxv $(DESTDIR)$(PREFIX)/bin || true
