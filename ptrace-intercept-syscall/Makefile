all: bin/emulator bin/exe
clean: ; rm -rf bin/

bin/emulator: emulator.c
	@mkdir -p $(@D)
	$(CC) -o $@ $^

bin/exe: exe.S
	$(CC) -o $@ $^