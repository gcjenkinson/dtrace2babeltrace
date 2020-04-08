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

#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "dtrace_dof.h"
#include "dof.h"

struct dof {
	FILE *dof_fp;
};

static bool validate_header(dof_hdr_t *);

static bool
validate_header(dof_hdr_t *hdr)
{
	uint64_t sec_len;

	/* Validate the DOF file header. */
	if (hdr->dofh_ident[DOF_ID_MAG0] != DOF_MAG_MAG0 ||
	    hdr->dofh_ident[DOF_ID_MAG1] != DOF_MAG_MAG1 ||
	    hdr->dofh_ident[DOF_ID_MAG2] != DOF_MAG_MAG2 ||
	    hdr->dofh_ident[DOF_ID_MAG3] != DOF_MAG_MAG3) {

		fprintf(stderr, "DOF file magic invalid\n");
		return false;
	} 
	
	if (hdr->dofh_ident[DOF_ID_MODEL] != DOF_MODEL_ILP32 &&
	    hdr->dofh_ident[DOF_ID_MODEL] != DOF_MODEL_LP64) {

		fprintf(stderr, "DOF model invalid\n");
		return false;
	}

	if (hdr->dofh_ident[DOF_ID_ENCODING] != DOF_ENCODE_LSB &&
	    hdr->dofh_ident[DOF_ID_ENCODING] != DOF_ENCODE_MSB) {

		fprintf(stderr, "DOF encoding invalid\n");
		return false;
	}

	if (hdr->dofh_ident[DOF_ID_VERSION] != DOF_VERSION_1 &&
	    hdr->dofh_ident[DOF_ID_VERSION] != DOF_VERSION_2) {

		fprintf(stderr, "DOF version invalid\n");
		return false;
	}

	if (hdr->dofh_ident[DOF_ID_DIFVERS] != DIF_VERSION_2) {

		fprintf(stderr, "DIF version invalid\n");
		return false;
	}

	for (int i = DOF_ID_PAD; i < DOF_ID_SIZE; i++) {

		if (hdr->dofh_ident[i] != 0) {

			fprintf(stderr, "DOF padding invalid\n");
			return false;
		}
	}

	if (hdr->dofh_flags & ~DOF_FL_VALID) {

		fprintf(stderr, "DOF flag invalid\n");
		return false;
	}

	if (hdr->dofh_secsize == 0) {

		fprintf(stderr, "DOF secsize invalid\n");
		return false;
	}

	sec_len = (uint64_t) hdr->dofh_secnum * (uint64_t) hdr->dofh_secsize;
	if (hdr->dofh_secoff > hdr->dofh_loadsz ||
	    sec_len > hdr->dofh_loadsz ||
	    hdr->dofh_secoff + sec_len > hdr->dofh_loadsz) {

		return false;
	}

	if (!IS_P2ALIGNED(hdr->dofh_secoff, sizeof(uint64_t))) {

		fprintf(stderr, "DOF secoff improperly aligned\n");
		return false;
	}

	if (!IS_P2ALIGNED(hdr->dofh_secsize, sizeof(uint64_t))) {

		fprintf(stderr, "DOF secsize improperly aligned\n");
		return false;
	}

	return true;
}

int
dof_load_header(struct dof *dof, dof_hdr_t *hdr)
{
	ssize_t bytes;

	/* Copy the byes read from the file to the header structure. */
	bytes = fread(hdr, sizeof(unsigned char), sizeof(dof_hdr_t), dof->dof_fp);
	if (bytes != sizeof(dof_hdr_t)) {
			
		return -1;
	}

	/* Validate the DOF header */
	if (validate_header(hdr)) {

		return 0;
	}

	return -1;
}

int
dof_new_buf(struct dof **self, char *buf, size_t len)
{
	struct dof *dof;

	assert(buf != NULL);

	dof = malloc(sizeof(struct dof));
	if (dof == NULL) {

		fprintf(stderr, "Failed allocating dof\n");
		return -1;
	}

	dof->dof_fp = fmemopen(buf, len, "rb");
	if (dof->dof_fp == NULL)
		goto error;


	*self = dof;
	return 0;
error:
	*self = NULL;
	free(dof);
	return -1;
}

void
dof_destroy(struct dof *dof)
{
	assert(dof != NULL);

	fclose(dof->dof_fp);
	free(dof);
}

int
dof_load_actdesc(struct dof *dof, dof_hdr_t *hdr, dof_sec_t *sec,
    dof_actdesc_t *actdesc)
{
	struct bbuf *buf;
	off_t off;
	size_t bytes;
	unsigned char *data;
	int rc;

	off = fseek(dof->dof_fp, sec->dofs_offset, SEEK_SET);
	if (off == -1) {

		return -1;
	}

	if (hdr->dofh_ident[DOF_ID_ENCODING] == DOF_ENCODE_LSB) {
	
		rc = bbuf_new(&buf, NULL, sec->dofs_size, BBUF_LITTLEENDIAN);
	} else {

		rc = bbuf_new(&buf, NULL, sec->dofs_size, BBUF_BIGENDIAN);
	}

	data = bbuf_data(buf);

	/* Load the section. */ 
	bytes = fread(data, sizeof(unsigned char), sec->dofs_size, dof->dof_fp);
	if (bytes != sec->dofs_size) {

		return -1;
	}

	rc = bbuf_get_uint32(buf, &actdesc->dofa_difo);
	rc |= bbuf_get_uint32(buf, &actdesc->dofa_strtab);
	rc |= bbuf_get_uint32(buf, &actdesc->dofa_kind);
	rc |= bbuf_get_uint32(buf, &actdesc->dofa_ntuple);
	rc |= bbuf_get_uint64(buf, &actdesc->dofa_arg);
	rc |= bbuf_get_uint64(buf, &actdesc->dofa_uarg);
	if (rc != 0) {

		bbuf_delete(buf);
		return -1;
	}

	bbuf_delete(buf);
	return 0;
}

int
dof_load_difohdr(struct dof *dof, dof_hdr_t *hdr, dof_sec_t *sec, 
    dtrace_difo_t *difo)
{
	struct bbuf *buf;
	dof_secidx_t secidx;
	off_t off;
	size_t bytes;
	unsigned char *data;
	int rc;

	if (hdr->dofh_ident[DOF_ID_ENCODING] == DOF_ENCODE_LSB) {
	
		rc = bbuf_new(&buf, NULL, sec->dofs_size, BBUF_LITTLEENDIAN);
	} else {

		rc = bbuf_new(&buf, NULL, sec->dofs_size, BBUF_BIGENDIAN);
	}

	data = bbuf_data(buf);

	/* Load the section ASCII->binary. */ 
	off = fseek(dof->dof_fp, sec->dofs_offset, SEEK_SET);
	if (off == -1) {

		return -1;
	}
	
	bytes = fread(data, sizeof(unsigned char), sec->dofs_size, dof->dof_fp);
	if (bytes != sec->dofs_size) {

		return -1;
	}

	memset(difo, 0, sizeof(dtrace_difo_t));

	rc = bbuf_get_uint8(buf, &difo->dtdo_rtype.dtdt_kind);
	rc |= bbuf_get_uint8(buf, &difo->dtdo_rtype.dtdt_ckind);
	rc |= bbuf_get_uint8(buf, &difo->dtdo_rtype.dtdt_flags);
	rc |= bbuf_get_uint8(buf, &difo->dtdo_rtype.dtdt_pad);
	rc |= bbuf_get_uint32(buf, &difo->dtdo_rtype.dtdt_size);
	
	while (bbuf_get_uint32(buf, &secidx) == 0) {

		dof_sec_t tmp_sec;
			
		rc = dof_load_sect(dof, hdr, secidx + 1, &tmp_sec);
		if (rc != 0) {

			fprintf(stderr, "Failed loading DOF section %u\n", secidx + 1);
			if (difo->dtdo_buf != NULL) {

				free(difo->dtdo_buf);
			}
			if (difo->dtdo_strtab != NULL) {

				free(difo->dtdo_strtab);
			}
			bbuf_delete(buf);
			break;
		}
		
		switch (tmp_sec.dofs_type) {
		case DOF_SECT_DIF: {

			struct bbuf *dif_buf;
			dif_instr_t *dtdo_buf;

			dtdo_buf = (dif_instr_t *) malloc(tmp_sec.dofs_size);

			rc = bbuf_new(&dif_buf, (unsigned char *) dtdo_buf,
			    tmp_sec.dofs_size, BBUF_LITTLEENDIAN);
			if (rc != 0) {

				if (difo->dtdo_strtab != NULL) {

					free(difo->dtdo_strtab);
				}
				bbuf_delete(dif_buf);
				free(dtdo_buf);
				bbuf_delete(buf);
				return -1;
			}

			rc = dof_load_sect_hex_into(dof, &tmp_sec, dif_buf);
			if (rc != 0) {

				if (difo->dtdo_strtab != NULL) {

					free(difo->dtdo_strtab);
				}
				bbuf_delete(dif_buf);
				free(dtdo_buf);
				bbuf_delete(buf);
				return -1;
			}
			bbuf_delete(dif_buf);

			difo->dtdo_buf = dtdo_buf;
			difo->dtdo_len = tmp_sec.dofs_size / sizeof(dif_instr_t);
			break;
		}
		case DOF_SECT_STRTAB: {

			struct bbuf *strtab_buf;
			unsigned char *dtdo_buf;

			dtdo_buf = (unsigned char *) malloc(tmp_sec.dofs_size);

			rc = bbuf_new(&strtab_buf, dtdo_buf, tmp_sec.dofs_size,
			    BBUF_LITTLEENDIAN);
			if (rc != 0) {

				if (difo->dtdo_buf != NULL) {

					free(difo->dtdo_buf);
				}
				bbuf_delete(strtab_buf);
				free(dtdo_buf);
				bbuf_delete(buf);
				return -1;
			}

			rc = dof_load_sect_hex_into(dof, &tmp_sec, strtab_buf);
			if (rc != 0) {

				if (difo->dtdo_buf != NULL) {

					free(difo->dtdo_buf);
				}
				bbuf_delete(strtab_buf);
				free(dtdo_buf);
				bbuf_delete(buf);
				return -1;
			}

			difo->dtdo_strtab = (char *) dtdo_buf;
			difo->dtdo_strlen = tmp_sec.dofs_size;
			break;
		}
		default:
			/* Unhandled DIFOHDR section */
			break;
		}	
	}	

	bbuf_delete(buf);
	return 0;
}

int
dof_load_ecbdesc(struct dof *dof, dof_hdr_t *hdr, dof_sec_t *sec,
    dof_ecbdesc_t *ecbdesc)
{
	struct bbuf *buf;
	off_t off;
	size_t bytes;
	unsigned char *data;
	int rc;

	off = fseek(dof->dof_fp, sec->dofs_offset, SEEK_SET);
	if (off == -1) {

		return -1;
	}
		
	if (hdr->dofh_ident[DOF_ID_ENCODING] == DOF_ENCODE_LSB) {
	
		rc = bbuf_new(&buf, NULL, sec->dofs_size, BBUF_LITTLEENDIAN);
	} else {

		rc = bbuf_new(&buf, NULL, sec->dofs_size, BBUF_BIGENDIAN);
	}

	data = bbuf_data(buf);

	/* Load the section. */ 
	bytes = fread(data, sizeof(unsigned char), sec->dofs_size, dof->dof_fp);
	if (bytes != sec->dofs_size) {

		return -1;
	}

	rc = bbuf_get_uint32(buf, &ecbdesc->dofe_probes);
	rc |= bbuf_get_uint32(buf, &ecbdesc->dofe_pred);
	rc |= bbuf_get_uint32(buf, &ecbdesc->dofe_actions);
	rc |= bbuf_get_uint32(buf, &ecbdesc->dofe_pad);
	rc |= bbuf_get_uint64(buf, &ecbdesc->dofe_uarg);
	if (rc != 0) {

		bbuf_delete(buf);
		return -1;
	}

	bbuf_delete(buf);
	return 0;
}
	
int
dof_load_sect(struct dof *dof, dof_hdr_t *hdr, uint32_t sec_num, dof_sec_t *sec)
{
	struct bbuf *buf;
	off_t off;
	ssize_t bytes;
	uint32_t sec_idx = sec_num - 1;
	int rc;
	uint8_t raw_sec[sizeof(dof_sec_t)];
	
	off = fseek(dof->dof_fp,
	    hdr->dofh_secoff + (sec_idx * sizeof(dof_sec_t)), SEEK_SET);
	if (off == -1) {

		return -1;
	}

	/* Load the section header */ 
	bytes = fread((void *) raw_sec, sizeof(unsigned char),
	    sizeof(dof_sec_t), dof->dof_fp);
	if (bytes != sizeof(dof_sec_t)) {

		return -1;
	}

	/* Construct a dof_sec_t respecting DOF endianness. */
	if (hdr->dofh_ident[DOF_ID_ENCODING] == DOF_ENCODE_LSB) {
	
		rc = bbuf_new(&buf, raw_sec, sizeof(dof_sec_t),
		    BBUF_LITTLEENDIAN);
	} else {
	
		bbuf_new(&buf, raw_sec, sizeof(dof_sec_t), BBUF_BIGENDIAN);
	}

	rc = bbuf_get_uint32(buf, &sec->dofs_type);
	rc |= bbuf_get_uint32(buf, &sec->dofs_align);
	rc |= bbuf_get_uint32(buf, &sec->dofs_flags);
	rc |= bbuf_get_uint32(buf, &sec->dofs_entsize);
	rc |= bbuf_get_uint64(buf, &sec->dofs_offset);
	rc |= bbuf_get_uint64(buf, &sec->dofs_size);
	bbuf_delete(buf);

	return rc;
}

int
dof_load_sect_hex(struct dof *dof, dof_sec_t *sec, struct bbuf **buf)
{
	int rc;

	rc = bbuf_new(buf, NULL, sec->dofs_size, BBUF_LITTLEENDIAN);
	if (rc == 0) {

		return dof_load_sect_hex_into(dof, sec, *buf);
	}
		
	return rc;
}

int
dof_load_sect_hex_into(struct dof *dof, dof_sec_t *sec, struct bbuf *buf)
{
	off_t off;
	ssize_t bytes;
	char *strtab;

	off = fseek(dof->dof_fp, sec->dofs_offset, SEEK_SET);
	if (off == -1) {

		return -1;
	}

	/* Load the section ASCII->binary. */ 
	strtab = (char *) bbuf_data(buf);
	
	bytes = fread((void *) strtab, sizeof(unsigned char), sec->dofs_size,
	    dof->dof_fp);
	if (bytes != sec->dofs_size) {

		return -1;
	}

	return 0;
}

int
dof_load_probedesc(struct dof *dof, dof_hdr_t *hdr, dof_sec_t *sec,
    dof_probedesc_t *pdesc)
{
	struct bbuf *buf;
	off_t off;
	unsigned char *data;
	ssize_t bytes;
	int rc;

	if (sec->dofs_type != DOF_SECT_PROBEDESC) {

		return -1;
	}

	off = fseek(dof->dof_fp, sec->dofs_offset, SEEK_SET);
	if (off == -1) {

		return -1;
	}
		
	if (hdr->dofh_ident[DOF_ID_ENCODING] == DOF_ENCODE_LSB) {
	
		rc = bbuf_new(&buf, NULL, sec->dofs_size, BBUF_LITTLEENDIAN);
	} else {

		rc = bbuf_new(&buf, NULL, sec->dofs_size, BBUF_BIGENDIAN);
	}

	data = bbuf_data(buf);

	/* Load the section */ 
	bytes = fread((void *) data, sizeof(unsigned char), sec->dofs_size,
	    dof->dof_fp);
	if (bytes != sec->dofs_size) {

		return -1;
	}

	rc = bbuf_get_uint32(buf, &pdesc->dofp_strtab);
	rc |= bbuf_get_uint32(buf, &pdesc->dofp_provider);
	rc |= bbuf_get_uint32(buf, &pdesc->dofp_mod);
	rc |= bbuf_get_uint32(buf, &pdesc->dofp_func);
	rc |= bbuf_get_uint32(buf, &pdesc->dofp_name);
	rc |= bbuf_get_uint32(buf, &pdesc->dofp_id);
	if (rc != 0) {

		bbuf_delete(buf);
		return -1;
	}

	bbuf_delete(buf);
	return 0;
}
