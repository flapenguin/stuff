#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <setjmp.h>

typedef void* coro_value_t;
typedef coro_value_t (*coro_function_t)(coro_value_t value);

typedef struct {
  jmp_buf user_ctx;
  jmp_buf coro_ctx;
  coro_value_t argument;
  coro_value_t yielded_value;
  coro_function_t fn;
  bool done;
} coro_t;

typedef struct {
  coro_t* coro;
  coro_function_t fn;
  void* stack;
  size_t stack_size;
  coro_value_t arg;
  void* user_stack;
  void* user_frame;
} coro__startup_t;

void coro_init(coro_t* coro, coro_function_t fn, void* stack, size_t stack_size, coro_value_t arg);
bool coro_next(coro_t* coro, coro_value_t* value, coro_value_t pass);
coro_value_t coro_yield(coro_value_t value);

static coro_t* coro_current;

void coro__wrap(coro__startup_t* ctx) {
  coro_yield((coro_value_t)0);
  coro_value_t result = ctx->fn(ctx->arg);
  coro_current->yielded_value = result;
  coro_current->done = true;
  longjmp(coro_current->user_ctx, 1);
}

void coro__init(coro__startup_t* ctx) {
  coro__startup_t* ctx_copy = (void*)((char*)ctx->stack + ctx->stack_size - sizeof(coro__startup_t));
  *ctx_copy = *ctx;
  ctx = ctx_copy;

  coro_current = ctx->coro;

  if (setjmp(ctx->coro->user_ctx) == 0) {
    __asm__ volatile(
      "movq %%rsp, %0\n"
      "movq %%rbp, %1\n"
      "movq %2, %%rsp\n"
      "movq %3, %%rbp\n"
      : "=r"(ctx->user_stack)
      , "=r"(ctx->user_frame)
      : "r"(ctx_copy)
      , "r"((char*)ctx->stack + ctx->stack_size)
    );

    __asm__ volatile(
      "movq %%rsp, %0\n"
      : "=r"(ctx)
    );

    coro__wrap(ctx);
    longjmp(ctx->coro->user_ctx, 1);
  }

  __asm__ volatile(
    "movq %0, %%rsp\n"
    "movq %1, %%rbp\n"
    :
    : "r"(ctx->user_stack)
    , "r"(ctx->user_frame)
  );
}

void coro_init(coro_t* coro, coro_function_t fn, void* stack, size_t stack_size, coro_value_t arg) {
  coro->done = false;
  coro->fn = fn;
  coro__startup_t ctx_instance = { coro, fn, stack, stack_size, arg };
  coro__init(&ctx_instance);
}

bool coro_next(coro_t* coro, coro_value_t* value, coro_value_t pass) {
  coro_t* const this_coro = coro_current;
  coro_current = coro;
  if (setjmp(coro->user_ctx) == 0) {
    /* Transfer control to aready running coroutine */
    coro->argument = pass;
    longjmp(coro->coro_ctx, 1);
  } else {
    /* Back from coroutine. */
    coro_current = this_coro;
    *value = coro->yielded_value;
    return !coro->done;
  }
}

coro_value_t coro_yield(coro_value_t value) {
  coro_t* coro = coro_current;
  coro_current->yielded_value = value;
  if (setjmp(coro->coro_ctx) == 0) {
    /* Back to user code */
    longjmp(coro->user_ctx, 1);
  } else {
    /* Returned to the coroutine */
    return coro->argument;
  }
}

coro_value_t gen_range(coro_value_t max) {
  printf("gen_range: start\n");

  for (uint64_t i = 0; i < (uint64_t)max; i++) {
    printf("gen_range: %ld\n", i);
    coro_yield((coro_value_t)i);
  }

  printf("gen_range: finish\n");

  return 0;
}

coro_value_t gen_squared(coro_value_t arg) {
  printf("gen_squared: start\n");

  coro_t* range = arg;

  uint64_t x;
  while (coro_next(range, (coro_value_t*)&x, 0)) {
    printf("gen_squared: %lu -> %lu\n", x, x*x);
    coro_yield((coro_value_t)(x*x));
  }

  printf("gen_squared: finish\n");

  return 0;
}


typedef struct {
  char* str;
  size_t len;
} readline_result_t;

coro_value_t gen_interaction(coro_value_t arg) {
  readline_result_t timezone = *(readline_result_t*)coro_yield((const char* []){ "readline", "/etc/timezone" });

  coro_yield((const char* []){ "writeline", timezone.str });

  return 0;
}

int main() {
  static char interaction_stack[4096];
  coro_t interaction;
  coro_init(&interaction, gen_interaction, interaction_stack, sizeof(interaction_stack), 0);

  readline_result_t readline_result;

  const char** request;
  void* response = 0;
  while (coro_next(&interaction, (coro_value_t)&request, response)) {
    response = 0;

    if (strcmp(request[0], "readline") == 0) {
      FILE* f = fopen(request[1], "r");
      readline_result = (readline_result_t){ 0, 0 };
      getline(&readline_result.str, &readline_result.len, f);
      fclose(f);

      response = &readline_result;
    }
    else if (strcmp(request[0], "writeline") == 0) {
      fputs(request[1], stdout);
    }
  }

  static char range_stack[4096];
  coro_t range;
  coro_init(&range, gen_range, range_stack, sizeof(range_stack), (coro_value_t)4ul);

  static char squared_stack[4096];
  coro_t squared;
  coro_init(&squared, gen_squared, squared_stack, sizeof(squared_stack), &range);

  coro_value_t x;
  while (coro_next(&squared, &x, 0)) {
    printf("main: %lu\n", (uint64_t)x);
    fflush(stdout);
  }

  printf("result: %lu\n", (uint64_t)x);
}
