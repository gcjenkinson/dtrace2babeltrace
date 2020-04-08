/*-
 * Copyright (c) 2020 (Graeme Jenkinson)
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
#define BT_LOG_TAG "PLUGIN/SRC.DTRACE.KAFKA/STREAM-ITER"
#include "logging/comp-logging.h"

#include <ctype.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <glib.h>
#include <inttypes.h>

#include <babeltrace2/babeltrace.h>
#include <librdkafka/rdkafka.h>

#include "compat/mman.h"
#include "common/assert.h"

#include "kafka_metadata.h"
#include "kafka_msg_iter.h"
#include "kafka_stream_iter.h"
#include "kafka_trace.h"

/* Iterator over a live stream. */
struct kafka_stream_iter {
	bt_logging_level log_level;
	bt_self_component *self_comp;
	bt_stream *stream; /* Owned by this. */
	uint8_t *buf; /* Owned by this. */
	GString *name; /* Owned by this. */
	struct ctf_msg_iter *msg_iter; /* Owned by this */
	struct kafka_trace *trace; /* Weak reference. */
	uint64_t stream_id;
	enum kafka_stream_state state;
	rd_kafka_t *consumer; /* Owned by this. */
	size_t buflen;
};

static enum ctf_msg_iter_medium_status medop_request_bytes(
    size_t, uint8_t **, size_t *, void *);
static bt_stream *medop_borrow_stream(bt_stream_class *, int64_t,
    void *);

static char const * const DDTRACE_KEY = "ddtrace";
#define STREAM_NAME_PREFIX "stream-"

static struct ctf_msg_iter_medium_ops medops = {
    .request_bytes = medop_request_bytes,
    .seek = NULL,
    .borrow_stream = medop_borrow_stream,
};

static const useconds_t POLL_PERIOD = 1000; /* 1000ms */

/**
 * @returns 1 if all bytes are printable, else 0.
 */
static int
is_printable(const char *buf, size_t size) {
        size_t i;

	/* Validate the method's preconditions */
	BT_ASSERT(buf != NULL);

        for (i = 0 ; i < size ; i++)
                if (!isprint((int)buf[i]))
                        return 0;

        return 1;
}

static enum ctf_msg_iter_medium_status
kafka_get_stream_bytes(struct kafka_msg_iter *kafka_msg_iter,
    struct kafka_stream_iter *stream, uint8_t *buf,
    uint64_t *recv_len)
{
	enum ctf_msg_iter_medium_status retstatus =
	    CTF_MSG_ITER_MEDIUM_STATUS_OK;
	bt_logging_level log_level = stream->log_level;
	bt_self_component *self_comp = stream->self_comp;
	rd_kafka_message_t *rkmessage;
	
	/* Validate the method's preconditions */	
	BT_ASSERT(kafka_msg_iter != NULL);
	BT_ASSERT(stream != NULL);
	BT_ASSERT(buf != NULL);
	BT_ASSERT(recv_len != NULL);

	/* Poll Kafka for a log message */
	rkmessage = rd_kafka_consumer_poll(
	    kafka_msg_iter_get_consumer(kafka_msg_iter), POLL_PERIOD);
	if (rkmessage == NULL) {

		/* Poll timed out */
		BT_COMP_LOGD("Kafka poll timed out: %d ms\n", POLL_PERIOD);
		if (kafka_graph_is_canceled(kafka_msg_iter)) {

			kafka_msg_iter_set_was_interrupted(kafka_msg_iter);
		}
		return CTF_MSG_ITER_MEDIUM_STATUS_AGAIN;
	}
	BT_ASSERT_DBG(rkmessage != NULL);
	BT_ASSERT_DBG(rkmessage->len <= stream->buflen);

	switch (rkmessage->err) {
	case RD_KAFKA_RESP_ERR_NO_ERROR:

		/* Valid the message key */ 
		if (rkmessage->key_len == strlen(DDTRACE_KEY) &&
		    strncmp(rkmessage->key, DDTRACE_KEY, strlen(DDTRACE_KEY)) == 0) {
		
			BT_COMP_LOGD("Read message from kafka: key %.*s\n",
			    (int) rkmessage->key_len, (char *) rkmessage->key);

			/* Copy the message into the stream iterator
			 * buffer. The copy into buf should be safe as
			 * the buffer is set as large as the configured
			 * Kafak receive.max.message.bytes. (There is
			 * also and assert to verify this.)
			 */
			memcpy(buf, rkmessage->payload, rkmessage->len);
			*recv_len = rkmessage->len;
			retstatus = CTF_MSG_ITER_MEDIUM_STATUS_OK;
		} else {
			if (rkmessage->key && is_printable(rkmessage->key, 
			    rkmessage->key_len)) {

				BT_COMP_LOGE("Read message from kafka: invalid key %.*s\n",
				    (int) rkmessage->key_len, (char *) rkmessage->key);
			} else {

				BT_COMP_LOGE("Read message from kafka: invalid key (%d bytes)\n",
				    (int) rkmessage->key_len);
			}
	
			retstatus = CTF_MSG_ITER_MEDIUM_STATUS_AGAIN;
		}
		break;
	case RD_KAFKA_RESP_ERR__PARTITION_EOF:
		BT_COMP_LOGD("No message in Kafka topic\n");

		retstatus = CTF_MSG_ITER_MEDIUM_STATUS_AGAIN;
		break;
	default:
		BT_COMP_LOGE("Failed reading message from kafka %s\n",
		    rd_kafka_err2str(rkmessage->err));

		retstatus = CTF_MSG_ITER_MEDIUM_STATUS_ERROR;
		break;
	}
	BT_COMP_LOGD("Freed Kafka message\n");
	rd_kafka_message_destroy(rkmessage);

	return retstatus;
}

static enum ctf_msg_iter_medium_status
medop_request_bytes(size_t request_sz, uint8_t **buffer_addr,
    size_t *buffer_sz, void *data)
{
	enum ctf_msg_iter_medium_status status =
		CTF_MSG_ITER_MEDIUM_STATUS_OK;
	struct kafka_stream_iter *stream_iter = (struct kafka_stream_iter *) data;
	struct kafka_trace *trace = stream_iter->trace;
	uint64_t recv_len = 0;
	
	/* Validate the method's preconditions */
	BT_ASSERT(buffer_addr != NULL);
	BT_ASSERT(buffer_sz != NULL);
	BT_ASSERT(data != NULL);
   		   
	/* Read log message from Kafka */
	BT_ASSERT_DBG(trace != NULL);
	status = kafka_get_stream_bytes(kafka_trace_get_kafka_msg_iter(trace),
	    stream_iter, stream_iter->buf, &recv_len);
	switch (status) {
	case CTF_MSG_ITER_MEDIUM_STATUS_OK:

		*buffer_addr = stream_iter->buf;
		*buffer_sz = recv_len;
		break;
	case CTF_MSG_ITER_MEDIUM_STATUS_AGAIN:

		stream_iter->state = KAFKA_STREAM_ACTIVE_NO_DATA;
		break;
	default:

		stream_iter->state = KAFKA_STREAM_EOF;
		break;
	}
	return status;
}

static bt_stream *
medop_borrow_stream(bt_stream_class *stream_class, int64_t stream_id,
    void *data)
{
	struct kafka_stream_iter *kafka_stream = (struct kafka_stream_iter *) data;
	bt_logging_level log_level = kafka_stream->log_level;
	bt_self_component *self_comp = kafka_stream->self_comp;
	
	/* Validate the method's preconditions */
	BT_ASSERT(stream_class != NULL);
	BT_ASSERT(data != NULL);

	BT_ASSERT_DBG(kafka_stream->stream != NULL);
	if (kafka_stream->stream == NULL) {
		uint64_t stream_class_id;

		stream_class_id = bt_stream_class_get_id(stream_class);
		BT_COMP_LOGI("Creating stream %s (ID: %" PRIu64 ") out of stream "
		    "class %" PRId64, kafka_stream->name->str,
		    stream_id, stream_class_id);

		if (stream_id < 0) {
			/* No stream instance ID in the stream. */
			kafka_stream->stream = bt_stream_create_with_id(
			    stream_class,
			    kafka_trace_get_bt_trace(kafka_stream->trace),
			    kafka_stream->stream_id);
		} else {
			kafka_stream->stream = bt_stream_create_with_id(
			    stream_class,
			    kafka_trace_get_bt_trace(kafka_stream->trace),
			    (uint64_t) stream_id);
		}

		if (kafka_stream->stream == NULL) {
			BT_COMP_LOGE("Cannot create stream %s (stream class ID "
				"%" PRId64 ", stream ID %" PRIu64 ")",
				kafka_stream->name->str,
				stream_class_id, stream_id);
			goto end;
		}

		bt_stream_set_name(kafka_stream->stream,
		    kafka_stream->name->str);
	}

end:
	return kafka_stream->stream;
}

BT_HIDDEN enum kafka_iterator_status
kafka_lazy_msg_init(struct kafka_trace *trace,
    bt_self_message_iterator *self_msg_iter)
{
	struct ctf_trace_class *ctf_tc;
	struct kafka_stream_iter *stream_iter;
	struct kafka_metadata *metadata;
	bt_logging_level log_level;
	bt_self_component *self_comp;

	/* Validate the method's preconditions */
	BT_ASSERT(trace != NULL);
	BT_ASSERT(self_msg_iter != NULL);
	
	stream_iter = kafka_trace_get_kafka_stream_iter(trace);
	BT_ASSERT_DBG(stream_iter != NULL);

	log_level = stream_iter->log_level;
	self_comp = stream_iter->self_comp;

	BT_ASSERT_DBG(stream_iter->msg_iter != NULL);
	if (stream_iter->msg_iter == NULL) {

		metadata = kafka_trace_get_kafka_metadata(trace);
		BT_ASSERT_DBG(metadata != NULL);

		ctf_tc = ctf_metadata_decoder_borrow_ctf_trace_class(
		    kafka_metadata_get_decoder(metadata));
		BT_ASSERT_DBG(ctf_tc != NULL);

		stream_iter->msg_iter = ctf_msg_iter_create(ctf_tc,
		    stream_iter->buflen, medops, stream_iter,
		    log_level, self_comp, self_msg_iter);
		BT_ASSERT_DBG(stream_iter->msg_iter != NULL);
		if (stream_iter->msg_iter == NULL) {

			BT_COMP_LOGE("Failed creating CTF msg iter\n");
			goto error;
		}
	}

	return KAFKA_ITERATOR_STATUS_OK;

error:
	return KAFKA_ITERATOR_STATUS_ERROR;
}

BT_HIDDEN struct kafka_stream_iter *
kafka_stream_iter_create(bt_logging_level log_level,
    bt_self_component * self_comp, struct kafka_trace *trace,
    uint64_t stream_id, bt_self_message_iterator *self_msg_iter)
{
	struct kafka_component *kafka;
	struct kafka_metadata *metadata;
	struct kafka_stream_iter *stream_iter;

	/* Verify the method's preconditions */
	BT_ASSERT(trace != NULL);
	BT_ASSERT(self_msg_iter != NULL);
	BT_ASSERT_DBG(self_comp != NULL);

	kafka = kafka_msg_iter_get_kafka_component(
	    kafka_trace_get_kafka_msg_iter(trace));
	BT_ASSERT_DBG(kafka != NULL);

	stream_iter = g_new0(struct kafka_stream_iter, 1);
	BT_ASSERT_DBG(stream_iter != NULL);
	if (stream_iter == NULL) {

		goto error;
	}

	stream_iter->log_level = log_level;
	stream_iter->self_comp = self_comp;
	stream_iter->trace = trace;
	stream_iter->state = KAFKA_STREAM_ACTIVE_NO_DATA;
	stream_iter->stream_id = stream_id;
				
	metadata = kafka_trace_get_kafka_metadata(trace);
	BT_ASSERT_DBG(metadata != NULL);

	if (kafka_trace_get_bt_trace(trace) != NULL) {

		struct ctf_trace_class *ctf_tc;
	       
		ctf_tc = ctf_metadata_decoder_borrow_ctf_trace_class(
		    kafka_metadata_get_decoder(metadata));
		BT_ASSERT(!stream_iter->msg_iter);

		stream_iter->msg_iter = ctf_msg_iter_create(ctf_tc,
			kafka_component_get_max_request_sz(kafka), medops, stream_iter,
			log_level, self_comp, self_msg_iter);
		if (stream_iter->msg_iter == NULL) {

			goto error;
		}

		//ctf_msg_iter_set_emit_stream_end_message(
		//	stream_iter->msg_iter, true);

		//ctf_msg_iter_set_emit_stream_beginning_message(
		//	stream_iter->msg_iter, true);
	}

	/* Allocate buffer store Kafka log messages */
	stream_iter->buflen = kafka_component_get_max_request_sz(kafka);
	BT_ASSERT_DBG(stream_iter->buflen > 0);

	stream_iter->buf = g_new0(uint8_t, stream_iter->buflen);
	BT_ASSERT_DBG(stream_iter->buf != NULL);
	if (stream_iter->buf == NULL) {

		goto error;
	}

	stream_iter->name = g_string_new(NULL);
	BT_ASSERT_DBG(stream_iter->name != NULL);
	if (stream_iter->name == NULL) {

		goto error;
	}

	g_string_printf(stream_iter->name, STREAM_NAME_PREFIX "%" PRIu64,
	    stream_iter->stream_id);

	goto end;
error:
	kafka_stream_iter_destroy(stream_iter);
	stream_iter = NULL;
end:
	return stream_iter;
}

BT_HIDDEN void
kafka_stream_iter_destroy(struct kafka_stream_iter *self)
{

	if (self == NULL) {
		return;
	}
	BT_ASSERT_DBG(self != NULL);

	if (self->stream) {

		BT_STREAM_PUT_REF_AND_RESET(self->stream);
	}

	if (self->msg_iter) {

		ctf_msg_iter_destroy(self->msg_iter);
	}

	g_free(self->buf);
	g_string_free(self->name, TRUE);

	/*
	 * Ensure we poke the trace metadata in the future, which is
	 * required to release the metadata reference on the trace.
	 */
	BT_ASSERT_DBG(self->trace != NULL);
	kafka_trace_set_new_metadata_needed(self->trace, true);

	g_free(self);
}

BT_HIDDEN struct ctf_msg_iter *
kafka_stream_iter_get_ctf_msg_iter(struct kafka_stream_iter *self)
{

	/* Validate the method's preconditions */
	BT_ASSERT(self != NULL);
	return self->msg_iter;
}
