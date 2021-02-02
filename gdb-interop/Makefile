CC := gcc
CFLAGS := -std=c99
CFLAGS += -flto
CFLAGS += -Wl,--hash-style=both # add DT_HASH alongside DT_GNU_HASH
CFLAGS += -nostdlib -nolibc -nodefaultlibs # ditch all standart libraries
CFLAGS += -ffreestanding
CFLAGS += -g -Og -O0 # give us debug information and do not optimize anything at all
CFLAGS += -fno-omit-frame-pointer # for ease of debugging
CFLAGS += -fno-exceptions -fno-asynchronous-unwind-tables -fno-unwind-tables # strip unneeded sections
CFLAGS += -Wl,--entry,__entry_point__ # use custom entry point instead of _start, just because we can

CFLAGS_STATIC += -static
CFLAGS_DYNAMIC += -shared -fPIC # -Bsymbolic -Bsymbolic-functions -ffunction-sections

all: bin/fake-1.so bin/fake-2.so bin/static bin/dynamic bin/interpreted
clean:; rm -rf bin

bin/fake-1.so bin/fake-2.so: fake-so.c
	@mkdir -p ${@D}
	gcc -shared -o $@ $^

bin/static: main.c main-static.c
	@mkdir -p ${@D}
	${CC} ${CFLAGS} ${CFLAGS_STATIC} -DFAKES_DIR=\"${shell pwd}/bin\" -DSTATIC -o $@ $^

bin/dynamic: main.c main-dynamic.c
	@mkdir -p ${@D}
	${CC} ${CFLAGS} ${CFLAGS_DYNAMIC} -DFAKES_DIR=\"${shell pwd}/bin\" -o $@ $^

# Notes on options here:
# -O0 is repeated after .c files because otherfile ld can't find __entry_point_impl__ for some reason.
# --unresolved-symbols to stop complaining about missing symbols, I will resolve them manually in runtime anyway.
bin/interpreted: bin/dynamic interpreted.c
	@mkdir -p ${@D}
	${CC} ${CFLAGS} -fPIE -o $@ -Og -Wl,-I${shell pwd}/$< ${filter %.c,$^} -O0 -Wl,--unresolved-symbols=ignore-in-object-files
