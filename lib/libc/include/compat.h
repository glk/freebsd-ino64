/*-
 * Copyright (c) 2009 Advanced Computing Technologies LLC
 * Written by: John H. Baldwin <jhb@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * This file defines compatiblity symbol versions for old system calls.  It
 * is included in all generated system call files.
 */

#ifndef __LIBC_COMPAT_H__
#define	__LIBC_COMPAT_H__

#define	__sym_compat(sym,impl,verid)	\
	.symver impl, sym@verid

__sym_compat(__semctl, freebsd7___semctl, FBSD_1.0);
__sym_compat(msgctl, freebsd7_msgctl, FBSD_1.0);
__sym_compat(shmctl, freebsd7_shmctl, FBSD_1.0);

__sym_compat(nfstat, freebsd9_nfstat, FBSD_1.0);
__sym_compat(nlstat, freebsd9_nlstat, FBSD_1.0);
__sym_compat(nstat, freebsd9_nstat, FBSD_1.0);

__sym_compat(fhstat, freebsd9_fhstat, FBSD_1.0);
__sym_compat(fstat, freebsd9_fstat, FBSD_1.0);
__sym_compat(fstatat, freebsd9_fstatat, FBSD_1.1);
__sym_compat(lstat, freebsd9_lstat, FBSD_1.0);
__sym_compat(stat, freebsd9_stat, FBSD_1.0);

__sym_compat(getdents, freebsd9_getdents, FBSD_1.0);
__sym_compat(getdirentries, freebsd9_getdirentries, FBSD_1.0);

__sym_compat(getfsstat, freebsd9_getfsstat, FBSD_1.0);
__sym_compat(fhstatfs, freebsd9_fhstatfs, FBSD_1.0);
__sym_compat(fstatfs, freebsd9_fstatfs, FBSD_1.0);
__sym_compat(statfs, freebsd9_statfs, FBSD_1.0);

__sym_compat(mknod, freebsd9_mknod, FBSD_1.0);
__sym_compat(mknodat, freebsd9_mknodat, FBSD_1.1);

#undef __sym_compat

#endif	/* __LIBC_COMPAT_H__ */

