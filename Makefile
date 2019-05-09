CFLAGS ?= -Wall -O2 -g -Wstrict-prototypes -Wmissing-prototypes
LDFLAGS ?= -Wl,-z,defs
LIBRARIES = -lX11

all: xwit

xwit: xwit.o ClientWin.o dsimple.o
	gcc $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIBRARIES)

%.o: %.c
	gcc $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

# Headers needed
dsimple.o: dsimple.h
xwit.o: dsimple.h ClientWin.h
ClientWin.o: ClientWin.h

clean:
	-rm xwit *.o

install: xwit
	install -m 755 xwit /usr/local/bin/
	install xwit.man /usr/local/share/man/man1/xwit.1

uninstall:
	$(RM) /usr/local/bin/xwit /usr/local/share/man/man1/xwit.1
