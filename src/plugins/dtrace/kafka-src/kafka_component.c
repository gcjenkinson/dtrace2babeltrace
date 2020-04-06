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

#include <glib.h>

#include <babeltrace2/babeltrace.h>
#include <babeltrace2/types.h>

#define BT_COMP_LOG_SELF_COMP self_comp
#define BT_LOG_OUTPUT_LEVEL log_level
#define BT_LOG_TAG "PLUGIN/SRC.DTRACE.KAFKA"
#include "logging/comp-logging.h"
#include "plugins/common/param-validation/param-validation.h"

#include "kafka_component.h"

#define OFFSET_PARAM			"offset"
#define MAX_QUERY_SIZE_PARAM		"max_query_sz"
#define RDKAFKA_CONF_PARAM		"rdkafka_conf"
#define TOPIC_PARAM			"topic"

#define DEFAULT_GROUP_ID		"src.dtrace.kafka"
#define DEFAULT_MAX_QUERY_SIZE		(1024*1024)

/*
 * A component instance is an iterator on a single session.
 */
struct kafka_component {
	bt_logging_level log_level;
	bt_self_component *self_comp; /* Borrowed ref. */
	gchar *topic_name; /* Owned by this. */
	size_t max_query_sz;
	rd_kafka_conf_t *conf; /* Owned by this. */
	int64_t offset;
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
static enum bt_param_validation_status rdkafka_conf_validation(const bt_value *,
    struct bt_param_validation_context *);
static enum bt_value_map_foreach_entry_const_func_status rdkafka_conf_validate_entry(
    const char *, const struct bt_value *, void *);

static struct bt_param_validation_map_value_entry_descr params_descr[] = {
	{ OFFSET_PARAM, BT_PARAM_VALIDATION_MAP_VALUE_ENTRY_OPTIONAL,
	    { .type = BT_VALUE_TYPE_STRING}},
	{ TOPIC_PARAM, BT_PARAM_VALIDATION_MAP_VALUE_ENTRY_MANDATORY,
	    { .type = BT_VALUE_TYPE_STRING }},
	{ MAX_QUERY_SIZE_PARAM, BT_PARAM_VALIDATION_MAP_VALUE_ENTRY_OPTIONAL,
            { .type = BT_VALUE_TYPE_SIGNED_INTEGER}},
	{ RDKAFKA_CONF_PARAM, BT_PARAM_VALIDATION_MAP_VALUE_ENTRY_OPTIONAL,
	    { .type = BT_VALUE_TYPE_MAP, .validation_func = rdkafka_conf_validation
	} },
	BT_PARAM_VALIDATION_MAP_VALUE_ENTRY_END
};

static char const * const OUT_PORT_NAME = "out";


static enum bt_value_map_foreach_entry_const_func_status
rdkafka_conf_validate_entry(const char *key, const struct bt_value *object, void *data)
{
	rd_kafka_conf_t *conf = (rd_kafka_conf_t *) data;
	char errstr[256];
	const char *value;

	if (bt_value_is_bool(object)) {

		size_t sz;
		char *buf;

		sz = snprintf(NULL, 0, "%s",
		    bt_value_bool_get(object) == true ? "true" : "false");
		buf = alloca(sz + 1);
		snprintf(buf, sz + 1, "%s",
		    bt_value_bool_get(object) == true ? "true" : "false");
		value = buf;
	} else if (bt_value_is_signed_integer(object)) {

		size_t sz;
		char *buf;

		sz = snprintf(NULL, 0, "%ld",
		    bt_value_integer_unsigned_get(object));
		buf = alloca(sz + 1);
		snprintf(buf, sz + 1, "%ld",
		    bt_value_integer_unsigned_get(object));
		value = buf;
	} else if (bt_value_is_unsigned_integer(object)) {

		size_t sz;
		char *buf;

		sz = snprintf(NULL, 0, "%lu",
		    bt_value_integer_unsigned_get(object));
		value = alloca(sz + 1);
		snprintf(buf, sz + 1, "%lu",
		    bt_value_integer_unsigned_get(object));
		value = buf;
	} else if (bt_value_is_string(object)) {

		value = bt_value_string_get(object);
	} else {

		return BT_VALUE_MAP_FOREACH_ENTRY_FUNC_STATUS_INTERRUPT;
	}

	if (rd_kafka_conf_set(conf, key, value,
	    errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK) {

		return BT_VALUE_MAP_FOREACH_ENTRY_FUNC_STATUS_INTERRUPT;
	}

	return BT_VALUE_MAP_FOREACH_ENTRY_FUNC_STATUS_OK;
}

static enum bt_param_validation_status rdkafka_conf_validation(
    const bt_value *value, struct bt_param_validation_context *context)
{
	bt_value_map_foreach_entry_func_status status;
	rd_kafka_conf_t *conf;

	/* Validate that the parameter is specified as a map */
	if (!bt_value_is_map(value)) {

		return bt_param_validation_error(context,
		   "rdkafka_conf param not specified as a map");
	}

	conf = rd_kafka_conf_new();
	if (conf == NULL) {

		return bt_param_validation_error(context,
		   "unable to validate rdkafka_conf");
	}

	/* Validate that entries in the map are valid rdkafka configuration
	 * options.
	 */
	status = bt_value_map_foreach_entry_const(value,
	    rdkafka_conf_validate_entry, conf);
	if (status != BT_VALUE_MAP_FOREACH_ENTRY_STATUS_OK) {

		rd_kafka_conf_destroy(conf);
		return bt_param_validation_error(context,
		    "Invalid rdkafka configuration parameter");
	}

	rd_kafka_conf_destroy(conf);

	return BT_PARAM_VALIDATION_STATUS_OK;
}

/*
 * Apply parameter with key `key` to `option`.  Use `def` as the value, if
 * the parameter is not specified.
 */
static void
apply_one_bool_with_default(const char *key, const bt_value *params,
    bool *option, bool def)
{
	const bt_value *value;

	/* Validate the method's preconditions */
	BT_ASSERT(key != NULL);
	BT_ASSERT(params != NULL);
	BT_ASSERT(option != NULL);

	value = bt_value_map_borrow_entry_value_const(params, key);
	if (value != NULL) {

		bt_bool bool_val;

		BT_ASSERT_DBG(value != NULL);
		bool_val = bt_value_bool_get(value);

		*option = (bool) bool_val;
	} else {
		*option = def;
	}
}

static void
apply_one_unsigned_integer(const char *key, const bt_value *params,
    unsigned long int *option, unsigned int default_val)
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
apply_one_string(const char *key, const bt_value *params, gchar **option,
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
	const bt_value *rdkafka_conf;
	char *buf, *offset;
	size_t buf_sz;
	char errstr[256];

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
	    &kafka->max_query_sz, DEFAULT_MAX_QUERY_SIZE);
	kafka->has_msg_iter = false;

	apply_one_string(TOPIC_PARAM, params, &kafka->topic_name, "");

	/* Parse the offset param if present */
	apply_one_string(OFFSET_PARAM, params, &offset, "LATEST");
	if (strcmp(offset, "EARLIEST") == 0) {

		kafka->offset =  RD_KAFKA_OFFSET_BEGINNING;
	} else {

		kafka->offset =  RD_KAFKA_OFFSET_END;
	}

	rdkafka_conf = bt_value_map_borrow_entry_value_const(params, RDKAFKA_CONF_PARAM);
	BT_ASSERT_DBG(rdkafka_conf != NULL);

	kafka->conf = rd_kafka_conf_new();
	if (kafka->conf == NULL) {

		status = BT_COMPONENT_CLASS_INITIALIZE_METHOD_STATUS_MEMORY_ERROR;
		goto end;
	}

	/* Set some default configuration values */
	if (rd_kafka_conf_set(kafka->conf, "group.id", DEFAULT_GROUP_ID,
	    errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK) {

		BT_COMP_LOGE("Failed setting Kafka conf\n");
		rd_kafka_conf_destroy(kafka->conf);
		status = BT_COMPONENT_CLASS_INITIALIZE_METHOD_STATUS_ERROR;
		goto error;
	}

	if (rd_kafka_conf_set(kafka->conf, "auto.offset.reset", "earliest",
	    errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK) {

		BT_COMP_LOGE("Failed setting Kafka conf\n");
		rd_kafka_conf_destroy(kafka->conf);
		status = BT_COMPONENT_CLASS_INITIALIZE_METHOD_STATUS_ERROR;
		goto error;
	}

	buf_sz = snprintf(NULL, 0, "%zu", kafka->max_query_sz);
	buf = alloca(buf_sz + 1);
	snprintf(buf, buf_sz + 1, "%zu",  kafka->max_query_sz);

	if (rd_kafka_conf_set(kafka->conf, "receive.message.max.bytes", buf,
	    errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK) {

		BT_COMP_LOGE("Failed setting Kafka conf\n");
		rd_kafka_conf_destroy(kafka->conf);
		status = BT_COMPONENT_CLASS_INITIALIZE_METHOD_STATUS_ERROR;
		goto error;
	}

	/* Validate that entries in the map are valid rdkafka configuration
	 * options.
	 */
	if (bt_value_map_foreach_entry_const(rdkafka_conf,
	    rdkafka_conf_validate_entry, kafka->conf) !=
	    BT_VALUE_MAP_FOREACH_ENTRY_STATUS_OK) {

		status = BT_COMPONENT_CLASS_INITIALIZE_METHOD_STATUS_ERROR;
		rd_kafka_conf_destroy(kafka->conf);
		goto end;
	}

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

	g_free(self->topic_name);
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
	bt_logging_level log_level;

	/* Validate the method'd preconditions */
	BT_ASSERT(comp_class != NULL);
	BT_ASSERT(priv_query_exec != NULL);
	BT_ASSERT(object != NULL);
	BT_ASSERT(result != NULL);

	log_level = bt_query_executor_get_logging_level(
	    bt_private_query_executor_as_query_executor_const(
	    priv_query_exec));

	if (strcmp(object, "group.id") == 0) {

		*result = bt_value_string_create_init(DEFAULT_GROUP_ID);
	} else if (strcmp(object, "receive.max.message.bytes") == 0) {
		char *buf;
		size_t buf_sz;

		buf_sz = snprintf(NULL, 0, "%u", DEFAULT_MAX_QUERY_SIZE);
		buf = alloca(buf_sz + 1);
		snprintf(buf, buf_sz + 1, "%u", DEFAULT_MAX_QUERY_SIZE);

		*result = bt_value_string_create_init(buf);
	} else {

		BT_COMP_LOGI("Unknown query object `%s`", object);
		status = BT_COMPONENT_CLASS_QUERY_METHOD_STATUS_UNKNOWN_OBJECT;
		goto end;
	}

end:
	return status;
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

BT_HIDDEN rd_kafka_conf_t *
kafka_component_get_conf(struct kafka_component *self)
{

	/* Validate the method's preconditions */
	BT_ASSERT(self != NULL);

	return self->conf;
}

BT_HIDDEN size_t
kafka_component_get_max_request_sz(struct kafka_component *self)
{

	/* Validate the method's preconditions */
	BT_ASSERT(self != NULL);

	return self->max_query_sz;
}

BT_HIDDEN int64_t
kafka_component_get_offset(struct kafka_component *self)
{

	/* Validate the method's preconditions */
	BT_ASSERT(self != NULL);

	return self->offset;
}

BT_HIDDEN char *
kafka_component_get_topic_name(struct kafka_component *self)
{

	/* Validate the method's preconditions */
	BT_ASSERT(self != NULL);

	return (char *) self->topic_name;
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
