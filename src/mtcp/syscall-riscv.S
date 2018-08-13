#include "sysdep/sysdep-riscv.h"

ENTRY (__syscall_error)
        mv t0, ra
        /* Fall through to __syscall_set_errno.  */
END (__syscall_error)

ENTRY (__syscall_set_errno)
	/* We got here because a0 < 0, but only codes in the range [-4095, -1]
	  represent errors.  Otherwise, just return the result normally.  */
	li t1, -4096
	bleu a0, t1, 1f
	neg a0, a0
#if RTLD_PRIVATE_ERRNO
	sw a0, rtld_errno, t1
#elif defined(__PIC__)
	la.tls.ie t1, errno
	add t1, t1, tp
	sw a0, 0(t1)
#else
	lui t1, %tprel_hi(errno)
	add t1, t1, tp, %tprel_add(errno)
	sw a0, %tprel_lo(errno)(t1)
#endif
	li a0, -1
1:	jr t0
END (__syscall_set_errno)
