bin/wasm: wasm_parse.c main.c wasm.h
	@mkdir -p $(@D)
	$(CC) -Wno-multichar -g -o $@ $(filter-out %.h,$^)
