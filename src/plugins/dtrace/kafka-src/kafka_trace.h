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

#ifndef BABELTRACE_PLUGIN_CTF_KAFKA_TRACE_H
#define BABELTRACE_PLUGIN_CTF_KAFKA_TRACE_H

#include <stdbool.h>

struct kafka_msg_iter;
struct kafka_stream_iter;
struct kafka_trace;
struct kafka_metadata;

typedef void (* kafka_trace_callback)(struct kafka_stream_iter *, void *);

extern struct kafka_trace *kafka_create_trace(struct kafka_msg_iter*,
    bt_logging_level, bt_self_component *, struct kafka_metadata *, uint64_t);

extern void kafka_trace_set_metadata(struct kafka_trace *, struct kafka_metadata *);
extern struct kafka_stream_iter * kafka_trace_get_kafka_stream_iter(struct kafka_trace *);
extern bool kafka_trace_is_new_metadata_needed(struct kafka_trace *);
extern void kafka_trace_set_new_metadata_needed(struct kafka_trace *, bool);
extern void kafka_trace_set_bt_trace(struct kafka_trace *, bt_trace *);
extern bt_trace_class * kafka_trace_get_bt_trace_class(struct kafka_trace *);
extern void kafka_trace_set_bt_trace_class(struct kafka_trace *, bt_trace_class *);
extern void kafka_trace_set_bt_clock_class(struct kafka_trace *, const bt_clock_class *);
extern struct kafka_metadata * kafka_trace_get_metadata(struct kafka_trace *);
extern struct kafka_msg_iter * kafka_trace_get_kafka_msg_iter(struct kafka_trace *);
extern struct kafka_topic * kafka_trace_get_kafka_topic(struct kafka_trace *);
extern struct bt_trace * kafka_trace_get_bt_trace(struct kafka_trace *);
extern struct kafka_metadata * kafka_trace_get_kafka_metadata(struct kafka_trace *);
extern bt_logging_level kafka_trace_get_logging_level(struct kafka_trace *);
extern bt_self_component * kafka_trace_get_self_comp(struct kafka_trace *);

extern void kafka_trace_foreach_stream_iter(struct kafka_trace *,
    kafka_trace_callback, void *);

#endif
