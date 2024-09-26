</$objtype/mkfile

LIB=libgraphics.a$O
OFILES=\
	camera.$O\
	viewport.$O\
	render.$O\
	clip.$O\
	xform.$O\
	scene.$O\
	vertex.$O\
	texture.$O\
	alloc.$O\
	fb.$O\
	shadeop.$O\
	color.$O\
	util.$O\
	nanosec.$O\
	marshal.$O\

HFILES=\
	graphics.h\
	internal.h\

UPDATE=\
	mkfile\
	$HFILES\
	${OFILES:%.$O=%.c}\

</sys/src/cmd/mklib
