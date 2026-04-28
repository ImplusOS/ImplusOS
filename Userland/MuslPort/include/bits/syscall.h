#ifndef _BITS_SYSCALL_H
#define _BITS_SYSCALL_H

/*
 * ImplusOS syscall mapping for a minimal musl static port on x86_64.
 * Values match Kernel/Syscall/Syscall_Main.h.
 */
#define SYS_read        24
#define SYS_write       25
#define SYS_open        23
#define SYS_close       26
#define SYS_lseek       34
#define SYS_fcntl       900
#define SYS_getpid      47
#define SYS_getppid     111
#define SYS_exit        8
#define SYS_exit_group  8
#define SYS_sched_yield 7
#define SYS_nanosleep   120

#endif
