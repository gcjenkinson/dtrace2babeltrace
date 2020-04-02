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

#ifndef BABELTRACE_PLUGIN_CTF_KAFKA_MSG_ITER_H
#define BABELTRACE_PLUGIN_CTF_KAFKA_MSG_ITER_H

#include <librdkafka/rdkafka.h>

struct kafka_component;
struct kafka_msg_iter;

enum kafka_iterator_status {
	/** Iterator state has progressed. Continue iteration immediately. */
	KAFKA_ITERATOR_STATUS_CONTINUE = 3,
	/** No message available for now. Try again later. */
	KAFKA_ITERATOR_STATUS_AGAIN = 2,
	/** No more CTF_KAFKAS to be delivered. */
	KAFKA_ITERATOR_STATUS_END = 1,
	/** No error, okay. */
	KAFKA_ITERATOR_STATUS_OK = 0,
	/** Invalid arguments. */
	KAFKA_ITERATOR_STATUS_INVAL = -1,
	/** General error. */
	KAFKA_ITERATOR_STATUS_ERROR = -2,
	/** Out of memory. */
	KAFKA_ITERATOR_STATUS_NOMEM = -3,
	/** Unsupported iterator feature. */
	KAFKA_ITERATOR_STATUS_UNSUPPORTED = -4,
};

extern bt_message_iterator_class_initialize_method_status kafka_msg_iter_init(
    bt_self_message_iterator *, bt_self_message_iterator_configuration *,
    bt_self_component_port_output *);
extern void kafka_msg_iter_finalize(bt_self_message_iterator * const);
extern bt_message_iterator_class_next_method_status kafka_msg_iter_next(
    bt_self_message_iterator *, bt_message_array_const, uint64_t, uint64_t *);

extern struct kafka_component * kafka_msg_iter_get_kafka_component(
    struct kafka_msg_iter *);
extern rd_kafka_t * kafka_msg_iter_get_consumer(struct kafka_msg_iter *);
extern bt_self_message_iterator * kafka_msg_iter_get_self_msg_iter(
    struct kafka_msg_iter *);
extern void kafka_msg_iter_set_was_interrupted(struct kafka_msg_iter *);

#endif
