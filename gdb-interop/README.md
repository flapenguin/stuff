# Interop with the debugger about loaded objects

```
$ make
clang -flto -std=c99 -O0 -fPIC -nostdlib -fno-omit-frame-pointer -fno-exceptions -fno-asynchronous-unwind-tables -fno-unwind-tables  -o bin/exe main.c
$ gdb -q ./bin/exe
Reading symbols from ./bin/exe...
(No debugging symbols found in ./bin/exe)
(gdb) r
Starting program: /home/flapenguin/projects/stuff/linker-debugger-interop/bin/exe

Program received signal SIGTRAP, Trace/breakpoint trap.
0x000000000040117d in _start ()
(gdb) info shared
warning: .dynamic section for "/lib64/ld-linux-x86-64.so.2" is not at the expected address (wrong library or version mismatch?)
warning: .dynamic section for "/lib/x86_64-linux-gnu/libc.so.6" is not at the expected address (wrong library or version mismatch?)
From                To                  Syms Read   Shared Object Library
0x00007ffffffd1180  0x00007fffffff3664  No          /lib64/ld-linux-x86-64.so.2
0x00007fffffe38988  0x00007ffffffad577  No          /lib/x86_64-linux-gnu/libc.so.6
```

# `r_debug`
- https://sourceware.org/pipermail/gdb/2000-April/004509.html
- https://webcache.googleusercontent.com/search?q=cache:pduhbeFGw3kJ:https://gbenson.net/%3Fp%3D407
- https://stackoverflow.com/questions/27256275/how-does-gdb-know-where-an-executable-has-been-relocated
- `_r_debug` initialization [glibc/elf/dl-debug.c](https://code.woboq.org/userspace/glibc/elf/dl-debug.c.html#_r_debug)
- `_r_debug` defintion [glibc/elf/link.h](https://code.woboq.org/userspace/glibc/elf/link.h.html#r_debug)

- gdb `DT_DEBUG` lookup https://github.com/bminor/binutils-gdb/blob/binutils-2_34-branch/gdb/solib-svr4.c#L798-L810