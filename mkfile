</$objtype/mkfile

LIB=libgraphics.a$O
OFILES=\
	camera.$O\
	render.$O\

HFILES=graphics.h ../libgeometry/geometry.h

CFLAGS=$CFLAGS -I. -I../libgeometry

</sys/src/cmd/mklib
