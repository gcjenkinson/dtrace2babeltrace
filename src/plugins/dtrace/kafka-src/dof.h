/*-
 * Copyright (c) 2019-2020 (Graeme Jenkinson)
 * All rights reserved.
 *
 * This software was developed by BAE Systems, the University of Cambridge
 * Computer Laboratory, and Memorial University under DARPA/AFRL contract
 * FA8650-15-C-7558 ("CADETS"), as part of the DARPA Transparent Computing
 * (TC) research program.
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
 */

#ifndef _LIBDOF_H
#define _LIBDOF_H

#include "dtrace_dof.h"
#include "bbuf.h"

struct dof;

extern int dof_new_buf(struct dof **, char *, size_t);
extern void dof_destroy(struct dof *);

extern int dof_load_actdesc(struct dof *, dof_hdr_t *, dof_sec_t *, dof_actdesc_t *);
extern int dof_load_difohdr(struct dof *, dof_hdr_t *, dof_sec_t *, dtrace_difo_t *);
extern int dof_load_ecbdesc(struct dof *, dof_hdr_t *, dof_sec_t *, dof_ecbdesc_t *);
extern int dof_load_header(struct dof *, dof_hdr_t *);
extern int dof_load_sect(struct dof *, dof_hdr_t *, uint32_t, dof_sec_t *);
extern int dof_load_sect_hex(struct dof *, dof_sec_t *, struct bbuf **);
extern int dof_load_sect_hex_into(struct dof *, dof_sec_t *, struct bbuf *);
extern int dof_load_probedesc(struct dof *, dof_hdr_t *, dof_sec_t *, dof_probedesc_t *);

#endif
