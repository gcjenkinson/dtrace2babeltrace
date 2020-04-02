/*-
 * Copyright (c) 2019-2020 (Graeme Jenkinson)
 * All rights reserved.
 *
 * This software was developed by BAE Systems, the University of Cambridge
 * Computer Laboratory, and Memorial University under DARPA/AFRL contract
 * FA8650-15-C-7558 ("CADETS"), as part of the DARPA Transparent Computing
 * (TC) research program.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract FA8750-10-C-0237
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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

#define BT_COMP_LOG_SELF_COMP self_comp
#define BT_LOG_OUTPUT_LEVEL log_level
#define BT_LOG_TAG "PLUGIN/SRC.DTRACE.KAFKA/META"
#include "logging/comp-logging.h"

#include <ctype.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <glib.h>
#include "compat/memstream.h"
#include <babeltrace2/babeltrace.h>

#include "kafka_metadata.h"
#include "kafka_trace.h"
#include "common/metadata/decoder.h"
#include "common/metadata/ctf-meta-configure-ir-trace.h"

#include "dof2ctf.h"

struct kafka_metadata {
	bt_logging_level log_level;
	bt_self_component *self_comp;
	struct kafka_trace *trace; /* Weak reference */
	struct ctf_metadata_decoder *decoder; /* Weak reference */
};

static char const * const DOF_KEY = "dof";

/**
 * @returns 1 if all bytes are printable, else 0.
 */
static int
is_printable(const char *buf, size_t size) {
        size_t i;

	/* Validate the mehod's preconditions */
	BT_ASSERT(buf != NULL);

        for (i = 0 ; i < size ; i++)
                if (!isprint((int)buf[i]))
                        return 0;

        return 1;
}

static bool
stream_classes_all_have_default_clock_class(bt_trace_class *tc,
    bt_logging_level log_level, bt_self_component *self_comp)
{
	uint64_t i, sc_count;
	const bt_clock_class *cc = NULL;
	const bt_stream_class *sc;
	bool ret = true;

	/* Validate the mehod's preconditions */
	BT_ASSERT(tc != NULL);
	BT_ASSERT(self_comp != NULL);

	sc_count = bt_trace_class_get_stream_class_count(tc);
	for (i = 0; i < sc_count; i++) {

		sc = bt_trace_class_borrow_stream_class_by_index_const(tc, i);
		BT_ASSERT_DBG(sc != NULL);

		cc = bt_stream_class_borrow_default_clock_class_const(sc);
		BT_ASSERT_DBG(sc != NULL);
		if (cc == NULL) {

			ret = false;
			BT_COMP_LOGE("Stream class doesn't have a default clock class: "
			    "sc-id=%" PRIu64 ", sc-name=\"%s\"",
			    bt_stream_class_get_id(sc),
			    bt_stream_class_get_name(sc));
			goto end;
		}
	}

end:
	return ret;
}

/*
 * Iterate over the stream classes and returns the first clock class
 * encountered. This is useful to create message iterator inactivity message as
 * we don't need a particular clock class.
 */
static const bt_clock_class *
borrow_any_clock_class(bt_trace_class *tc)
{
	uint64_t i, sc_count;
	const bt_clock_class *cc = NULL;
	const bt_stream_class *sc;

	/* Validate the method's preconditions */
	BT_ASSERT(tc != NULL);

	sc_count = bt_trace_class_get_stream_class_count(tc);
	for (i = 0; i < sc_count; i++) {

		sc = bt_trace_class_borrow_stream_class_by_index_const(tc, i);
		BT_ASSERT_DBG(sc != NULL);

		cc = bt_stream_class_borrow_default_clock_class_const(sc);
		if (cc != NULL) {
			goto end;
		}
	}
end:
	BT_ASSERT_DBG(cc != NULL);
	return cc;
}

BT_HIDDEN enum kafka_iterator_status
kafka_metadata_update(struct kafka_trace *trace)
{
	struct kafka_metadata *metadata;
	size_t size;
	char *metadata_buf = NULL;
	FILE *fp = NULL;
	enum ctf_metadata_decoder_status decoder_status;
	enum kafka_iterator_status status = KAFKA_ITERATOR_STATUS_OK;
	bt_logging_level log_level;
	bt_self_component *self_comp;
	rd_kafka_message_t *rkmessage;
	int poll_period_ms = 1000; /* 1000ms */
	
	log_level = kafka_trace_get_logging_level(trace);
	self_comp = kafka_trace_get_self_comp(trace);

	metadata = kafka_trace_get_kafka_metadata(trace);
	BT_ASSERT_DBG(metadata != NULL);

	/* Check whether the trace metadata has already been processed.
	 */
	if (!kafka_trace_is_new_metadata_needed(trace)) {

		BT_COMP_LOGW("Kafka trace metadata present already procesed!");
		goto end;
	}

	/* Grab all available metadata. */
	BT_COMP_LOGD("Polling Kafka for metadata (%dms)\n", poll_period_ms);

	rkmessage = rd_kafka_consumer_poll(
	    kafka_msg_iter_get_consumer(kafka_trace_get_kafka_msg_iter(trace)),
	    poll_period_ms);
	if (rkmessage == NULL) {

		/* Poll timed out */
		BT_COMP_LOGD("Kafka poll timed out: %d ms\n", poll_period_ms);
		if (kafka_graph_is_canceled(
	    	    kafka_trace_get_kafka_msg_iter(trace))) {

			kafka_msg_iter_set_was_interrupted(
			    kafka_trace_get_kafka_msg_iter(trace));
		}
		return KAFKA_ITERATOR_STATUS_AGAIN;
	}
	BT_ASSERT_DBG(rkmessage != NULL);
	
	switch (rkmessage->err) {
	case RD_KAFKA_RESP_ERR_NO_ERROR:

		if (strncmp(rkmessage->key, DOF_KEY, strlen(DOF_KEY)) == 0) {

			/* Open for writing */
			fp = bt_open_memstream(&metadata_buf, &size);
			BT_ASSERT_DBG(fp != NULL);
			if (fp == NULL) {

				if (errno == EINTR &&
				    kafka_graph_is_canceled(
					kafka_trace_get_kafka_msg_iter(trace))) {
					status = KAFKA_ITERATOR_STATUS_AGAIN;
				} else {
					BT_COMP_LOGE_APPEND_CAUSE_ERRNO(self_comp,
					    "Metadata open_memstream", ".");
					status = KAFKA_ITERATOR_STATUS_ERROR;
				}
				goto error;
			}

			/* Convert the DOF into TSDL */
			dof2ctf(rkmessage->payload, rkmessage->len, fp,
			    log_level, self_comp);
	
			/* Free the Kafka message containing the DOF */
			rd_kafka_message_destroy(rkmessage);
			break;
		} else {

			if (rkmessage->key && is_printable(rkmessage->key, 
			    rkmessage->key_len)) {

				BT_COMP_LOGW(
				    "Log message from kafka: invalid key %.*s\n",
				    (int) rkmessage->key_len, (char *) rkmessage->key);
			} else {

				BT_COMP_LOGW(
				    "Log message from kafka: invalid key (%d bytes)\n",
				    (int) rkmessage->key_len);
			}

			/* Free the Kafka message */
			rd_kafka_message_destroy(rkmessage);
			status = KAFKA_ITERATOR_STATUS_AGAIN;
			goto end;
		}
		break;
	case RD_KAFKA_RESP_ERR__PARTITION_EOF:

		BT_COMP_LOGD("No message in Kafka topic\n");
		status = KAFKA_ITERATOR_STATUS_AGAIN;
		rd_kafka_message_destroy(rkmessage);
		goto end;
	default:
		/* Set the status to _ERROR unless errno is
		 * EINTR (in which case try again _AGAIN)
		 */
		status = KAFKA_ITERATOR_STATUS_ERROR;
		if (errno == EINTR) {
			if (kafka_graph_is_canceled(
			    kafka_trace_get_kafka_msg_iter(trace))) {

				status = KAFKA_ITERATOR_STATUS_AGAIN;
			}
		}

		rd_kafka_message_destroy(rkmessage);
		goto end;
	}

	if (bt_close_memstream(&metadata_buf, &size, fp)) {

		BT_COMP_LOGE("bt_close_memstream: %s", strerror(errno));
		goto error;
	}
		
	fp = bt_fmemopen(metadata_buf, size, "rb");
	BT_ASSERT_DBG(fp != NULL);
	if (fp == NULL) {
		if (errno == EINTR &&
		    kafka_graph_is_canceled(
			kafka_trace_get_kafka_msg_iter(trace))) {
			status = KAFKA_ITERATOR_STATUS_AGAIN;
		} else {
			BT_COMP_LOGE_APPEND_CAUSE_ERRNO(self_comp,
			    "Cannot memory-open metadata buffer", ".");
			status = KAFKA_ITERATOR_STATUS_ERROR;
		}
		goto error;
	}

	/*
	 * The call to ctf_metadata_decoder_append_content() will append
	 * new metadata to our current trace class.
	 */
	BT_COMP_LOGD("Appending new metadata to the ctf_trace class");
	decoder_status = ctf_metadata_decoder_append_content(
	    metadata->decoder, fp);
	switch (decoder_status) {
	case CTF_METADATA_DECODER_STATUS_OK:
		if (kafka_trace_get_bt_trace_class(trace) == NULL) {
			struct ctf_trace_class *tc;

			tc = ctf_metadata_decoder_borrow_ctf_trace_class(
			    metadata->decoder);
			BT_ASSERT_DBG(tc != NULL);

			kafka_trace_set_bt_trace_class(trace, 
			    ctf_metadata_decoder_get_ir_trace_class(
	 		    metadata->decoder));
			kafka_trace_set_bt_trace(trace,
			    bt_trace_create(kafka_trace_get_bt_trace_class(trace)));
			if (kafka_trace_get_bt_trace(trace) == NULL) {

				goto error;
			}

			if (ctf_trace_class_configure_ir_trace(tc,
			    kafka_trace_get_bt_trace(trace))) {

				goto error;
			}

			if (!stream_classes_all_have_default_clock_class(
			    kafka_trace_get_bt_trace_class(trace), log_level,
			    self_comp)) {

				goto error;
			}
			kafka_trace_set_bt_clock_class(trace,
			    borrow_any_clock_class(
			    kafka_trace_get_bt_trace_class(trace)));
		}
		kafka_trace_set_new_metadata_needed(trace, false);
		break;
	case CTF_METADATA_DECODER_STATUS_INCOMPLETE:
		status = KAFKA_ITERATOR_STATUS_AGAIN;
		break;
	default:
		goto error;
	}

	goto end;
error:
	status = KAFKA_ITERATOR_STATUS_ERROR;
end:
	if (fp != NULL) {
		int closeret;

		closeret = fclose(fp);
		if (closeret != 0) {

			BT_COMP_LOGE("Error on fclose");
		}
	}
	free(metadata_buf);
	return status;
}

BT_HIDDEN int
kafka_metadata_create(struct kafka_metadata **self,
    bt_logging_level log_level, bt_self_component *self_comp)
{
	struct kafka_metadata *metadata = NULL;
	struct ctf_metadata_decoder_config cfg = {
		.log_level = log_level,
		.self_comp = self_comp,
		.clock_class_offset_s = 0,
		.clock_class_offset_ns = 0,
		.create_trace_class = true,
	};

	/* Validate the method's preconditions */
	BT_ASSERT(self != NULL);
	BT_ASSERT(self_comp != NULL);

	metadata = g_new0(struct kafka_metadata, 1);
	if (!metadata) {

		return -1;
	}

	metadata->log_level = log_level;
	metadata->self_comp = self_comp;
	metadata->decoder = ctf_metadata_decoder_create(&cfg);
	BT_ASSERT_DBG(metadata->decoder != NULL);
	if (metadata->decoder == NULL) {

		goto error;
	}

	*self = metadata;
	return 0;

error:
	ctf_metadata_decoder_destroy(metadata->decoder);
	g_free(metadata);
	return -1;
}

BT_HIDDEN void
kafka_metadata_fini(struct kafka_trace *trace)
{
	struct kafka_metadata *metadata;

	metadata = kafka_trace_get_metadata(trace);
	if (metadata == NULL) {

		return;
	}
	BT_ASSERT_DBG(metadata != NULL);

	BT_ASSERT_DBG(metadata->decoder != NULL);
	ctf_metadata_decoder_destroy(metadata->decoder);
	kafka_trace_set_metadata(trace, NULL);
	g_free(metadata);
}

BT_HIDDEN struct ctf_metadata_decoder *
kafka_metadata_get_decoder(struct kafka_metadata *self)
{

	/* Validate the method's preconditions */
	BT_ASSERT(self != NULL);
	return self->decoder;
}
