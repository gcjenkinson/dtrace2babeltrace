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

extern "C" {
#define BT_COMP_LOG_SELF_COMP self_comp
#define BT_LOG_OUTPUT_LEVEL log_level
#define BT_LOG_TAG "PLUGIN/SINK.OPENTRACING.JAEGER"
#include "logging/comp-logging.h"

#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include <babeltrace2/babeltrace.h>

#include "compat/compiler.h"
#include "common/common.h"
#include "common/assert.h"
#include "plugins/common/param-validation/param-validation.h"
#include "lib/object.h"
#include "jaegertracing.h"
}

#include <exception>
#include <string>

struct jaeger_options {
	bool verbose;
};

struct jaeger_component {
	bt_self_component *self_comp;
	bt_logging_level log_level;
	bt_message_iterator *iterator;
	struct jaeger_options options;
};

#define BATCH_OPT "batch"
#define PWD_OPT "pwd"
#define TAGS_OPT "tags"
#define USER_OPT "user"
#define URI_OPT "uri"
#define VERBOSE_OPT "verbose"

static const char * const BATCH_OPT_NAME = BATCH_OPT;
static const char * const PWD_OPT_NAME = PWD_OPT;
static const char * const TAGS_OPT_NAME = TAGS_OPT;
static const char * const USER_OPT_NAME = USER_OPT;
static const char * const URI_OPT_NAME = URI_OPT;
static const char * const VERBOSE_OPT_NAME = VERBOSE_OPT;

static const char * const IN_PORT_NAME = "in";

static struct jaeger_component* create_jaeger(bt_self_component *,
    bt_logging_level);
static void destroy_jaeger_data(struct jaeger_component *);
static bt_message_iterator_class_next_method_status handle_message(
    struct jaeger_component *, const bt_message *);

static void apply_one_string(const char *, const bt_value *, const char **);
static void apply_one_bool_with_default(const char *, const bt_value *,
    bool *, bool);
static void apply_one_unsigned_integer(const char *, const bt_value *,
    unsigned int *);
static bt_component_class_initialize_method_status apply_params(
    struct jaeger_component *, const bt_value *);
static void write_bool(struct jaeger_component *,
    const char *, const bt_field *);
static void write_field(struct jaeger_component *,
    const char *, const bt_field *);
static void write_integer(struct jaeger_component *,
    const char *, const bt_field *);
static void write_struct(struct jaeger_component *,
    const char *, const bt_field *);
static void write_struct_field(struct jaeger_component *,
    const char *, const bt_field_class_structure_member *, const bt_field *);

static struct bt_param_validation_map_value_entry_descr jaeger_params[] = {
	{ BATCH_OPT, BT_PARAM_VALIDATION_MAP_VALUE_ENTRY_OPTIONAL,
    	    { .type = BT_VALUE_TYPE_SIGNED_INTEGER} },
    	{ PWD_OPT, BT_PARAM_VALIDATION_MAP_VALUE_ENTRY_OPTIONAL,
    	    { .type = BT_VALUE_TYPE_STRING } },
    	{ URI_OPT, BT_PARAM_VALIDATION_MAP_VALUE_ENTRY_MANDATORY,
    	    { .type = BT_VALUE_TYPE_STRING } },
    	{ USER_OPT, BT_PARAM_VALIDATION_MAP_VALUE_ENTRY_OPTIONAL,
    	    { .type = BT_VALUE_TYPE_STRING } },
    	{ VERBOSE_OPT, BT_PARAM_VALIDATION_MAP_VALUE_ENTRY_OPTIONAL,
    	   { .type = BT_VALUE_TYPE_BOOL } },
    	BT_PARAM_VALIDATION_MAP_VALUE_ENTRY_END
};

static void
destroy_jaeger_data(struct jaeger_component *jaeger)
{

	/* Verify the method's preconditions */
	if (jaeger == NULL) {

		goto end;
	}

	BT_ASSERT_DBG(jaeger != NULL);
	BT_ASSERT_DBG(jaeger->iterator != NULL);

	bt_message_iterator_put_ref(jaeger->iterator);

	free(jaeger);
end:
	return;
}

static struct jaeger_component *
create_jaeger(bt_self_component *self_comp, bt_logging_level log_level)
{
	struct jaeger_component *jaeger;

	/* Verify the method's preconditions */
	BT_ASSERT(self_comp != NULL);

	jaeger = (struct jaeger_component *) malloc(
	    sizeof(struct jaeger_component));
	BT_ASSERT_DBG(jaeger != NULL);
	if (jaeger == NULL) {

		goto error;
	}

	jaeger->self_comp = self_comp;
	jaeger->log_level = log_level;

	return jaeger;

error:
	BT_COMP_LOGE("Failed constructing InfluxDB component\n");
	return NULL;
}

static void
write_bool(struct jaeger_component *jaeger,
    const char *field_name, const bt_field *field)
{
	bt_bool v;

	/* Verify the method's preconditions */
	BT_ASSERT(jaeger != NULL);
	BT_ASSERT(field_name != NULL);
	BT_ASSERT(field != NULL);

	v = bt_field_bool_get_value(field);

	//p.addField(field_name, v == true ? "true" : "false");
}

static void
write_field(struct jaeger_component *jaeger,
    const char *name, const bt_field *field)
{
	bt_self_component *self_comp = jaeger->self_comp;
	bt_logging_level log_level = jaeger->log_level;
	bt_field_class_type class_id;

	/* Validate the method's preconditions */
	BT_ASSERT(jaeger != NULL);
	BT_ASSERT(field != NULL);

	class_id = bt_field_get_class_type(field);
	switch (class_id) {
	case BT_FIELD_CLASS_TYPE_BOOL:

		write_bool(jaeger, name, field);
		break;
	case BT_FIELD_CLASS_TYPE_STRUCTURE:

		write_struct(jaeger, name, field);
		break;
	 case BT_FIELD_CLASS_TYPE_UNSIGNED_INTEGER:
		/* FALLTHROUGH */
	 case BT_FIELD_CLASS_TYPE_SIGNED_INTEGER:

		write_integer(jaeger, name, field);
		break;
	case BT_FIELD_CLASS_TYPE_BIT_ARRAY:
	case BT_FIELD_CLASS_TYPE_STATIC_ARRAY:
		/* FALLTHROUGH */
	default:
		/* TODO: implement other types  */
		BT_COMP_LOGW("class_id %ld unimplemented\n", class_id);
		break;
	}
}

static void
write_integer(struct jaeger_component *jaeger,
    const char *field_name, const bt_field *field)
{
	const bt_field_class *int_fc;
	bt_field_class_type ft_type;

	/* Verify the method's preconditions */
	BT_ASSERT(jaeger != NULL);
	BT_ASSERT(field_name != NULL);
	BT_ASSERT(field != NULL);

	int_fc = bt_field_borrow_class_const(field);
	BT_ASSERT_DBG(int_fc != NULL);

	ft_type = bt_field_get_class_type(field);
	if (bt_field_class_type_is(ft_type,
	    BT_FIELD_CLASS_TYPE_UNSIGNED_INTEGER)) {

		//p.addField(field_name,
		//    bt_field_integer_signed_get_value(field));
	} else {

		//p.addField(field_name,
		//    bt_field_integer_unsigned_get_value(field));
	}
}

static void
write_struct(struct jaeger_component *jaeger,
    const char *field_name, const bt_field *field)
{
	const bt_field_class *struct_class = NULL;
	uint64_t nr_fields;

	/* Validate the method's preconditions */
	BT_ASSERT(jaeger != NULL);
	BT_ASSERT(field != NULL);

	struct_class = bt_field_borrow_class_const(field);
	BT_ASSERT_DBG(struct_class != NULL);

	/* Iterate the structures fields  adding each to the Point */
	nr_fields = bt_field_class_structure_get_member_count(struct_class);
	for (uint64_t i = 0; i < nr_fields; i++) {

		const bt_field *struct_field;
		const bt_field_class_structure_member *struct_member;

		struct_member =
		    bt_field_class_structure_borrow_member_by_index_const(
		    struct_class, i);
		BT_ASSERT_DBG(struct_member != NULL);

		struct_field =
		    bt_field_structure_borrow_member_field_by_index_const(
		    field, i);
		BT_ASSERT_DBG(struct_field != NULL);

		write_struct_field(jaeger, field_name, struct_member,
		    struct_field);
	}
}

static void
write_struct_field(struct jaeger_component *jaeger,
    const char *field_name,
    const bt_field_class_structure_member *member, const bt_field *field)
{
	const char *member_field_name;

	/* Validate the method's preconditions */
	BT_ASSERT(jaeger != NULL);
	BT_ASSERT(member != NULL);
	BT_ASSERT(field != NULL);

	member_field_name = bt_field_class_structure_member_get_name(member);
	BT_ASSERT_DBG(member_field_name != NULL);

	write_field(jaeger, member_field_name, field);
}

static bt_message_iterator_class_next_method_status
handle_message(struct jaeger_component *jaeger, const bt_message *message)
{
	bt_message_iterator_class_next_method_status ret =
	    BT_MESSAGE_ITERATOR_CLASS_NEXT_METHOD_STATUS_OK;
	const bt_event_class *event_class = NULL;
	bt_self_component *self_comp = jaeger->self_comp;
	bt_logging_level log_level = jaeger->log_level;
	const char *ev_name;
	const bt_event *event;
	bt_message_type msg_type;
	int rc;

	/* Validate the method's preconditions */
	BT_ASSERT(jaeger != NULL);
	BT_ASSERT(message != NULL);

	msg_type = bt_message_get_type(message);
	switch (msg_type) {
	case BT_MESSAGE_TYPE_STREAM_BEGINNING:

		BT_COMP_LOGD("Stream beginning\n");
		break;
	case BT_MESSAGE_TYPE_STREAM_END:

		BT_COMP_LOGD("Stream end\n");
		break;
	case BT_MESSAGE_TYPE_EVENT: {
		const bt_clock_snapshot *clock_snapshot;
		const bt_field *main_field;
		uint64_t ts_val;

		event = bt_message_event_borrow_event_const(message);
		BT_ASSERT_DBG(event != NULL);

		event_class = bt_event_borrow_class_const(event);
		BT_ASSERT_DBG(event_class != NULL);

		/* Event Header */
		ev_name = bt_event_class_get_name(event_class);
		BT_ASSERT_DBG(ev_name != NULL);

		/* Payload */
		main_field = bt_event_borrow_payload_field_const(event);
		BT_ASSERT_DBG(main_field != NULL);

		write_field(jaeger, NULL, main_field);

		/* Write the clock snapshot value */
		clock_snapshot =
		    bt_message_event_borrow_default_clock_snapshot_const(
		    message);
		BT_ASSERT_DBG(clock_snapshot != NULL);

		ts_val = bt_clock_snapshot_get_value(clock_snapshot);

		break;
	}
	case BT_MESSAGE_TYPE_DISCARDED_EVENTS:
		/* FALLTHROUGH */
	case BT_MESSAGE_TYPE_DISCARDED_PACKETS:

		BT_COMP_LOGD("Discarded EVENTS/PACKETS\n");
		break;
	default:
		BT_COMP_LOGD("Unhandled bt_message_type_event: 0x%02X\n",
		    msg_type);
		break;
	}

	return ret;
}

static void
apply_one_string(const char *key, const bt_value *params, char const **option)
{
	const bt_value *value = NULL;
	const char *str;

	/* Validate the method's preconditions */
	BT_ASSERT(key != NULL);
	BT_ASSERT(params != NULL);
	BT_ASSERT(option != NULL);

	value = bt_value_map_borrow_entry_value_const(params, key);
	if (value == NULL) {

		*option = NULL;
		goto end;
	}

	BT_ASSERT_DBG(value != NULL);
	str = bt_value_string_get(value);
	*option = str;

end:
	return;
}

static void
apply_one_unsigned_integer(const char *key, const bt_value *params,
    unsigned int *option)
{
	const bt_value *value = NULL;
	unsigned int val;

	/* Validate the method's preconditions */
	BT_ASSERT(key != NULL);
	BT_ASSERT(params != NULL);
	BT_ASSERT(option != NULL);

	value = bt_value_map_borrow_entry_value_const(params, key);
	if (!value) {

		goto end;
	}

	BT_ASSERT_DBG(value != NULL);
	val = bt_value_integer_unsigned_get(value);
	*option = val;

end:
	return;
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

static bt_component_class_initialize_method_status
apply_params(struct jaeger_component *jaeger, const bt_value *params)
{
	bt_component_class_initialize_method_status status;
    	bt_logging_level log_level = jaeger->log_level;
    	bt_self_component *self_comp = jaeger->self_comp;
	enum bt_param_validation_status validation_status;
	gchar *validate_error = NULL;
	const char *user = NULL, *passwd = NULL, *uri;
	unsigned int port = 0, batch = 0;


	/* Validate the method's preconditions */
	BT_ASSERT(jaeger != NULL);
	BT_ASSERT(params != NULL);

	/* Validate the parameters. */
	validation_status = bt_param_validation_validate(params,
	    jaeger_params, &validate_error);
	if (validation_status == BT_PARAM_VALIDATION_STATUS_MEMORY_ERROR) {

		status = BT_COMPONENT_CLASS_INITIALIZE_METHOD_STATUS_MEMORY_ERROR;
		goto end;
	} else if (validation_status == BT_PARAM_VALIDATION_STATUS_VALIDATION_ERROR) {

		status = BT_COMPONENT_CLASS_INITIALIZE_METHOD_STATUS_ERROR;
		BT_COMP_LOGE_APPEND_CAUSE(self_comp, "%s", validate_error);
		goto end;
	}

	/* Parse the parameters. */
	apply_one_unsigned_integer(BATCH_OPT_NAME, params, &batch);
	apply_one_string(URI_OPT_NAME, params, &uri);
	apply_one_string(USER_OPT_NAME, params, &user);
	apply_one_string(PWD_OPT_NAME, params, &passwd);
	apply_one_bool_with_default(VERBOSE_OPT_NAME, params,
	    &jaeger->options.verbose, false);

end:
	g_free(validate_error);

	return status;
}

BT_HIDDEN bt_component_class_initialize_method_status
jaeger_init(bt_self_component_sink *self_comp_sink,
    bt_self_component_sink_configuration *config,
    const bt_value *params, __attribute__((unused)) void *init_method_data)
{
	bt_component_class_initialize_method_status status;
	bt_self_component_add_port_status add_port_status;
	struct jaeger_component *jaeger;
	bt_self_component *self_comp;
	const bt_component *comp;
	bt_logging_level log_level;

	/* Validate the method's preconditions */
	BT_ASSERT(self_comp_sink != NULL);
	BT_ASSERT(params != NULL);

	self_comp = bt_self_component_sink_as_self_component(self_comp_sink);
	BT_ASSERT_DBG(self_comp != NULL);

	comp = bt_self_component_as_component(self_comp);
	BT_ASSERT_DBG(comp != NULL);

	log_level = bt_component_get_logging_level(comp);

	jaeger = create_jaeger(self_comp, log_level);
	if (jaeger == NULL) {

		status = BT_COMPONENT_CLASS_INITIALIZE_METHOD_STATUS_MEMORY_ERROR;
		goto error;
	}
	BT_ASSERT_DBG(jaeger != NULL);

	add_port_status = bt_self_component_sink_add_input_port(
	    self_comp_sink, IN_PORT_NAME, NULL, NULL);
	switch (add_port_status) {
	case BT_SELF_COMPONENT_ADD_PORT_STATUS_OK:
		status = BT_COMPONENT_CLASS_INITIALIZE_METHOD_STATUS_OK;
		break;
	case BT_SELF_COMPONENT_ADD_PORT_STATUS_MEMORY_ERROR:
		status = BT_COMPONENT_CLASS_INITIALIZE_METHOD_STATUS_MEMORY_ERROR;
		goto error;
	case BT_SELF_COMPONENT_ADD_PORT_STATUS_ERROR:
		/* FALLTHROUGH */
	default:
		status = BT_COMPONENT_CLASS_INITIALIZE_METHOD_STATUS_ERROR;
		goto error;
	}

	status = apply_params(jaeger, params);
	if (status != BT_COMPONENT_CLASS_INITIALIZE_METHOD_STATUS_OK) {

		goto error;
	}

	bt_self_component_set_data(self_comp, jaeger);

	status = BT_COMPONENT_CLASS_INITIALIZE_METHOD_STATUS_OK;
	goto end;

error:
	destroy_jaeger_data(jaeger);

end:
	return status;
}

BT_HIDDEN void
jaeger_finalize(bt_self_component_sink *comp)
{
	struct jaeger_component *self;

	if (comp == NULL) {

		return;
	}
	BT_ASSERT_DBG(comp != NULL);

	self = (struct jaeger_component *) bt_self_component_get_data(
	    bt_self_component_sink_as_self_component(comp));
	BT_ASSERT_DBG(self != NULL);

	destroy_jaeger_data(self);
}

BT_HIDDEN bt_component_class_sink_consume_method_status
jaeger_consume(bt_self_component_sink *comp)
{
	bt_component_class_sink_consume_method_status ret =
	    BT_COMPONENT_CLASS_SINK_CONSUME_METHOD_STATUS_OK;
	bt_message_array_const msgs;
	bt_message_iterator *it;
	struct jaeger_component *jaeger;
	bt_message_iterator_next_status next_status;
	uint64_t count = 0, i = 0;

	/* Verify the method's preconditions */
	BT_ASSERT(comp != NULL);

	jaeger = (struct jaeger_component *) bt_self_component_get_data(
	    bt_self_component_sink_as_self_component(comp));
	BT_ASSERT_DBG(jaeger);
	BT_ASSERT_DBG(jaeger->iterator != NULL);

	it = jaeger->iterator;
	BT_ASSERT_DBG(it != NULL);

	next_status = bt_message_iterator_next(it, &msgs, &count);
	switch (next_status) {
	case BT_MESSAGE_ITERATOR_NEXT_STATUS_OK:
		break;
	case BT_MESSAGE_ITERATOR_NEXT_STATUS_MEMORY_ERROR:
		ret = BT_COMPONENT_CLASS_SINK_CONSUME_METHOD_STATUS_MEMORY_ERROR;
		goto end;
	case BT_MESSAGE_ITERATOR_NEXT_STATUS_AGAIN:
		ret = BT_COMPONENT_CLASS_SINK_CONSUME_METHOD_STATUS_AGAIN;
		goto end;
	case BT_MESSAGE_ITERATOR_NEXT_STATUS_END:
		ret = BT_COMPONENT_CLASS_SINK_CONSUME_METHOD_STATUS_END;
		BT_MESSAGE_ITERATOR_PUT_REF_AND_RESET(
		    jaeger->iterator);
		goto end;
	default:
		ret = BT_COMPONENT_CLASS_SINK_CONSUME_METHOD_STATUS_ERROR;
		goto end;
	}

	BT_ASSERT_DBG(next_status == BT_MESSAGE_ITERATOR_NEXT_STATUS_OK);

	for (i = 0; i < count; i++) {

		if (handle_message(jaeger, msgs[i]) !=
		    BT_MESSAGE_ITERATOR_CLASS_NEXT_METHOD_STATUS_OK) {

			ret = BT_COMPONENT_CLASS_SINK_CONSUME_METHOD_STATUS_ERROR;
			goto end;
		}

		bt_message_put_ref(msgs[i]);
	}

end:
	/* Unreference any messages after any error occurs in
	 * handle_message() - freeing th ememory.
	 */
	for (; i < count; i++) {

		bt_message_put_ref(msgs[i]);
	}

	return ret;
}

BT_HIDDEN bt_component_class_sink_graph_is_configured_method_status
jaeger_graph_is_configured(bt_self_component_sink *comp)
{
	bt_component_class_sink_graph_is_configured_method_status status;
	bt_message_iterator_create_from_sink_component_status
		msg_iter_status;
	struct jaeger_component *jaeger;

	/* Verify the method's preconditions */
	BT_ASSERT(comp != NULL);

	jaeger = (struct jaeger_component *) bt_self_component_get_data(
	    bt_self_component_sink_as_self_component(comp));
	BT_ASSERT_DBG(jaeger != NULL);
	BT_ASSERT_DBG(jaeger->iterator != NULL);

	msg_iter_status = bt_message_iterator_create_from_sink_component(
	    comp, bt_self_component_sink_borrow_input_port_by_name(comp,
	    IN_PORT_NAME), &jaeger->iterator);
	switch (msg_iter_status) {
	case BT_MESSAGE_ITERATOR_CREATE_FROM_SINK_COMPONENT_STATUS_OK:
		status = BT_COMPONENT_CLASS_SINK_GRAPH_IS_CONFIGURED_METHOD_STATUS_OK;
		break;
	case BT_MESSAGE_ITERATOR_CREATE_FROM_SINK_COMPONENT_STATUS_MEMORY_ERROR:
		status = BT_COMPONENT_CLASS_SINK_GRAPH_IS_CONFIGURED_METHOD_STATUS_MEMORY_ERROR;
		break;
	case BT_MESSAGE_ITERATOR_CREATE_FROM_SINK_COMPONENT_STATUS_ERROR:
		/* FALLTHROUGH */
	default:
		status = BT_COMPONENT_CLASS_SINK_GRAPH_IS_CONFIGURED_METHOD_STATUS_ERROR;
		break;
	}
	return status;
}
