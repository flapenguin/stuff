.section .data
before_msg: .asciz "syscall(%ld) = "
after_msg: .asciz "%ld\n"

.section .text
.global main
main:
    push %rbp

    leaq before_msg(%rip), %rdi
    movq $4242, %rsi
    call printf

    movq $4242, %rax
    syscall

    leaq after_msg(%rip), %rdi
    movq %rax, %rsi
    call printf

    movq $0, %rax

    pop %rbp
    ret
