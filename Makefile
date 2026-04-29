NAME = spod
VERSION = 0.1

PREFIX = /usr/local
MANPREFIX = ${PREFIX}/share/man

CPPFLAGS = -DVERSION=\"${VERSION}\"
CFLAGS = -std=c99 -pedantic -Wall -Wextra -Os ${CPPFLAGS}

SRC = spod.c
OBJ = ${SRC:.c=.o}

all: ${NAME}

.c.o:
	${CC} -c ${CFLAGS} $<

${NAME}: ${OBJ}
	${CC} -o $@ ${OBJ} ${LDFLAGS}

clean:
	rm -f ${NAME} ${OBJ} ${NAME}-${VERSION}.tar.gz

dist: clean
	mkdir -p ${NAME}-${VERSION}
	cp -R LICENSE Makefile README config.def.h config.h \
		${SRC} ${NAME}-${VERSION}
	tar -czf ${NAME}-${VERSION}.tar.gz ${NAME}-${VERSION}
	rm -rf ${NAME}-${VERSION}

install: all
	mkdir -p ${DESTDIR}${PREFIX}/bin
	cp -f ${NAME} ${DESTDIR}${PREFIX}/bin
	chmod 755 ${DESTDIR}${PREFIX}/bin/${NAME}
	mkdir -p ${DESTDIR}${MANPREFIX}/man1
	cp -f ${NAME}.1 ${DESTDIR}${MANPREFIX}/man1
	chmod 644 ${DESTDIR}${MANPREFIX}/man1/${NAME}.1

uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/${NAME}
	rm -f ${DESTDIR}${MANPREFIX}/man1/${NAME}.1

.PHONY: all clean dist install uninstall
