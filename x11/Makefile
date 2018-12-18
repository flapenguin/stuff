all: bin/x11 bin/xcb
clean: ; rm -rf bin/

bin/x11: x11.c
	@mkdir -p $(@D)
	$(CC) -g -o $@ $^

bin/xcb: xcb.c
	@mkdir -p $(@D)
	$(CC) -o $@ $^ `pkg-config --libs --cflags xcb`
