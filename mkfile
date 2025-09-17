</$objtype/mkfile

TARG=herbe
OFILES=herbe.$O

HFILES=config.h

BIN=$home/bin

$TARG: $OFILES
	$LD -o $target $prereq

%.$O: %.c $HFILES
	$CC $CFLAGS $stem.c

install:V: $TARG
	cp $TARG $BIN/
