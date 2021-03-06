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

#ifndef BABELTRACE_PLUGIN_DRACE_KAFKA_H
#define BABELTRACE_PLUGIN_DTRACE_KAFKA_H

#include <stdbool.h>

#include <babeltrace2/babeltrace.h>
#include <librdkafka/rdkafka.h>

/* Forward declaration */
struct kafka_component;

extern bt_component_class_initialize_method_status kafka_component_init(
    bt_self_component_source *, bt_self_component_source_configuration *,
    const bt_value *, void *);
extern void kafka_component_finalize(bt_self_component_source *component);
extern bt_component_class_query_method_status kafka_query(
    bt_self_component_class_source *, bt_private_query_executor *,
    const char *, const bt_value *, void *, const bt_value **);

extern bt_self_component * kafka_component_get_self_comp(struct kafka_component *);
extern bt_logging_level kafka_component_get_logging_level(struct kafka_component *);

extern rd_kafka_conf_t * kafka_component_get_conf(struct kafka_component *);
extern size_t kafka_component_get_max_request_sz(struct kafka_component *);
extern int64_t kafka_component_get_offset(struct kafka_component *);
extern char * kafka_component_get_topic_name(struct kafka_component *);

extern bool kafka_component_has_msg_iter(struct kafka_component *);
extern void kafka_component_set_has_msg_iter(struct kafka_component *, bool);

#endif /* BABELTRACE_PLUGIN_DTRACE_KAFKA_H */
