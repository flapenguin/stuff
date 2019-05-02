CFLAGS += -Werror=missing-prototypes
CFLAGS += -Werror=missing-declarations
CFLAGS += -Werror=implicit-function-declaration

bin/gpu: gpu.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -o $@ $^ -lxcb -lxcb-dri3 -lxcb-randr -lm
