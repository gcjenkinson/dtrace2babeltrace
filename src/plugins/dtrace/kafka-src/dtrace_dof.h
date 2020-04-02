/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Copyright (c) 2013, Joyent, Inc. All rights reserved.
 * Copyright (c) 2013 by Delphix. All rights reserved.
 */

#ifndef DTRACE_DOF_H
#define	DTRACE_DOF_H

#include <endian.h>

#define _BIG_ENDIAN __BIG_ENDIAN

typedef unsigned int uint_t;

#ifdef	__cplusplus
extern "C" {
#endif

#define IS_P2ALIGNED(v, a) ((((uintptr_t)(v)) & ((uintptr_t)(a) - 1)) == 0)

/*
 * DTrace Intermediate Format (DIF)
 *
 * The following definitions describe the DTrace Intermediate Format (DIF), a
 * a RISC-like instruction set and program encoding used to represent
 * predicates and actions that can be bound to DTrace probes.  The constants
 * below defining the number of available registers are suggested minimums; the
 * compiler should use DTRACEIOC_CONF to dynamically obtain the number of
 * registers provided by the current DTrace implementation.
 */
#define	DIF_VERSION_2	2		/* DIF version 2: Solaris 10 FCS */

/*
 * A DTrace Intermediate Format Type (DIF Type) is used to represent the types
 * of variables, function and associative array arguments, and the return type
 * for each DIF object (shown below).  It contains a description of the type,
 * its size in bytes, and a module identifier.
 */
typedef struct dtrace_diftype {
	uint8_t dtdt_kind;		/* type kind (see below) */
	uint8_t dtdt_ckind;		/* type kind in CTF */
	uint8_t dtdt_flags;		/* type flags (see below) */
	uint8_t dtdt_pad;		/* reserved for future use */
	uint32_t dtdt_size;		/* type size in bytes (unless string) */
} dtrace_diftype_t;

/*
 * A DTrace Intermediate Format variable record is used to describe each of the
 * variables referenced by a given DIF object.  It contains an integer variable
 * identifier along with variable scope and properties, as shown below.  The
 * size of this structure must be sizeof (int) aligned.
 */
typedef struct dtrace_difv {
	uint32_t dtdv_name;		/* variable name index in dtdo_strtab */
	uint32_t dtdv_id;		/* variable reference identifier */
	uint8_t dtdv_kind;		/* variable kind (see below) */
	uint8_t dtdv_scope;		/* variable scope (see below) */
	uint16_t dtdv_flags;		/* variable flags (see below) */
	dtrace_diftype_t dtdv_type;	/* variable type (see above) */
} dtrace_difv_t;

typedef uint32_t dif_instr_t;

/*
 * DTrace Actions
 *
 * The upper byte determines the class of the action; the low bytes determines
 * the specific action within that class.  The classes of actions are as
 * follows:
 *
 *   [ no class ]                  <= May record process- or kernel-related data
 *   DTRACEACT_PROC                <= Only records process-related data
 *   DTRACEACT_PROC_DESTRUCTIVE    <= Potentially destructive to processes
 *   DTRACEACT_KERNEL              <= Only records kernel-related data
 *   DTRACEACT_KERNEL_DESTRUCTIVE  <= Potentially destructive to the kernel
 *   DTRACEACT_SPECULATIVE         <= Speculation-related action
 *   DTRACEACT_AGGREGATION         <= Aggregating action
 */
#define	DTRACEACT_DIFEXPR		1	/* action is DIF expression */
#define	DTRACEACT_PRINTF		3	/* printf() action */

/*
 * DTrace Object Format (DOF)
 *
 * DTrace programs can be persistently encoded in the DOF format so that they
 * may be embedded in other programs (for example, in an ELF file) or in the
 * dtrace driver configuration file for use in anonymous tracing.  The DOF
 * format is versioned and extensible so that it can be revised and so that
 * internal data structures can be modified or extended compatibly.  All DOF
 * structures use fixed-size types, so the 32-bit and 64-bit representations
 * are identical and consumers can use either data model transparently.
 *
 * The file layout is structured as follows:
 *
 * +---------------+-------------------+----- ... ----+---- ... ------+
 * |   dof_hdr_t   |  dof_sec_t[ ... ] |   loadable   | non-loadable  |
 * | (file header) | (section headers) | section data | section data  |
 * +---------------+-------------------+----- ... ----+---- ... ------+
 * |<------------ dof_hdr.dofh_loadsz --------------->|               |
 * |<------------ dof_hdr.dofh_filesz ------------------------------->|
 *
 * The file header stores meta-data including a magic number, data model for
 * the instrumentation, data encoding, and properties of the DIF code within.
 * The header describes its own size and the size of the section headers.  By
 * convention, an array of section headers follows the file header, and then
 * the data for all loadable sections and unloadable sections.  This permits
 * consumer code to easily download the headers and all loadable data into the
 * DTrace driver in one contiguous chunk, omitting other extraneous sections.
 *
 * The section headers describe the size, offset, alignment, and section type
 * for each section.  Sections are described using a set of #defines that tell
 * the consumer what kind of data is expected.  Sections can contain links to
 * other sections by storing a dof_secidx_t, an index into the section header
 * array, inside of the section data structures.  The section header includes
 * an entry size so that sections with data arrays can grow their structures.
 *
 * The DOF data itself can contain many snippets of DIF (i.e. >1 DIFOs), which
 * are represented themselves as a collection of related DOF sections.  This
 * permits us to change the set of sections associated with a DIFO over time,
 * and also permits us to encode DIFOs that contain different sets of sections.
 * When a DOF section wants to refer to a DIFO, it stores the dof_secidx_t of a
 * section of type DOF_SECT_DIFOHDR.  This section's data is then an array of
 * dof_secidx_t's which in turn denote the sections associated with this DIFO.
 *
 * This loose coupling of the file structure (header and sections) to the
 * structure of the DTrace program itself (ECB descriptions, action
 * descriptions, and DIFOs) permits activities such as relocation processing
 * to occur in a single pass without having to understand D program structure.
 *
 * Finally, strings are always stored in ELF-style string tables along with a
 * string table section index and string table offset.  Therefore strings in
 * DOF are always arbitrary-length and not bound to the current implementation.
 */

#define	DOF_ID_SIZE	16	/* total size of dofh_ident[] in bytes */

typedef struct dof_hdr {
	uint8_t dofh_ident[DOF_ID_SIZE]; /* identification bytes (see below) */
	uint32_t dofh_flags;		/* file attribute flags (if any) */
	uint32_t dofh_hdrsize;		/* size of file header in bytes */
	uint32_t dofh_secsize;		/* size of section header in bytes */
	uint32_t dofh_secnum;		/* number of section headers */
	uint64_t dofh_secoff;		/* file offset of section headers */
	uint64_t dofh_loadsz;		/* file size of loadable portion */
	uint64_t dofh_filesz;		/* file size of entire DOF file */
	uint64_t dofh_pad;		/* reserved for future use */
} dof_hdr_t;

#define	DOF_ID_MAG0	0	/* first byte of magic number */
#define	DOF_ID_MAG1	1	/* second byte of magic number */
#define	DOF_ID_MAG2	2	/* third byte of magic number */
#define	DOF_ID_MAG3	3	/* fourth byte of magic number */
#define	DOF_ID_MODEL	4	/* DOF data model (see below) */
#define	DOF_ID_ENCODING	5	/* DOF data encoding (see below) */
#define	DOF_ID_VERSION	6	/* DOF file format major version (see below) */
#define	DOF_ID_DIFVERS	7	/* DIF instruction set version */
#define	DOF_ID_DIFIREG	8	/* DIF integer registers used by compiler */
#define	DOF_ID_DIFTREG	9	/* DIF tuple registers used by compiler */
#define	DOF_ID_PAD	10	/* start of padding bytes (all zeroes) */

#define	DOF_MAG_MAG0	0x7F	/* DOF_ID_MAG[0-3] */
#define	DOF_MAG_MAG1	'D'
#define	DOF_MAG_MAG2	'O'
#define	DOF_MAG_MAG3	'F'

#define	DOF_MAG_STRING	"\177DOF"
#define	DOF_MAG_STRLEN	4

#define	DOF_MODEL_NONE	0	/* DOF_ID_MODEL */
#define	DOF_MODEL_ILP32	1
#define	DOF_MODEL_LP64	2

#ifdef _LP64
#define	DOF_MODEL_NATIVE	DOF_MODEL_LP64
#else
#define	DOF_MODEL_NATIVE	DOF_MODEL_ILP32
#endif

#define	DOF_ENCODE_NONE	0	/* DOF_ID_ENCODING */
#define	DOF_ENCODE_LSB	1
#define	DOF_ENCODE_MSB	2

#if BYTE_ORDER == _BIG_ENDIAN
#define	DOF_ENCODE_NATIVE	DOF_ENCODE_MSB
#else
#define	DOF_ENCODE_NATIVE	DOF_ENCODE_LSB
#endif

#define	DOF_VERSION_1	1	/* DOF version 1: Solaris 10 FCS */
#define	DOF_VERSION_2	2	/* DOF version 2: Solaris Express 6/06 */
#define	DOF_VERSION	DOF_VERSION_2	/* Latest DOF version */

#define	DOF_FL_VALID	0	/* mask of all valid dofh_flags bits */

typedef uint32_t dof_secidx_t;	/* section header table index type */
typedef uint32_t dof_stridx_t;	/* string table index type */

#define	DOF_SECIDX_NONE	(-1U)	/* null value for section indices */
#define	DOF_STRIDX_NONE	(-1U)	/* null value for string indices */

typedef struct dof_sec {
	uint32_t dofs_type;	/* section type (see below) */
	uint32_t dofs_align;	/* section data memory alignment */
	uint32_t dofs_flags;	/* section flags (if any) */
	uint32_t dofs_entsize;	/* size of section entry (if table) */
	uint64_t dofs_offset;	/* offset of section data within file */
	uint64_t dofs_size;	/* size of section data in bytes */
} dof_sec_t;

#define	DOF_SECT_NONE		0	/* null section */
#define	DOF_SECT_COMMENTS	1	/* compiler comments */
#define	DOF_SECT_SOURCE		2	/* D program source code */
#define	DOF_SECT_ECBDESC	3	/* dof_ecbdesc_t */
#define	DOF_SECT_PROBEDESC	4	/* dof_probedesc_t */
#define	DOF_SECT_ACTDESC	5	/* dof_actdesc_t array */
#define	DOF_SECT_DIFOHDR	6	/* dof_difohdr_t (variable length) */
#define	DOF_SECT_DIF		7	/* uint32_t array of byte code */
#define	DOF_SECT_STRTAB		8	/* string table */
#define	DOF_SECT_VARTAB		9	/* dtrace_difv_t array */
#define	DOF_SECT_RELTAB		10	/* dof_relodesc_t array */
#define	DOF_SECT_TYPTAB		11	/* dtrace_diftype_t array */
#define	DOF_SECT_URELHDR	12	/* dof_relohdr_t (user relocations) */
#define	DOF_SECT_KRELHDR	13	/* dof_relohdr_t (kernel relocations) */
#define	DOF_SECT_OPTDESC	14	/* dof_optdesc_t array */
#define	DOF_SECT_PROVIDER	15	/* dof_provider_t */
#define	DOF_SECT_PROBES		16	/* dof_probe_t array */
#define	DOF_SECT_PRARGS		17	/* uint8_t array (probe arg mappings) */
#define	DOF_SECT_PROFFS		18	/* uint32_t array (probe arg offsets) */
#define	DOF_SECT_INTTAB		19	/* uint64_t array */
#define	DOF_SECT_UTSNAME	20	/* struct utsname */
#define	DOF_SECT_XLTAB		21	/* dof_xlref_t array */
#define	DOF_SECT_XLMEMBERS	22	/* dof_xlmember_t array */
#define	DOF_SECT_XLIMPORT	23	/* dof_xlator_t */
#define	DOF_SECT_XLEXPORT	24	/* dof_xlator_t */
#define	DOF_SECT_PREXPORT	25	/* dof_secidx_t array (exported objs) */
#define	DOF_SECT_PRENOFFS	26	/* uint32_t array (enabled offsets) */

#define	DOF_SECF_LOAD		1	/* section should be loaded */

#define	DOF_SEC_ISLOADABLE(x)						\
	(((x) == DOF_SECT_ECBDESC) || ((x) == DOF_SECT_PROBEDESC) ||	\
	((x) == DOF_SECT_ACTDESC) || ((x) == DOF_SECT_DIFOHDR) ||	\
	((x) == DOF_SECT_DIF) || ((x) == DOF_SECT_STRTAB) ||		\
	((x) == DOF_SECT_VARTAB) || ((x) == DOF_SECT_RELTAB) ||		\
	((x) == DOF_SECT_TYPTAB) || ((x) == DOF_SECT_URELHDR) ||	\
	((x) == DOF_SECT_KRELHDR) || ((x) == DOF_SECT_OPTDESC) ||	\
	((x) == DOF_SECT_PROVIDER) || ((x) == DOF_SECT_PROBES) ||	\
	((x) == DOF_SECT_PRARGS) || ((x) == DOF_SECT_PROFFS) ||		\
	((x) == DOF_SECT_INTTAB) || ((x) == DOF_SECT_XLTAB) ||		\
	((x) == DOF_SECT_XLMEMBERS) || ((x) == DOF_SECT_XLIMPORT) ||	\
	((x) == DOF_SECT_XLEXPORT) ||  ((x) == DOF_SECT_PREXPORT) || 	\
	((x) == DOF_SECT_PRENOFFS))

typedef struct dof_ecbdesc {
	dof_secidx_t dofe_probes;	/* link to DOF_SECT_PROBEDESC */
	dof_secidx_t dofe_pred;		/* link to DOF_SECT_DIFOHDR */
	dof_secidx_t dofe_actions;	/* link to DOF_SECT_ACTDESC */
	uint32_t dofe_pad;		/* reserved for future use */
	uint64_t dofe_uarg;		/* user-supplied library argument */
} dof_ecbdesc_t;

typedef struct dof_probedesc {
	dof_secidx_t dofp_strtab;	/* link to DOF_SECT_STRTAB section */
	dof_stridx_t dofp_provider;	/* provider string */
	dof_stridx_t dofp_mod;		/* module string */
	dof_stridx_t dofp_func;		/* function string */
	dof_stridx_t dofp_name;		/* name string */
	uint32_t dofp_id;		/* probe identifier (or zero) */
} dof_probedesc_t;

typedef struct dof_actdesc {
	dof_secidx_t dofa_difo;		/* link to DOF_SECT_DIFOHDR */
	dof_secidx_t dofa_strtab;	/* link to DOF_SECT_STRTAB section */
	uint32_t dofa_kind;		/* action kind (DTRACEACT_* constant) */
	uint32_t dofa_ntuple;		/* number of subsequent tuple actions */
	uint64_t dofa_arg;		/* kind-specific argument */
	uint64_t dofa_uarg;		/* user-supplied argument */
} dof_actdesc_t;

typedef struct dof_difohdr {
	dtrace_diftype_t dofd_rtype;	/* return type for this fragment */
	dof_secidx_t dofd_links[1];	/* variable length array of indices */
} dof_difohdr_t;

typedef struct dof_relohdr {
	dof_secidx_t dofr_strtab;	/* link to DOF_SECT_STRTAB for names */
	dof_secidx_t dofr_relsec;	/* link to DOF_SECT_RELTAB for relos */
	dof_secidx_t dofr_tgtsec;	/* link to section we are relocating */
} dof_relohdr_t;

typedef struct dof_relodesc {
	dof_stridx_t dofr_name;		/* string name of relocation symbol */
	uint32_t dofr_type;		/* relo type (DOF_RELO_* constant) */
	uint64_t dofr_offset;		/* byte offset for relocation */
	uint64_t dofr_data;		/* additional type-specific data */
} dof_relodesc_t;

#define	DOF_RELO_NONE	0		/* empty relocation entry */
#define	DOF_RELO_SETX	1		/* relocate setx value */
#define	DOF_RELO_DOFREL	2		/* relocate DOF-relative value */

typedef struct dof_optdesc {
	uint32_t dofo_option;		/* option identifier */
	dof_secidx_t dofo_strtab;	/* string table, if string option */
	uint64_t dofo_value;		/* option value or string index */
} dof_optdesc_t;

typedef uint32_t dof_attr_t;		/* encoded stability attributes */

#define	DOF_ATTR(n, d, c)	(((n) << 24) | ((d) << 16) | ((c) << 8))
#define	DOF_ATTR_NAME(a)	(((a) >> 24) & 0xff)
#define	DOF_ATTR_DATA(a)	(((a) >> 16) & 0xff)
#define	DOF_ATTR_CLASS(a)	(((a) >>  8) & 0xff)

typedef struct dof_provider {
	dof_secidx_t dofpv_strtab;	/* link to DOF_SECT_STRTAB section */
	dof_secidx_t dofpv_probes;	/* link to DOF_SECT_PROBES section */
	dof_secidx_t dofpv_prargs;	/* link to DOF_SECT_PRARGS section */
	dof_secidx_t dofpv_proffs;	/* link to DOF_SECT_PROFFS section */
	dof_stridx_t dofpv_name;	/* provider name string */
	dof_attr_t dofpv_provattr;	/* provider attributes */
	dof_attr_t dofpv_modattr;	/* module attributes */
	dof_attr_t dofpv_funcattr;	/* function attributes */
	dof_attr_t dofpv_nameattr;	/* name attributes */
	dof_attr_t dofpv_argsattr;	/* args attributes */
	dof_secidx_t dofpv_prenoffs;	/* link to DOF_SECT_PRENOFFS section */
} dof_provider_t;

typedef struct dof_probe {
	uint64_t dofpr_addr;		/* probe base address or offset */
	dof_stridx_t dofpr_func;	/* probe function string */
	dof_stridx_t dofpr_name;	/* probe name string */
	dof_stridx_t dofpr_nargv;	/* native argument type strings */
	dof_stridx_t dofpr_xargv;	/* translated argument type strings */
	uint32_t dofpr_argidx;		/* index of first argument mapping */
	uint32_t dofpr_offidx;		/* index of first offset entry */
	uint8_t dofpr_nargc;		/* native argument count */
	uint8_t dofpr_xargc;		/* translated argument count */
	uint16_t dofpr_noffs;		/* number of offset entries for probe */
	uint32_t dofpr_enoffidx;	/* index of first is-enabled offset */
	uint16_t dofpr_nenoffs;		/* number of is-enabled offsets */
	uint16_t dofpr_pad1;		/* reserved for future use */
	uint32_t dofpr_pad2;		/* reserved for future use */
} dof_probe_t;

typedef struct dof_xlator {
	dof_secidx_t dofxl_members;	/* link to DOF_SECT_XLMEMBERS section */
	dof_secidx_t dofxl_strtab;	/* link to DOF_SECT_STRTAB section */
	dof_stridx_t dofxl_argv;	/* input parameter type strings */
	uint32_t dofxl_argc;		/* input parameter list length */
	dof_stridx_t dofxl_type;	/* output type string name */
	dof_attr_t dofxl_attr;		/* output stability attributes */
} dof_xlator_t;

typedef struct dof_xlmember {
	dof_secidx_t dofxm_difo;	/* member link to DOF_SECT_DIFOHDR */
	dof_stridx_t dofxm_name;	/* member name */
	dtrace_diftype_t dofxm_type;	/* member type */
} dof_xlmember_t;

typedef struct dof_xlref {
	dof_secidx_t dofxr_xlator;	/* link to DOF_SECT_XLATORS section */
	uint32_t dofxr_member;		/* index of referenced dof_xlmember */
	uint32_t dofxr_argn;		/* index of argument for DIF_OP_XLARG */
} dof_xlref_t;

/*
 * DTrace Intermediate Format Object (DIFO)
 *
 * A DIFO is used to store the compiled DIF for a D expression, its return
 * type, and its string and variable tables.  The string table is a single
 * buffer of character data into which sets instructions and variable
 * references can reference strings using a byte offset.  The variable table
 * is an array of dtrace_difv_t structures that describe the name and type of
 * each variable and the id used in the DIF code.  This structure is described
 * above in the DIF section of this header file.  The DIFO is used at both
 * user-level (in the library) and in the kernel, but the structure is never
 * passed between the two: the DOF structures form the only interface.  As a
 * result, the definition can change depending on the presence of _KERNEL.
 */
typedef struct dtrace_difo {
	dif_instr_t *dtdo_buf;		/* instruction buffer */
	uint64_t *dtdo_inttab;		/* integer table (optional) */
	char *dtdo_strtab;		/* string table (optional) */
	dtrace_difv_t *dtdo_vartab;	/* variable table (optional) */
	uint_t dtdo_len;		/* length of instruction buffer */
	uint_t dtdo_intlen;		/* length of integer table */
	uint_t dtdo_strlen;		/* length of string table */
	uint_t dtdo_varlen;		/* length of variable table */
	dtrace_diftype_t dtdo_rtype;	/* return type */
	uint_t dtdo_refcnt;		/* owner reference count */
	uint_t dtdo_destructive;	/* invokes destructive subroutines */
#ifndef _KERNEL
	dof_relodesc_t *dtdo_kreltab;	/* kernel relocations */
	dof_relodesc_t *dtdo_ureltab;	/* user relocations */
	struct dt_node **dtdo_xlmtab;	/* translator references */
	uint_t dtdo_krelen;		/* length of krelo table */
	uint_t dtdo_urelen;		/* length of urelo table */
	uint_t dtdo_xlmlen;		/* length of translator table */
#endif
} dtrace_difo_t;


#ifdef	__cplusplus
}
#endif

#endif	/* DTRACE_DOF_H */
