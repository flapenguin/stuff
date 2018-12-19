#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

/********/
/* core */
/********/

#define GeneratorDefine(Name, Prefix, Type, ...) \
  typedef struct { Type value; bool done; } Prefix##_result_t; \
  typedef struct Prefix##__struct Name; \
  typedef Prefix##_result_t (*Prefix##_next_fn)(Name* self, ##__VA_ARGS__); \
  struct Prefix##__struct { Prefix##_next_fn next; void (*cleanup)(Name*); };

#define GeneratorForEach(Value, Range) \
  for (__auto_type Value = Range->next(Range); !Value.done; Value = Range->next(Range))

/****************/
/* memory stuff */
/****************/

GeneratorDefine(int_generator_t, int_generator, int);

#define HeapGeneratorNew(State, Next, Cleanup, ...) ({ \
    State* state = malloc(sizeof(*state)); \
    *state = (State){ {(void*)Next, Cleanup}, __VA_ARGS__ }; \
    &state->base; \
  })

#define HeapGeneratorFree (void*)free

/**********************************/
/* utils (can leave without them) */
/**********************************/

#define auto __auto_type

#define LAMBDA(ReturnValue, Arguments, Body) \
  ({ ReturnValue unnamedlambdafn Arguments Body; &unnamedlambdafn; })

/* It would be nice to have foo_t* x CLEANUP(x) with _Generic inside, but it doesn't work with attributes. */
#define CLEANUP_int_generator __attribute__((cleanup(int_generator__cleanup)))

void int_generator__cleanup(int_generator_t** self) {
  if (*self && (*self)->cleanup) {
    (*self)->cleanup(*self);
  }
}

/*************/
/* gen_range */
/*************/

typedef struct {
  int_generator_t base;
  int current;
  int to;
} gen_range_generator_t;

int_generator_result_t gen_range__next(void* self_) {
  gen_range_generator_t* self = self_;
  return self->current < self->to
    ? (int_generator_result_t){ self->current++, false }
    : (int_generator_result_t){ 0, true };
}

int_generator_t* gen_range(int from, int to) {
  return HeapGeneratorNew(gen_range_generator_t, gen_range__next, HeapGeneratorFree, from, to);
}

/****************/
/* gen_transform */
/****************/

typedef struct {
  int_generator_t base;
  int_generator_t* source;
  int (*callback)(int arg);
} gen_transform_generator_t;

int_generator_result_t gen_transform__next(void* self_) {
  gen_transform_generator_t* self = self_;
  auto res = self->source->next(self->source);
  return (int_generator_result_t){ .value = res.done ? 0 : self->callback(res.value), .done = res.done };
}

int_generator_t* gen_transform(int_generator_t* source, int (*callback)(int arg)) {
  return HeapGeneratorNew(gen_transform_generator_t, gen_transform__next, HeapGeneratorFree, source, callback);
}

/**************/
/* gen_filter */
/**************/

typedef struct {
  int_generator_t base;
  int_generator_t* source;
  bool (*callback)(int arg);
} gen_filter_generator_t;

int_generator_result_t gen_filter__next(void* self_) {
  gen_filter_generator_t* self = self_;
  while (true) {
    auto res = self->source->next(self->source);
    if (res.done) {
      return (int_generator_result_t){ .value = 0, .done = true };
    }
    if (self->callback(res.value)) {
      return (int_generator_result_t){ .value = res.value, .done = false };
    }
  }
}

int_generator_t* gen_filter(int_generator_t* source, bool (*callback)(int arg)) {
  return HeapGeneratorNew(gen_filter_generator_t, gen_filter__next, HeapGeneratorFree, source, callback);
}

/************/
/* gen_head */
/************/

typedef struct {
  int_generator_t base;
  int_generator_t* source;
  size_t left;
} gen_head_generator_t;

int_generator_result_t gen_head__next(void* self_) {
  gen_head_generator_t* self = self_;
  if (!self->left) {
    return (int_generator_result_t){ .done = true };
  }

  self->left--;
  auto res = self->source->next(self->source);
  return (int_generator_result_t){ .value = res.value, .done = res.done };
}

int_generator_t* gen_head(int_generator_t* source, size_t length) {
  return HeapGeneratorNew(gen_head_generator_t, gen_head__next, HeapGeneratorFree, source, length);
}

/************/
/* gen_tail */
/************/

typedef struct {
  int_generator_t base;
  int_generator_t* source;
  size_t length;
  size_t pos;
  bool completed;
  int buffer[];
} gen_tail_generator_t;

int_generator_result_t gen_tail__next(void* self_) {
  gen_tail_generator_t* self = self_;
  if (!self->completed) {
    self->completed = true;

    /* Read whole sequence until end and keep last N entries. */
    self->pos = 0;
    GeneratorForEach(it, self->source) {
      if (self->pos == self->length) {
        memmove(self->buffer, self->buffer + 1, sizeof(self->buffer[0]) * (self->length - 1));
        self->pos = self->length - 1;
      }

      self->buffer[self->pos++] = it.value;
    }

    /* Remember how much we currenty have in buffer. */
    self->length = self->pos;
    self->pos = 0;
  }


  if (self->pos == self->length) {
    return (int_generator_result_t){ .done = true };
  }

  int value = self->buffer[self->pos++];
  return (int_generator_result_t){ .value = value, .done = false };
}

int_generator_t* gen_tail(int_generator_t* source, size_t length) {
  gen_tail_generator_t* state = malloc(sizeof(*state) + sizeof(state->buffer[0])*length);
  state->base.next = (void*)gen_tail__next;
  state->base.cleanup = HeapGeneratorFree;
  state->source = source;
  state->length = length;
  state->completed = false;

  return (void*)state;
}

/*******************/
/* gen_interaction */
/*******************/

GeneratorDefine(interaction_generator_t, interaction_generator, const char**, const char*);

typedef struct {
  interaction_generator_t base;
  void* state;
} gen_interaction_generator_t;

interaction_generator_result_t gen_interaction__next(gen_interaction_generator_t* self, const char* response) {
  if (self->state) goto *self->state;

  state_start: {
    self->state = &&state_onread;
    static const char* request[] = { "readline", "/etc/timezone" };
    return (interaction_generator_result_t){ .value = request, .done = false };
  }

  state_onread: {
    self->state = &&state_exit;
    static const char* request[] = { "writeline", "" };
    request[1] = response;
    return (interaction_generator_result_t){ .value = request, .done = false };
  }

  state_exit: {
    return (interaction_generator_result_t){ .value = 0, .done = true };
  }
}

interaction_generator_t* gen_interaction() {
  return HeapGeneratorNew(gen_interaction_generator_t, gen_interaction__next, HeapGeneratorFree, 0);
}


/********/
/* main */
/********/

int main(char* argv[], int argc) {
  int_generator_t* range CLEANUP_int_generator = gen_range(0, 1000);
  int_generator_t* squared CLEANUP_int_generator = gen_transform(range, LAMBDA(int, (int arg), { return arg*arg; }));
  int_generator_t* head CLEANUP_int_generator = gen_head(squared, 100);
  int_generator_t* tail CLEANUP_int_generator = gen_tail(head, 10);

  printf("[0..1000] | square | head(100) | tail(10): ");
  GeneratorForEach(it, tail) {
    printf("%d ", it.value);
  }
  printf("\n");

  int_generator_t* numbers CLEANUP_int_generator = gen_range(2, __INT_MAX__);
  int_generator_t* primes CLEANUP_int_generator = gen_filter(numbers, LAMBDA(bool, (int arg), {
    if (arg == 2) return true;
    if (arg % 2 == 0) return false;
    for (int i = 3; i <= sqrt(arg); i += 2) {
      if (arg % i == 0) return false;
    }
    return true;
  }));

  int_generator_t* first_primes CLEANUP_int_generator = gen_head(primes, 20);

  printf("first 20 primes: ");
  GeneratorForEach(it, first_primes) {
    printf("%d ", it.value);
  }
  printf("\n");

  interaction_generator_t* interaction = gen_interaction();
  char* response = 0;
  while (true) {
    interaction_generator_result_t res = interaction->next(interaction, response);
    if (res.done) break;

    response = 0;

    if (strcmp(res.value[0], "readline") == 0) {
      size_t capacity = 0;
      FILE* f = fopen(res.value[1], "r");
      size_t length = getline(&response, &capacity, f);
      response[length - 1] = '\0'; // cut \n
      continue;
    }

    if (strcmp(res.value[0], "writeline") == 0) {
      printf("%s\n", res.value[1]);
      continue;
    }
  }

  interaction->cleanup(interaction);

  return 0;
}
