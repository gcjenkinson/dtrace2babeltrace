/*-
 * Copyright (c) 2019-20 (Graeme Jenkinson)
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

#ifndef BABELTRACE_PLUGIN_DTRACE_KAFKA_STREAM_ITER_H
#define BABELTRACE_PLUGIN_DTRACE_KAFKA_STREAM_ITER_H

#include <stdio.h>
#include <glib.h>
#include <babeltrace2/babeltrace.h>

#include "common/macros.h"
#include "common/msg-iter/msg-iter.h"

#include "kafka_trace.h"

/* Forward declaration */
struct kafka_steam_iter;

enum kafka_stream_state {
	/* This stream won't have data until some known time in the future. */
	KAFKA_STREAM_QUIESCENT,
	/*
	 * This stream won't have data until some known time in the future and
	 * the message iterator inactivity message was already sent downstream.
	 */
	KAFKA_STREAM_QUIESCENT_NO_DATA, /* */
	/* This stream has data ready to be consumed. */
	KAFKA_STREAM_ACTIVE_DATA,
	/*
	 * This stream has no data left to consume. We should asked the relay
	 * for more.
	 */
	KAFKA_STREAM_ACTIVE_NO_DATA,
	/* This stream won't have anymore data, ever. */
	KAFKA_STREAM_EOF,
};

extern struct kafka_stream_iter *kafka_stream_iter_create(
    bt_logging_level, bt_self_component *, struct kafka_trace *,
    uint64_t, bt_self_message_iterator *);
extern void kafka_stream_iter_destroy(struct kafka_stream_iter *);

extern enum kafka_iterator_status kafka_lazy_msg_init(struct kafka_trace *,
    bt_self_message_iterator *self_msg_iter);

extern struct ctf_msg_iter * kafka_stream_iter_get_ctf_msg_iter(
    struct kafka_stream_iter *);

#endif /* BABELTRACE_PLUGIN_DTRACE_KAFKA_STREAM_ITER_H */
