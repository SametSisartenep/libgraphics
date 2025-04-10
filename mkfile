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
	raster.$O\
	fb.$O\
	shadeop.$O\
	color.$O\
	util.$O\
	nanosec.$O\
	marshal.$O\
	`{~ $objtype amd64\
		&& echo memsetl-$objtype.$O\
		|| echo memsetl.$O}

HFILES=\
	graphics.h\
	internal.h\

UPDATE=\
	mkfile\
	$HFILES\
	${OFILES:%.$O=%.c}\

</sys/src/cmd/mklib
