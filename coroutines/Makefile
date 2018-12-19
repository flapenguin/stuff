all: bin/stackful bin/stackless

bin/stackful: stackful.c
	@mkdir -p $(@D)
	$(CC) -g -std=c11 -o $@ $^

bin/stackless: stackless.c
	@mkdir -p $(@D)
	$(CC) -g -std=c11 -o $@ $^ -lm
