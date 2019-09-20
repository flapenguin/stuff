CFLAGS += -Werror=missing-prototypes
CFLAGS += -Werror=missing-declarations
CFLAGS += -Werror=implicit-function-declaration

LDFLAGS += -lxcb -lxcb-dri3 -lxcb-randr -lm

all: bin/gpu bin/gpu.3d bin/ioctl-logger

bin/gpu: gpu.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

bin/gpu.3d: gpu.3d.c ioctl-logger.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) -ldl

bin/ioctl-logger: ioctl-logger.c
	@mkdir -p $(@D)
	gcc -std=gnu11 -Wall -fPIC -shared -o $@ $^ -ldl
