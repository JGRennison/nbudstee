all: nbudstee

VERSION_STRING := $(shell git describe --always --dirty=-m 2>/dev/null || date "+%F %T %z" 2>/dev/null)
ifdef VERSION_STRING
CVFLAGS := -DVERSION_STRING='"${VERSION_STRING}"'
endif

nbudstee: nbudstee.cpp
	g++ nbudstee.cpp -Wall --std=gnu++0x -O3 -g -o nbudstee ${CVFLAGS}

.PHONY: all install clean

clean:
	rm -f nbudstee nbudstee.1

install: nbudstee
	install -m 755 nbudstee /usr/local/bin/


HELP2MANOK := $(shell help2man --version 2>/dev/null)
ifdef HELP2MANOK
all: nbudstee.1

nbudstee.1: nbudstee
	help2man -s 1 -N ./nbudstee -n "Non-Blocking Unix Domain Socket Tee" -o nbudstee.1

install: install-man

.PHONY: install-man

install-man: nbudstee.1
	install -m 644 nbudstee.1 /usr/local/share/man/man1/
	-mandb -pq
else
$(shell echo "Install help2man for man page generation" >&2)
endif
