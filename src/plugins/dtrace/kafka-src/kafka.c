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
#define BT_LOG_TAG "PLUGIN/SRC.DTRACE.KAFKA"
#include "logging/comp-logging.h"

#include <glib.h>
#include <inttypes.h>
#include <unistd.h>

#include "common/assert.h"
#include <babeltrace2/babeltrace.h>
#include "compat/compiler.h"
#include <babeltrace2/types.h>

#include "plugins/common/muxing/muxing.h"
#include "plugins/common/param-validation/param-validation.h"

#include "kafka.h"
#include "kafka_metadata.h"
#include "kafka_stream_iter.h"
#include "kafka_trace.h"

#define BOOTSTRAP_SERVERS_PARAM		"bootstrap.servers"
#define TOPIC_PARAM			"topic"
#define GROUP_ID_PARAM			"group.id"
#define MAX_QUERY_SIZE_PARAM		"receive.max.message.bytes"

#define DEFAULT_GROUP_ID		"src.dtrace.kafka"
#define MAX_QUERY_SIZE			(1024*1024)

/*
 * A component instance is an iterator on a single session.
 */
struct kafka_component {
	bt_logging_level log_level;
	bt_self_component *self_comp; /* Borrowed ref. */
	GString *bootstrap_servers; /* Owned by this. */
	GString *group_id; /* Owned by this. */
	GString *topic; /* Owned by this. */
	size_t max_query_sz;
	/*
	 * Keeps track of whether the downstream component already has a
	 * message iterator on this component.
	 */
	bool has_msg_iter;
};

static bt_component_class_initialize_method_status kafka_component_create(
    const bt_value *, bt_logging_level, bt_self_component *,
    struct kafka_component **);
static void kafka_component_destroy_data(struct kafka_component *);

static struct bt_param_validation_map_value_entry_descr params_descr[] = {
	{ BOOTSTRAP_SERVERS_PARAM, BT_PARAM_VALIDATION_MAP_VALUE_ENTRY_MANDATORY,
	    { .type = BT_VALUE_TYPE_STRING }},
	{ TOPIC_PARAM, BT_PARAM_VALIDATION_MAP_VALUE_ENTRY_MANDATORY,
	    { .type = BT_VALUE_TYPE_STRING }},
	{ GROUP_ID_PARAM, BT_PARAM_VALIDATION_MAP_VALUE_ENTRY_OPTIONAL,
            { .type = BT_VALUE_TYPE_STRING }},
	{ MAX_QUERY_SIZE_PARAM, BT_PARAM_VALIDATION_MAP_VALUE_ENTRY_OPTIONAL,
            { .type = BT_VALUE_TYPE_SIGNED_INTEGER}},
	BT_PARAM_VALIDATION_MAP_VALUE_ENTRY_END
};

static char const * const OUT_PORT_NAME = "out";

static void
apply_one_unsigned_integer(const char *key, const bt_value *params,
    unsigned int *option, unsigned int default_val)
{
	const bt_value *bt_val;

	/* Validate the method's preconditions */
	BT_ASSERT(key != NULL);
	BT_ASSERT(params != NULL);
	BT_ASSERT(option != NULL);

	bt_val = bt_value_map_borrow_entry_value_const(params, key);
	if (bt_val == NULL) {

		*option = default_val;
		goto end;
	}

	BT_ASSERT_DBG(bt_val != NULL);
	*option = bt_value_integer_unsigned_get(bt_val);
	
end:
	return;
}

static void
apply_one_string(const char *key, const bt_value *params, char **option,
    char *default_val)
{
	const bt_value *bt_val;
	const char *val;

	/* Validate the method's preconditions */
	BT_ASSERT(key != NULL);
	BT_ASSERT(params != NULL);
	BT_ASSERT(option != NULL);
	BT_ASSERT(default_val != NULL);

	bt_val = bt_value_map_borrow_entry_value_const(params, key);
	if (bt_val == NULL) {

		*option = g_strdup(default_val);
		goto end;
	}

	BT_ASSERT_DBG(bt_val != NULL);
	val = bt_value_string_get(bt_val);

	BT_ASSERT_DBG(val != NULL);
	*option = g_strdup(val);
end:
	return;
}

static bt_component_class_initialize_method_status
kafka_component_create(const bt_value *params, bt_logging_level log_level,
    bt_self_component *self_comp, struct kafka_component **component)
{
	struct kafka_component *kafka = NULL;
	enum bt_param_validation_status validation_status;
	gchar *validation_error = NULL;
	bt_component_class_initialize_method_status status;

	/* Validate the method's preconditions */
	BT_ASSERT(params != NULL);
	BT_ASSERT(self_comp != NULL);
	BT_ASSERT(component != NULL);

	/* Validate the component parameters */
	validation_status = bt_param_validation_validate(params, params_descr,
		&validation_error);
	if (validation_status == BT_PARAM_VALIDATION_STATUS_MEMORY_ERROR) {

		status = BT_COMPONENT_CLASS_INITIALIZE_METHOD_STATUS_MEMORY_ERROR;
		goto error;
	} else if (validation_status == BT_PARAM_VALIDATION_STATUS_VALIDATION_ERROR) {

		BT_COMP_LOGE_APPEND_CAUSE(self_comp, "%s", validation_error);
		status = BT_COMPONENT_CLASS_INITIALIZE_METHOD_STATUS_ERROR;
		goto error;
	}

	kafka = g_new0(struct kafka_component, 1);
	if (kafka == NULL) {

		status = BT_COMPONENT_CLASS_INITIALIZE_METHOD_STATUS_MEMORY_ERROR;
		goto end;
	}

	kafka->log_level = log_level;
	kafka->self_comp = self_comp;
	apply_one_unsigned_integer(MAX_QUERY_SIZE_PARAM, params,
	    &kafka->max_query_sz, MAX_QUERY_SIZE);
	kafka->has_msg_iter = false;
	
	apply_one_string(BOOTSTRAP_SERVERS_PARAM, params, &kafka->bootstrap_servers, "");
	apply_one_string(TOPIC_PARAM, params, &kafka->topic, "");
	apply_one_string(GROUP_ID_PARAM, params, &kafka->group_id, DEFAULT_GROUP_ID);

	status = BT_COMPONENT_CLASS_INITIALIZE_METHOD_STATUS_OK;
	goto end;

error:
	kafka_component_destroy_data(kafka);
	kafka = NULL;
end:
	g_free(validation_error);

	*component = kafka;
	return status;
}

static void
kafka_component_destroy_data(struct kafka_component *self)
{
	if (self == NULL) {

		return;
	}
	BT_ASSERT_DBG(self != NULL);

	BT_ASSERT_DBG(self->bootstrap_servers != NULL);
	g_free(self->bootstrap_servers);

	BT_ASSERT_DBG(self->group_id != NULL);
	g_free(self->group_id);

	BT_ASSERT_DBG(self->topic != NULL);
	g_free(self->topic);

	g_free(self);
}

BT_HIDDEN bt_component_class_initialize_method_status
kafka_component_init(bt_self_component_source *self_comp_src,
    bt_self_component_source_configuration *config, const bt_value *params,
    __attribute__((unused)) void *init_method_data)
{
	struct kafka_component *kafka;
	bt_component_class_initialize_method_status ret;
	bt_self_component *self_comp;
	bt_logging_level log_level;
	bt_self_component_add_port_status add_port_status;

	/* Validate the method's preconditions */
	BT_ASSERT(self_comp_src != NULL);
	BT_ASSERT(params != NULL);
	
	self_comp = bt_self_component_source_as_self_component
		(self_comp_src);
	BT_ASSERT_DBG(self_comp != NULL);

	log_level = bt_component_get_logging_level(
		bt_self_component_as_component(self_comp));

	ret = kafka_component_create(params, log_level, self_comp, &kafka);
	if (ret != BT_COMPONENT_CLASS_INITIALIZE_METHOD_STATUS_OK) {

		goto error;
	}
	BT_ASSERT_DBG(kafka != NULL);

	add_port_status = bt_self_component_source_add_output_port(
		self_comp_src, OUT_PORT_NAME, NULL, NULL);
	if (add_port_status != BT_SELF_COMPONENT_ADD_PORT_STATUS_OK) {

		ret = (int) add_port_status;
		goto end;
	}

	bt_self_component_set_data(self_comp, kafka);
	goto end;

error:
	kafka_component_destroy_data(kafka);
	kafka = NULL;
end:
	return ret;
}

BT_HIDDEN void
kafka_component_finalize(bt_self_component_source *component)
{
	struct kafka_component *kafka;
       
	kafka = bt_self_component_get_data(
	    bt_self_component_source_as_self_component(component));
	if (kafka == NULL) {

		return;
	}
	BT_ASSERT_DBG(kafka != NULL);

	kafka_component_destroy_data(kafka);
}

BT_HIDDEN
bt_component_class_query_method_status kafka_query(
    bt_self_component_class_source *comp_class,
    bt_private_query_executor *priv_query_exec,
    const char *object, const bt_value *params,
    __attribute__((unused)) void *method_data, const bt_value **result)
{
	bt_component_class_query_method_status status =
		BT_COMPONENT_CLASS_QUERY_METHOD_STATUS_OK;
	bt_self_component *self_comp = NULL;
	bt_logging_level log_level = bt_query_executor_get_logging_level(
		bt_private_query_executor_as_query_executor_const(
			priv_query_exec));

	if (strcmp(object, "group.id") == 0) {

		*result = bt_value_string_create_init(DEFAULT_GROUP_ID);
	} else if (strcmp(object, "receive.max.message.bytes") == 0) {
		char *buf;
		size_t buf_sz;

		buf_sz = snprintf(NULL, 0, "%zu", MAX_QUERY_SIZE);
		buf = alloca(buf_sz + 1);
		snprintf(buf, buf_sz + 1, "%zu", MAX_QUERY_SIZE);

		*result = bt_value_string_create_init(buf);
	} else {

		BT_COMP_LOGI("Unknown query object `%s`", object);
		status = BT_COMPONENT_CLASS_QUERY_METHOD_STATUS_UNKNOWN_OBJECT;
		goto end;
	}

end:
	return status;
}

BT_HIDDEN bool
kafka_graph_is_canceled(struct kafka_msg_iter *msg_iter)
{
	bool ret;

	if (msg_iter == NULL) {

		ret = false;
		goto end;
	}
	BT_ASSERT_DBG(msg_iter != NULL);

	ret = bt_self_message_iterator_is_interrupted(
	    kafka_msg_iter_get_self_msg_iter(msg_iter));

end:
	return ret;
}

BT_HIDDEN size_t
kafka_get_max_request_sz(struct kafka_component *self)
{

	/* Validate the method's preconditions */
	BT_ASSERT(self != NULL);

	return self->max_query_sz;
}

BT_HIDDEN bool
kafka_component_has_msg_iter(struct kafka_component *self)
{

	/* Validate the method's preconditions */
	BT_ASSERT(self != NULL);

	return self->has_msg_iter;
}

BT_HIDDEN void
kafka_component_set_has_msg_iter(struct kafka_component *self, bool has_msg_iter)
{

	/* Validate the method's preconditions */
	BT_ASSERT(self != NULL);

	self->has_msg_iter = has_msg_iter;
}

BT_HIDDEN bt_self_component *
kafka_component_get_self_comp(struct kafka_component *self)
{

	/* Validate the method's preconditions */
	BT_ASSERT(self != NULL);

	return self->self_comp;
}

BT_HIDDEN bt_logging_level
kafka_component_get_logging_level(struct kafka_component *self)
{

	/* Validate the method's preconditions */
	BT_ASSERT(self != NULL);

	return self->log_level;
}

BT_HIDDEN GString  *
kafka_component_get_bootstrap_servers(struct kafka_component *self)
{

	/* Validate the method's preconditions */
	BT_ASSERT(self != NULL);

	return self->bootstrap_servers;
}

BT_HIDDEN GString  *
kafka_component_get_group_id(struct kafka_component *self)
{

	/* Validate the method's preconditions */
	BT_ASSERT(self != NULL);

	return self->group_id;
}

BT_HIDDEN GString  *
kafka_component_get_topic(struct kafka_component *self)
{

	/* Validate the method's preconditions */
	BT_ASSERT(self != NULL);

	return self->topic;
}
