#define _POSIX_C_SOURCE 200112L
#define _GNU_SOURCE

/* C standard library */
#include <errno.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/* POSIX */
#include <unistd.h>
#include <sys/user.h>
#include <sys/syscall.h>
#include <sys/wait.h>

/* Linux */
#include <sys/ptrace.h>

#define ptrace_or_die(...) do { \
        if (ptrace(__VA_ARGS__) == -1) { \
            perror(":: ptrace"); \
            printf("::\t@%s  %s:%d\n", __FILE__, __func__, __LINE__); \
            exit(1); \
        } \
    } while (0)

#define MAGIC_SYSCALL 4242ul

int main(int argc, char** argv) {
    pid_t pid = fork();

    /* child */
    if (!pid) {
        ptrace_or_die(PTRACE_TRACEME, 0, 0, 0);
        static char* args[] = {"123", 0};
        static char* env[] = {0};
        execve(argv[1], args, env);
        perror("execve");
        return 1;
    }

    /* Wait for PTRACE_TRACEME in child. */
    waitpid(pid, 0, 0);

    /* Automatically SIGKILL tracee, if tracer dies. */
    ptrace_or_die(PTRACE_SETOPTIONS, pid, 0, PTRACE_O_EXITKILL);

    for (;;) {
        /* Resume tracee and wait for the next system call. */
        ptrace_or_die(PTRACE_SYSCALL, pid, 0, 0);
        waitpid(pid, 0, 0);

        /* Gather system call arguments. */
        struct user_regs_struct regs;
        ptrace_or_die(PTRACE_GETREGS, pid, 0, &regs);

        /* Gather syscall number. */
        const uint64_t syscall_number = regs.orig_rax;

        /* Patch syscall to mke invalid syscall, i.e. nop. */
        if (syscall_number == MAGIC_SYSCALL) {
            regs.orig_rax = -1;
            ptrace_or_die(PTRACE_SETREGS, pid, 0, &regs);
        }

        /* Pass syscall to the kernel and wait for result. */
        ptrace_or_die(PTRACE_SYSCALL, pid, 0, 0);
        waitpid(pid, 0, 0);

        /* At this point process may be already dead. */
        if (syscall_number == SYS_exit || syscall_number == SYS_exit_group) {
            printf(":: tracee exited\n");
            return 0;
        }

        /* Update tracee's registers. */
        if (syscall_number == MAGIC_SYSCALL) {
            int64_t result;
            printf(":: patching syscall %ld return value from %lld to ... ", MAGIC_SYSCALL, regs.orig_rax);
            scanf("%lu", &result);

            ptrace_or_die(PTRACE_GETREGS, pid, 0, &regs);
            regs.rax = result;
            ptrace_or_die(PTRACE_SETREGS, pid, 0, &regs);
        }
    }

    return 0;
}
