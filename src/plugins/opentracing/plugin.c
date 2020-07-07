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

#include <babeltrace2/babeltrace.h>
#include "jaeger/jaegertracing.h"

#ifndef BT_BUILT_IN_PLUGINS
BT_PLUGIN_MODULE();
#endif

BT_PLUGIN(opentracing);
BT_PLUGIN_DESCRIPTION("Reconstruct Jaegertracing spans");
BT_PLUGIN_AUTHOR("Graeme Jenkinson <gcj21@cl.cam.ac.uk>");
BT_PLUGIN_LICENSE("2-Clause BSD");

/* influxdb sink */
BT_PLUGIN_SINK_COMPONENT_CLASS(jaeger, jaeger_consume);
BT_PLUGIN_SINK_COMPONENT_CLASS_INITIALIZE_METHOD(jaeger, jaeger_init);
BT_PLUGIN_SINK_COMPONENT_CLASS_FINALIZE_METHOD(jaeger, jaeger_finalize);
BT_PLUGIN_SINK_COMPONENT_CLASS_GRAPH_IS_CONFIGURED_METHOD(jaeger,
    jaeger_graph_is_configured);
BT_PLUGIN_SINK_COMPONENT_CLASS_DESCRIPTION(jaeger,
    "Construct Jaegertracing spans from trace records");
BT_PLUGIN_SINK_COMPONENT_CLASS_HELP(jaeger,
    "See the babeltrace2-sink.opentracing.jaeger(7) manual page.");
