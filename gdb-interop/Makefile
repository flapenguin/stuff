CXX := clang -flto
CXXFLAGS := -std=c99 -O0 -fPIC -nostdlib
# CXXFLAGS += -shared # TODO: dynamically linked shared executable, requires self-relocation
# CXXFLAGS += -g
CXXFLAGS += -fno-omit-frame-pointer
CXXFLAGS += -fno-exceptions -fno-asynchronous-unwind-tables -fno-unwind-tables # strip unneeded sections

bin/exe: main.c syscall.inl
	@mkdir -p $(@D)
	${CXX} ${CXXFLAGS} -o $@ ${filter %.c,$^}
