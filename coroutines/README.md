# Coroutines

## Stackfull coroutines

Allocate new stack and switch back and forth with `setjmp`/`longmp`.

POSIX had `makecontext`, but now it's deprecated.

Pros:
- regular C except for `coro_yield`/`coro_return`
    - local (stack) variables are preserved between generator calls
    - want 10 nested loops with gotos, early returns and gnu magick attribures like cleanup? no problem

Cons:
- allocating huge separate stacks is costly; libraries love to use a lot of stack
- impossible to dynamically grow stack without some help from compiler (not there yet)

## Stackless coroutines

Store whole state in some object (on the heap or on the caller stack) and call some function with it. Looks a lot like iterators from c++, shares the same problems.

Pros:
- very cheap in comparison with stackfull coroutines, only small state must be allocated (and even this allocation can be eliminated if needed)
- there's no need to carry values between two stacks, regular arguments and returns can be used, with any types
- can be done in standard C (however gnu extensions are very useful)

Cons:
- hard to read/write
    - no way to write `for (...) { yield x; }` with same ease
    - local (stack) variables are not preserved between generator calls
    - code is either Duff-device-like `switch` or a bunch of `goto`s
- heap allocations for state objects; another options is to leak state structure to the caller
(passing buffer from caller doesn't help much, because now you have to ensure that size of the buffer is correct every time)
