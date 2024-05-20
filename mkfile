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
	util.$O\
	nanosec.$O\

HFILES=\
	graphics.h\
	internal.h\
	libobj/obj.h

UPDATE=\
	mkfile\
	$HFILES\
	${OFILES:%.$O=%.c}\

</sys/src/cmd/mklib

libobj/libobj.a$O:
	cd libobj
	mk install

pulldeps:VQ:
	git/clone git://antares-labs.eu/libobj || \
	git/clone git://shithub.us/rodri/libobj || \
	git/clone https://github.com/sametsisartenep/libobj

clean nuke:V:
	rm -f *.[$OS] $LIB
	@{cd libobj; mk $target}
