#ifndef TLSUTIL_H
#define TLSUTIL_H

/* These functions are not defined for x86_64. */
#ifdef __i386__
# define tls_get_thread_area(arg, myinfo_gs) \
  mtcp_sys_get_thread_area(arg)
# define tls_set_thread_area(arg, myinfo_gs) \
  mtcp_sys_set_thread_area(arg)
#endif // ifdef __i386__

#ifdef __x86_64__
# include <asm/prctl.h>
# include <sys/prctl.h>

/* man arch_prctl has both signatures, and prctl.h above has no declaration.
 *  int arch_prctl(int code, unsigned long addr);
 *  int arch_prctl(int code, unsigned long addr);
 */

int arch_prctl();
# if 1

// These calls need to be made from both DMTCP and mtcp_restart

/* ARE THE _GS OPERATIONS NECESSARY? */
#  define tls_get_thread_area(uinfo, myinfo_gs)                            \
  (mtcp_inline_syscall(arch_prctl, 2, ARCH_GET_FS,                         \
                       (unsigned long int)(&(((struct user_desc *)uinfo)-> \
                                             base_addr))),                 \
   mtcp_inline_syscall(arch_prctl, 2, ARCH_GET_GS, &myinfo_gs)             \
  )
#  define tls_set_thread_area(uinfo, myinfo_gs)                              \
  (mtcp_inline_syscall(arch_prctl, 2, ARCH_SET_FS,                           \
                       *(unsigned long int *)&(((struct user_desc *)uinfo)-> \
                                               base_addr)),                  \
   mtcp_inline_syscall(arch_prctl, 2, ARCH_SET_GS, myinfo_gs)                \
  )
# else // if 1

/* ARE THE _GS OPERATIONS NECESSARY? */
#  define tls_get_thread_area(uinfo, myinfo_gs)                                \
  (arch_prctl(ARCH_GET_FS,                                                     \
              (unsigned long int)(&(((struct user_desc *)uinfo)->base_addr))), \
   arch_prctl(ARCH_GET_GS, &myinfo_gs)                                         \
  )
#  define tls_set_thread_area(uinfo, myinfo_gs)                                 \
  (arch_prctl(ARCH_SET_FS,                                                      \
              *(unsigned long int *)&(((struct user_desc *)uinfo)->base_addr)), \
   arch_prctl(ARCH_SET_GS, myinfo_gs)                                           \
  )
# endif // if 1
#endif /* end __x86_64__ */

#ifdef __arm__

/* This allocation hack will work only if calls to mtcp_sys_get_thread_area
 * and mtcp_sys_get_thread_area are both inside the same file (mtcp.c).
 * This is all because get_thread_area is not implemented for arm.
 *     For ARM, the thread pointer seems to point to the next slot
 * after the 'struct pthread'.  Why??  So, we subtract that address.
 * After that, tid/pid will be located at  offset 104/108 as expected
 * for glibc-2.13.
 * NOTE:  'struct pthread' defined in glibc/nptl/descr.h
 *     The value below (1216) is current for glibc-2.13.
 *     May have to update 'sizeof(struct pthread)' for new versions of glibc.
 *     We can automate this by searching for negative offset from end
 *     of 'struct pthread' in tls_tid_offset, tls_pid_offset in mtcp.c.
 */

# define tls_get_thread_area(uinfo, myinfo_gs)                          \
  ({ asm volatile ("mrc     p15, 0, %0, c13, c0, 3  @ load_tp_hard\n\t" \
                   : "=r" (myinfo_gs));                                 \
     myinfo_gs = myinfo_gs - 1216; /* sizeof(struct pthread) = 1216 */  \
     *(unsigned long int *)&(((struct user_desc *)uinfo)->base_addr)    \
       = myinfo_gs;                                                     \
     myinfo_gs; })
# define tls_set_thread_area(uinfo, myinfo_gs)                        \
  (myinfo_gs =                                                        \
     *(unsigned long int *)&(((struct user_desc *)uinfo)->base_addr), \
   (mtcp_sys_kernel_set_tls(myinfo_gs + 1216), 0)                     \
   /* 0 return value at end means success */)
#endif /* end __arm__ */

#ifdef __aarch64__

/* This allocation hack will work only if calls to mtcp_sys_get_thread_area
 * and mtcp_sys_get_thread_area are both inside the same file (mtcp.c).
 * This is all because get_thread_area is not implemented for aarch64.
 *     For ARM, the thread pointer seems to point to the next slot
 * after the 'struct pthread'.  Why??  So, we subtract that address.
 * After that, tid/pid will be located at offset 208/212 as expected
 * for glibc-2.17.
 * NOTE:  'struct pthread' defined in glibc/nptl/descr.h
 *     The value below (1776) is current for glibc-2.17.
 #     See PORTING file for easy way to compute these numbers.
 *     May have to update 'sizeof(struct pthread)' for new versions of glibc.
 *     We can automate this by searching for negative offset from end
 *     of 'struct pthread' in tls_tid_offset, tls_pid_offset in mtcp.c.
 */

/* NOTE:  We want 'sizeof(myinfo_gs) == sizeof(unsigned long int)' always. */
# define tls_get_thread_area(uinfo, myinfo_gs)                         \
  ({ asm volatile ("mrs   %0, tpidr_el0"                               \
                   : "=r" (myinfo_gs));                                \
     myinfo_gs = myinfo_gs - 1776; /* sizeof(struct pthread) = 1776 */ \
     *(unsigned long int *)&(((struct user_desc *)uinfo)->base_addr)   \
       = myinfo_gs;                                                    \
     myinfo_gs; })
# define tls_set_thread_area(uinfo, myinfo_gs)                          \
  ({ myinfo_gs =                                                        \
       *(unsigned long int *)&(((struct user_desc *)uinfo)->base_addr); \
     myinfo_gs = myinfo_gs + 1776;                                      \
     asm volatile ("msr     tpidr_el0, %[gs]" : :[gs] "r" (myinfo_gs)); \
     0;  })
#endif /* end __aarch64__ */

#ifdef __riscv

// Taken from glibc-2.27.9000/sysdeps/riscv/nptl/tls.h

register void *__thread_self asm ("tp");
# define READ_THREAD_POINTER() ({ __thread_self; })

/* The thread pointer tp points to the end of the TCB.
   The pthread_descr structure is immediately in front of the TCB.  */
# define TLS_TCB_OFFSET	0

/* Alignment requirements for the TCB.  */
# define TLS_TCB_ALIGN          1776 // __alignof__ (struct pthread)

/* This is the size of the TCB.  Because our TCB is before the thread
   pointer, we don't need this.  */
# define TLS_TCB_SIZE		0

/* This is the size we need before TCB - actually, it includes the TCB.  */
# define TLS_PRE_TCB_SIZE \
  (1776						      \
   + ((16 + TLS_TCB_ALIGN - 1) & ~(TLS_TCB_ALIGN - 1))) // sizeof (tcbhead_t) == 16

/* Return the thread descriptor for the current thread.  */
# define THREAD_SELF \
 ((READ_THREAD_POINTER ()			     \
		      - TLS_TCB_OFFSET - TLS_PRE_TCB_SIZE))

# define tls_get_thread_area(uinfo, myinfo_gs)                         \
  ({ myinfo_gs = READ_THREAD_POINTER();                                \
     myinfo_gs = THREAD_SELF;                                          \
     *(unsigned long int *)&(((struct user_desc *)uinfo)->base_addr)   \
       = myinfo_gs;                                                    \
     myinfo_gs; })
# define tls_set_thread_area(uinfo, myinfo_gs)                          \
  ({ myinfo_gs =                                                        \
       *(unsigned long int *)&(((struct user_desc *)uinfo)->base_addr); \
     myinfo_gs = myinfo_gs + TLS_TCB_OFFSET;                            \
     __thread_self = myinfo_gs;                                         \
     0;  })
#endif // ifdef __riscv

#endif /* TLSUTIL_H */
