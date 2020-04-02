/*-
 * Copyright (c) 2019-20 (Graeme Jenkinson)
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

#include <stdlib.h>
#include <babeltrace2/babeltrace.h>
#include <glib.h>

#include "dof.h"
#include "dof2ctf.h"

#define BT_LOG_TAG "PLUGIN/SRC.DTRACE.KAFKA/CTF-WRITER"
#include "ctf-writer/logging.h"
#include "ctf-writer/writer.h"
#include "ctf-writer/clock.h"
#include "compat/stdlib.h"

/* libbbuf memory allocation and freeing functions. */
const bbuf_malloc_func bbuf_alloc = malloc;
const bbuf_free_func bbuf_free = free;

/* dtrace clock frequency  - 1/10^-9 (nanosec granularity). */
static const uint64_t DTRACE_CLOCK_FREQ = 1000000000;
static char const * const CLOCK_NAME = "dtrace";
static char const * const STREAM_NAME = "dtrace";
static char const * const TMP_DIRNAME = "dof2ctf_XXXXXX";

void
dof2ctf(char *buf, size_t len, FILE *fp, bt_logging_level log_level,
    bt_self_component *self_comp)
{
	struct dof *dof;
	dof_hdr_t hdr;
	struct bt_ctf_writer *writer;
	struct bt_ctf_trace *trace;
	struct bt_ctf_stream_class *stream_class;
	struct bt_ctf_stream *stream;
	struct bt_ctf_field_type *event_header_type;
	struct bt_ctf_clock *clock;
	struct bt_ctf_field_type *epid_ft = NULL;
	struct bt_ctf_field_type *timestamp_ft = NULL;
	struct bt_ctf_field_type *_uint8_t;
	struct bt_ctf_field_type *_uint16_t;
	struct bt_ctf_field_type *_uint32_t;
	struct bt_ctf_field_type *_uint64_t;
	struct bt_ctf_event_class *epidnone_event_class;
	struct bt_ctf_event *epidnone_event;
	gchar *trace_path;
	char *metadata_string;
	uint32_t epid = 0;
	int rc;
	int ret;

	BT_LOGI_STR("Converting DOF to CTF metadata representation\n");

	/* Create DOF from the received metadata buffer. */
	rc = dof_new_buf(&dof, buf, len);
	if (rc != 0) {

		BT_LOGE_STR("Failed parsing DOF\n");
		return;
	}

	rc = dof_load_header(dof, &hdr);
	if (rc != 0) {

		BT_LOGE_STR("Failed parsing DOF header\n");
		goto err_destroy_dof;
	}

	/* Create /tmp filepath to write the generated CTF metadata (TSDL) file to. */
	trace_path = g_build_filename(g_get_tmp_dir(), TMP_DIRNAME, NULL);
	BT_ASSERT(trace_path != NULL);
	if (!bt_mkdtemp(trace_path)) {
	
		BT_LOGE("Failed to create temporary directory for the CTF writer\n");
		goto err_destroy_dof;
	}

	writer = bt_ctf_writer_create(trace_path);
	BT_ASSERT(writer != NULL);
	if (writer == NULL) {

		BT_LOGE("Failed to create CTF writer\n");
		g_free(trace_path);
		goto err_destroy_dof;
	}

	g_free(trace_path);

	/* Integer aliases */
	_uint8_t = get_field_type(FIELD_TYPE_ALIAS_UINT8_T);
	_uint16_t = get_field_type(FIELD_TYPE_ALIAS_UINT16_T);
	_uint32_t = get_field_type(FIELD_TYPE_ALIAS_UINT32_T);
	_uint64_t = get_field_type(FIELD_TYPE_ALIAS_UINT64_T);

	/* Set the trace endianness based on DOF. */
	trace = bt_ctf_writer_get_trace(writer);
	BT_ASSERT(trace != NULL);
	if (hdr.dofh_ident[DOF_ID_ENCODING] == DOF_ENCODE_MSB) {

		BT_LOGD("Trace byte order: big-endian\n");
		bt_ctf_trace_set_native_byte_order(trace, BT_CTF_BYTE_ORDER_BIG_ENDIAN);
	} else {

		BT_LOGD("Trace byte order: little-endian\n");
		bt_ctf_trace_set_native_byte_order(trace, BT_CTF_BYTE_ORDER_LITTLE_ENDIAN);
	}

	/* Create a CTF clock.
	 * The 64bit timestamp in the dtrace header represents nanoseconds since 
	 * epcoh (note that this is different to the traditional dtrace
	 * interpretation * of the timestamp field - as cycles since reset).
	 */
	clock = bt_ctf_clock_create(CLOCK_NAME);
	BT_ASSERT(clock != NULL);
	if (clock == NULL) {

		BT_LOGE("Failed to create CTF clock\n");
		goto err_put_ref_writer;
	}

	ret = bt_ctf_clock_set_frequency(clock, DTRACE_CLOCK_FREQ);
	if (ret != 0) {
		
		BT_LOGE("Failed to set CTF clock frequency\n");
		goto err_put_ref_clock;
	}

	ret = bt_ctf_writer_add_clock(writer, clock);

	/* Create a single CTF event stream */
	stream_class = bt_ctf_stream_class_create(STREAM_NAME);
	BT_ASSERT(stream_class != NULL);
	if (stream_class == NULL) {

		BT_LOGE("Failed to create CTF stream class\n");
		goto err_put_ref_clock;
	}

	stream = bt_ctf_writer_create_stream(writer, stream_class);
	BT_ASSERT(stream != NULL);
	if (stream == NULL) {

		BT_LOGE("Failed to create CTF stream\n");
		//bt_ctf_object_put_ref(stream_class);
		goto err_put_ref_clock;
	}

	/* Create the event header.
	 * The dtrace trace buffer is formated as:
	 * <EPID><TS><DATA>
	 * where EPID is a 32bit identifier for the enabled probe
	 * and TS is a 64bit timestamp.
	 */
	event_header_type = bt_ctf_field_type_structure_create();
	BT_ASSERT(event_header_type != NULL);
	if (event_header_type == NULL) {

		BT_LOGE("Failed to create CTF event header\n");
		goto err_put_ref_stream;
	}

	epid_ft = bt_ctf_field_type_integer_create(sizeof(uint32_t) * CHAR_BIT);
	BT_ASSERT(epid_ft != NULL);

	ret = bt_ctf_field_type_set_alignment(epid_ft, sizeof(uint32_t) * CHAR_BIT);
	if (ret != 0) {

		//BT_LOGE("Cannot add `id` field to event header field type.");
		//goto end;
	}

	ret = bt_ctf_field_type_set_byte_order(epid_ft,
	    hdr.dofh_ident[DOF_ID_ENCODING] == DOF_ENCODE_MSB ?
	    BT_CTF_BYTE_ORDER_BIG_ENDIAN : BT_CTF_BYTE_ORDER_LITTLE_ENDIAN);
	if (ret != 0) {

		BT_LOGE("Error setting epid byte order\n");
		//goto end;
	}

	ret = bt_ctf_field_type_structure_add_field(event_header_type,
		epid_ft, "id");
	if (ret != 0) {

		//BT_LOGE("Cannot add `id` field to event header field type.");
		//goto end;
	}
	//bt_ctf_object_put_ref(epid_ft);

	timestamp_ft = bt_ctf_field_type_integer_create(sizeof(uint64_t) * CHAR_BIT);
	BT_ASSERT(timestamp_ft != NULL);

	ret = bt_ctf_field_type_set_alignment(timestamp_ft, sizeof(uint64_t) * CHAR_BIT);
	if (ret != 0) {

		//BT_LOGE("Cannot add `id` field to event header field type.");
		//goto end;
	}

	ret = bt_ctf_field_type_set_byte_order(timestamp_ft,
	    hdr.dofh_ident[DOF_ID_ENCODING] == DOF_ENCODE_MSB ?
	    BT_CTF_BYTE_ORDER_BIG_ENDIAN : BT_CTF_BYTE_ORDER_LITTLE_ENDIAN);
	if (ret != 0) {

		//BT_LOGE("Cannot add `id` field to event header field type.");
		//goto end;
	}

	ret = bt_ctf_field_type_integer_set_mapped_clock_class(timestamp_ft, clock->clock_class);
	if (ret != 0) {

		BT_LOGE("Error setting timestamp byte order\n");
		//BT_LOGE_STR("Cannot add `timestamp` field to event header field type.");
		//goto end;
	}

	ret = bt_ctf_field_type_structure_add_field(event_header_type,
		timestamp_ft, "timestamp");
	if (ret != 0) {

		//BT_LOGE_STR("Cannot add `timestamp` field to event header field type.");
		//goto end;
	}
	//bt_ctf_object_put_ref(timestamp_ft);
	
	ret = bt_ctf_field_type_set_alignment(event_header_type, sizeof(uint64_t) * CHAR_BIT);
	if (ret != 0) {

		//BT_LOGE_STR("Cannot add `timestamp` field to event header field type.");
		//goto end;
	}

	bt_ctf_stream_add_event_header(stream_class, event_header_type);
	
	/* Create EPINONE (id = 0) event class.
	 * The trace buffer from dtrace is padded with zeroes, this event
	 * matches those ids preventing babletrace reporting an
	 * invalid event. 
	 */
	epidnone_event_class = bt_ctf_event_class_create("EPIDNONE");
	BT_ASSERT(epidnone_event_class != NULL);
	if (epidnone_event_class == NULL) {

		//BT_LOGE_STR("Cannot add `timestamp` field to event header field type.");
		//goto end;
	}

	ret = bt_ctf_event_class_set_id(epidnone_event_class, epid++);
	if (ret != 0) {

		//BT_LOGE_STR("Cannot add `timestamp` field to event header field type.");
		//goto end;
	}
	
	bt_ctf_stream_class_add_event_class(stream_class, epidnone_event_class);
	//bt_ctf_object_put_ref(epidnone_event_class);

	epidnone_event = bt_ctf_event_create(epidnone_event_class);
	BT_ASSERT(epidnone_event_class != NULL);
	if (epidnone_event == NULL) {

		//BT_LOGE_STR("Cannot add `timestamp` field to event header field type.");
		//bt_ctf_object_put_ref(epidnone_event_class);
		//goto end;
	}
	//bt_ctf_object_put_ref(epidnone_event_class);

	/* Add the EPIDNONE event to the stream. */
	bt_ctf_stream_append_event(stream, epidnone_event);
	//bt_ctf_object_put_ref(epidnone_event);

	/* Iterate across all of the sections in the DOF file.
	 * Any error in parsing the DOF result in failure to provide the CTF metadata.
	 * Partial failures with simply result in a partially parsed stream, which
	 * is likely to be of likely practical use.
	 */
	for (uint32_t sec_num = 1; sec_num <= hdr.dofh_secnum;
	    sec_num++) {
		dof_sec_t sec;

		/* Load the section, processing each of the ECDDESCs. */
		rc = dof_load_sect(dof, &hdr, sec_num, &sec);
		if (rc != 0) {

			BT_LOGE("Failure loading DOF section: %u\n", sec_num);
			goto err_put_ref_writer;
		}

		if (sec.dofs_type == DOF_SECT_ECBDESC) { 

			dof_ecbdesc_t ecbdesc;
			dof_probedesc_t pdesc;
			dof_sec_t psec;
			struct bbuf *strtab_buf;
			struct bt_ctf_event_class *event_class;
			char *strtab;
				
			/* Load the ECBDESC section data */
			rc = dof_load_ecbdesc(dof, &hdr, &sec, &ecbdesc);
			if (rc != 0) {
				
				BT_LOGE_STR("Error parsing ecbdesc (DOF malformed)\n");
				goto err_put_ref_writer;
			}

			/* Load the PROBEDESC section header specified in the ECBDESC. */
			rc = dof_load_sect(dof, &hdr, ecbdesc.dofe_probes + 1, &psec);
			if (rc != 0) {

				BT_LOGE_STR("Error parsing probedesc (DOF malformed)\n");
				goto err_put_ref_writer;
			}

			/* Load the PROBEDESC section data. */
			rc = dof_load_probedesc(dof, &hdr, &psec, &pdesc);
			if (rc != 0) {
				
				BT_LOGE_STR("Error parsing probedesc (DOF malformed)\n");
				goto err_put_ref_writer;
			}

			/* Load the STRTAB section header. */
			rc = dof_load_sect(dof, &hdr, pdesc.dofp_strtab + 1, &sec);
			if (rc != 0) {

				BT_LOGE_STR("Error parsing strtab (DOF malformed)\n");
				goto err_put_ref_writer;
			}

			/* Load the PROBEDESC STRTAB section. */
			rc = dof_load_sect_hex(dof, &sec, &strtab_buf);
			if (rc != 0) {
				
				BT_LOGE_STR("Error parsing probedec strtab (DOF malformed)\n");
				goto err_put_ref_writer;
			}

			/* Reconstruct the probe 4-tuple as the CTF event name */
			strtab = (char *) bbuf_data(strtab_buf);
			int n;
			n = snprintf(NULL, 0, "%s:%s:%s:%s", 
				&strtab[pdesc.dofp_provider], &strtab[pdesc.dofp_mod],
				&strtab[pdesc.dofp_func], &strtab[pdesc.dofp_name]);

			char probe_name[n];
			sprintf(probe_name, "%s:%s:%s:%s",
				&strtab[pdesc.dofp_provider], &strtab[pdesc.dofp_mod],
				&strtab[pdesc.dofp_func], &strtab[pdesc.dofp_name]);

			/* Free the buffer holding the STRTAB used to
			 * construct the event/probe name.
			 */
			bbuf_delete(strtab_buf);

			/* Create a new event class representing enablings
			 * with this probename
			 */
			event_class = bt_ctf_event_class_create(probe_name);
			BT_ASSERT(event_class != NULL);
			if (event_class == NULL) {

				BT_LOGE("Error creating event class %s\n", probe_name);
				goto err_put_ref_writer;
			}

			/* Load the section header. */
			if (ecbdesc.dofe_actions != DOF_SECIDX_NONE) {

				dof_sec_t actsec;
				dof_actdesc_t *actdesc;
				struct bbuf *actdesc_buf;
				struct bt_ctf_event *event;
				struct sbuf *rec_name;
				unsigned int recn = 0;
			
				/* Load the section header. */
				rc = dof_load_sect(dof, &hdr,
				    ecbdesc.dofe_actions + 1, &actsec);
				if (rc != 0) {

					BT_LOGE_STR(
					    "Error parsing section header (DOF malformed)\n");
					goto err_put_ref_writer;
				}

				rc = dof_load_sect_hex(dof, &actsec, &actdesc_buf);
				if (rc != 0) {
					
					BT_LOGE_STR("Error parsing actdesc (DOF malformed)\n");
					goto err_put_ref_writer;
				}

				/* Iterate the ACTDESCs */
				actdesc = (dof_actdesc_t *) bbuf_data(actdesc_buf);
				BT_ASSERT(actdesc != NULL);

				rec_name = sbuf_new_auto();	
		
				for (size_t idx = 0; idx < bbuf_len(actdesc_buf);
				    idx += actsec.dofs_entsize) {

					dtrace_difo_t difo;

					/* Load the section header. */
					rc = dof_load_sect(dof, &hdr,
					    actdesc->dofa_difo + 1, &sec);
					if (rc != 0) {

						BT_LOGE_STR(
						    "Error parsing section header (DOF malformed)\n");
						goto err_put_ref_stream;
					}

					/* Load the DIFOHDR section. */
					rc = dof_load_difohdr(dof, &hdr, &sec, &difo);
					if (rc != 0) {
						
						BT_LOGE_STR(
						    "Error parsing difohdr (DOF malformed)\n");
						goto err_put_ref_stream;
					}
			
					/* Construct a name for the record. */	
					sbuf_printf(rec_name, "rec%d", recn++);
					sbuf_finish(rec_name);
					if (sbuf_error(rec_name) != 0) {

						BT_LOGE_STR("Error creating record name\n");
						goto err_put_ref_stream;
					}

					switch (actdesc->dofa_kind) {
					case DTRACEACT_DIFEXPR:
					case DTRACEACT_PRINTF:
						switch (difo.dtdo_rtype.dtdt_size) {
						case 0:
							/* Ignore */
							ret = 0;
							break;
						case 1:
							/* 8bit record */
							ret = bt_ctf_event_class_add_field(
							    event_class, _uint8_t,
							    sbuf_data(rec_name));
							break;
						case 2:
							/* 16bit record */
							ret = bt_ctf_event_class_add_field(
								event_class, _uint16_t,
								sbuf_data(rec_name));
							break;
						case 4:
							/* 32bit record */
							ret = bt_ctf_event_class_add_field(
							    event_class, _uint32_t,
							    sbuf_data(rec_name));
							break;
						case 8:
							/* 64bit record */
							ret = bt_ctf_event_class_add_field(
							    event_class, _uint64_t,
							    sbuf_data(rec_name));
							break;
						default: {
							/* Array record */
							struct bt_ctf_field_type *array;

							array = bt_ctf_field_type_array_create(
							    _uint8_t,
							    difo.dtdo_rtype.dtdt_size);
							BT_ASSERT(array != NULL);

							ret = bt_ctf_event_class_add_field(
							    event_class, array,
							    sbuf_data(rec_name));
							break;
						}
						}
						break;
					default:
						BT_LOGI(
						    "Action type not currently handled: %d\n",
						    actdesc->dofa_kind);
						break;
					}

					if (ret != 0) {

						BT_LOGE(
						    "Error adding field (%u bytes) to event\n",
						    difo.dtdo_rtype.dtdt_size);
						goto err_put_ref_stream;

					}

					actdesc++;
					sbuf_clear(rec_name);
				}
				
				/* Delete the sbuf used to store the record name */	
				sbuf_delete(rec_name);
					
				/* Assign the event id consistently with epid assignment - 
				 * that is monotonically increasing.
				 */
				ret = bt_ctf_event_class_set_id(event_class, epid++);

				/* Add the event_class and event to the stream
				 * (releasing their references)
				 */
				bt_ctf_stream_class_add_event_class(
				    stream_class, event_class);

				event = bt_ctf_event_create(event_class);
				BT_ASSERT_DBG(event != NULL);
				if (event == NULL) {

					BT_LOGE("Error creating event");
					bt_ctf_object_put_ref(event_class);
					goto err_put_ref_stream;
				}
				bt_ctf_stream_append_event(stream, event);

				bt_ctf_object_put_ref(event_class);
				bt_ctf_object_put_ref(event);
			}
		}
	}

	/* Write the complete metadata to the provided file pointer. */
	metadata_string = bt_ctf_writer_get_metadata_string(writer);
	fwrite(metadata_string, strlen(metadata_string), sizeof(char), fp);

	/* Flush the writer so that all the metadata is written to the
	 * file before closing.
	 */
	bt_ctf_writer_flush_metadata(writer);

	/* Release references to the remaining BT objects. */
	bt_ctf_object_put_ref(stream);
	bt_ctf_object_put_ref(stream_class);
	bt_ctf_object_put_ref(trace);
	BT_CTF_OBJECT_PUT_REF_AND_RESET(writer);

	/* Destroy the DOF object */
	dof_destroy(dof);

	return;

err_put_ref_stream:
	bt_ctf_object_put_ref(stream);

err_put_ref_clock:
	BT_CTF_OBJECT_PUT_REF_AND_RESET(clock);

err_put_ref_writer:
	BT_CTF_OBJECT_PUT_REF_AND_RESET(writer);

err_destroy_dof:
	dof_destroy(dof);
	return;
}
