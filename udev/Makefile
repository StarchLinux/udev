.POSIX:
.SUFFIXES: .c .o

SRC = udevadm.c udevd.c

OFILES.common  = udev-event.o udev-watch.o udev-node.o udev-rules.o udev-util.o libudev-device-db-write.o
OFILES.udevadm = $(OFILES.common) udevadm-control.o udevadm-info.o udevadm-monitor.o udevadm-settle.o udevadm-test.o udevadm-trigger.o
OFILES.udevd   = $(OFILES.common)

OBJ = $(SRC:S/.c/.o/)
BIN = $(SRC:S/.c//)

CFLAGS += -include ../config.h

all: $(BIN)

$(BIN): lib/libudev.a

.for x in $(BIN)
$x: $x.o $(OFILES.$x)
	@echo LD $@
	@$(CC) $(LDFLAGS) -o $@ $x.o $(OFILES.$x) lib/libudev.a

clean: clean.$x

clean.$x:
	rm -f $(OFILES.$x)
.endfor

.c.o:
	@echo CC $<
	@$(CC) -c -o $@ $< $(CFLAGS)

lib/libudev.a:
	@(cd lib && $(MAKE))

install: all
	@echo installing executables to $(DESTDIR)$(PREFIX)/bin
	@mkdir -p $(DESTDIR)$(PREFIX)/bin
	@cp $(BIN) $(DESTDIR)$(PREFIX)/bin
	@(cd $(DESTDIR)$(PREFIX)/bin && chmod 755 $(BIN))
	@echo installing manual pages to $(DESTDIR)$(MANPREFIX)/man1
	@mkdir -p $(DESTDIR)$(MANPREFIX)/man1
	@cp $(MAN) $(DESTDIR)$(MANPREFIX)/man1
	@(cd $(DESTDIR)$(MANPREFIX)/man1 && chmod 644 $(MAN))
	@echo installing library
	@(cd lib && $(MAKE) install)

uninstall:
	@echo removing executables from $(DESTDIR)$(PREFIX)/bin
	@(cd $(DESTDIR)$(PREFIX)/bin && rm -f $(BIN))
	@echo removing manual pages from $(DESTDIR)$(MANPREFIX)/man1
	@(cd $(DESTDIR)$(MANPREFIX)/man1 && rm -f $(MAN))
	@echo removing library
	$(cd lib && $(MAKE) uninstall)

clean:
	@echo cleaning
	@rm -f $(BIN) $(OBJ) $(LIB) util.a
