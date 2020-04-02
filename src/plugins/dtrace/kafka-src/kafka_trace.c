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
#define BT_LOG_TAG "PLUGIN/SRC.DTRACE.KAFKA/TRACE"
#include "logging/comp-logging.h"

#include "kafka_msg_iter.h"
#include "kafka_stream_iter.h"
#include "kafka_trace.h"

struct kafka_trace {
	bt_logging_level log_level;
	bt_self_component *self_comp;
	struct kafka_msg_iter *msg_iter;
	uint64_t id; /* ctf trace ID within the session. */
	bt_trace *trace; /* Owned by this. */
	bt_trace_class *trace_class; /* Weak reference. */
	struct kafka_metadata *metadata;
	const bt_clock_class *clock_class;
	/* Array of pointers to struct kafka_stream_iter. */
	/* Owned by this. */
	GPtrArray *stream_iterators;
	bool new_metadata_needed;
};

BT_HIDDEN struct kafka_trace *
kafka_create_trace(struct kafka_msg_iter *msg_iter, bt_logging_level log_level,
    bt_self_component *self_comp, struct kafka_metadata *metadata, uint64_t trace_id)
{
	struct kafka_trace *trace;
	struct kafka_stream_iter *stream_iter;

	trace = g_new0(struct kafka_trace, 1);
	if (trace == NULL) {

		goto error;
	}
	BT_ASSERT_DBG(trace != NULL);

	trace->log_level = log_level;
	trace->self_comp = self_comp;
	trace->msg_iter = msg_iter;
	trace->id = trace_id;
	trace->trace_class = NULL;
	trace->trace = NULL;
	trace->stream_iterators = g_ptr_array_new_with_free_func(
	    (GDestroyNotify) kafka_stream_iter_destroy);
	BT_ASSERT_DBG(trace->stream_iterators != NULL);
	if (trace->stream_iterators == NULL) {

		goto error;
	}

	stream_iter = kafka_stream_iter_create(trace, 0,
	    kafka_msg_iter_get_self_msg_iter(msg_iter));
	BT_ASSERT_DBG(stream_iter != NULL);
	if (stream_iter == NULL) {

		goto error;
	}

	g_ptr_array_add(trace->stream_iterators, stream_iter);

	trace->metadata = metadata;
	trace->new_metadata_needed = true;

	goto end;
error:
	BT_COMP_LOGD("Failed to instatiate Kafka trace");
	g_free(trace);
	trace = NULL;
end:
	return trace;
}

BT_HIDDEN struct bt_trace *
kafka_trace_get_bt_trace(struct kafka_trace *self)
{

	/* Validate the method's preconditions */
	BT_ASSERT(self != NULL);

	return self->trace;
}

BT_HIDDEN struct kafka_stream_iter *
kafka_trace_get_kafka_stream_iter(struct kafka_trace *self)
{

	/* Validate the method's preconditions */
	BT_ASSERT(self != NULL);

	return g_ptr_array_index(self->stream_iterators, 0);
}

BT_HIDDEN struct kafka_metadata *
kafka_trace_get_kafka_metadata(struct kafka_trace *kafka_trace)
{
				
	return kafka_trace->metadata;
}

BT_HIDDEN bt_logging_level
kafka_trace_get_logging_level(struct kafka_trace *self)
{

	/* Validate the method's preconditions */
	BT_ASSERT(self != NULL);

	return self->log_level;
}

BT_HIDDEN bt_self_component *
kafka_trace_get_self_comp(struct kafka_trace *self)
{

	/* Validate the method's preconditions */
	BT_ASSERT(self != NULL);
	BT_ASSERT(self->self_comp != NULL);

	return self->self_comp;
}

BT_HIDDEN bool
kafka_trace_is_new_metadata_needed(struct kafka_trace *self)
{

	/* Validate the method's preconditions */
	BT_ASSERT(self != NULL);

	return self->new_metadata_needed;
}

BT_HIDDEN void
kafka_trace_set_new_metadata_needed(struct kafka_trace *self, bool needed)
{

	/* Validate the method's preconditions */
	BT_ASSERT(self != NULL);

	self->new_metadata_needed = needed;
}

BT_HIDDEN struct kafka_metadata *
kafka_trace_get_metadata(struct kafka_trace *self)
{

	/* Validate the method's preconditions */
	BT_ASSERT(self != NULL);

	return self->metadata;
}

BT_HIDDEN void
kafka_trace_set_metadata(struct kafka_trace *self, struct kafka_metadata *metadata)
{

	/* Validate the method's preconditions */
	BT_ASSERT(self != NULL);
	BT_ASSERT(metadata != NULL);

	self->metadata = metadata;
}

BT_HIDDEN bt_trace_class *
kafka_trace_get_bt_trace_class(struct kafka_trace *self)
{

	/* Validate the method's preconditions */
	BT_ASSERT(self != NULL);

	return self->trace_class;
}

BT_HIDDEN struct kafka_msg_iter *
kafka_trace_get_kafka_msg_iter(struct kafka_trace *self)
{

	/* Validate the method's preconditions */
	BT_ASSERT(self != NULL);

	return self->msg_iter;
}


BT_HIDDEN void
kafka_trace_set_bt_trace_class(struct kafka_trace *self, bt_trace_class *tc)
{

	/* Validate the method's preconditions */
	BT_ASSERT(self != NULL);
	BT_ASSERT(tc != NULL);

	self->trace_class = tc;
}

BT_HIDDEN void
kafka_trace_set_bt_trace(struct kafka_trace *self, bt_trace *trace)
{

	/* Validate the method's preconditions */
	BT_ASSERT(self != NULL);
	BT_ASSERT(trace != NULL);

	self->trace = trace;
}

BT_HIDDEN void
kafka_trace_set_bt_clock_class(struct kafka_trace *self, const bt_clock_class *cc)
{

	/* Validate the method's preconditions */
	BT_ASSERT(self != NULL);
	BT_ASSERT(cc != NULL);

	self->clock_class = cc;
}

BT_HIDDEN void
kafka_trace_foreach_stream_iter(struct kafka_trace *self,
    kafka_trace_callback cb, void *data)
{

	/* Validate the method's preconditions */
	BT_ASSERT(self != NULL);
	BT_ASSERT(cb != NULL);

	for (guint i = 0; i < self->stream_iterators->len; i++) {
	
		struct kafka_stream_iter *stream_iter;

		stream_iter = g_ptr_array_index(
		    self->stream_iterators, i);

		(* cb)(stream_iter, data);
	}
}
