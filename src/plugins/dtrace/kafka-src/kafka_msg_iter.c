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

#include <stdbool.h>

#define BT_COMP_LOG_SELF_COMP self_comp
#define BT_LOG_OUTPUT_LEVEL log_level
#define BT_LOG_TAG "PLUGIN/SRC.DTRACE.KAFKA/MSG-ITER"
#include "logging/comp-logging.h"

#include "kafka_metadata.h"
#include "kafka_stream_iter.h"
#include "kafka_trace.h"

struct kafka_msg_iter {
	bt_logging_level log_level;
	bt_self_component *self_comp; /* Borrowed ref. */
	struct kafka_component *kafka_comp; /* Borrowed ref. */
	bt_self_message_iterator *self_msg_iter; /* Weak reference. */
	rd_kafka_t *consumer; /* Owned by this. */
	struct kafka_trace *trace;
	struct kafka_metadata *metadata;
	bool was_interrupted;
};

struct kafka_msg_iter_cb_data {
    	bt_message_array_const msgs;
	uint64_t capacity;
	uint64_t count;
};

static void
kafka_msg_iter_callback(struct kafka_stream_iter *stream_iter, void *arg)
{
	const bt_message *message;
	enum ctf_msg_iter_status msgstatus;
	struct kafka_msg_iter_cb_data *cb_data =
	    (struct kafka_msg_iter_cb_data *) arg;
	
	/* Validate the method's preconditions */
	BT_ASSERT(stream_iter != NULL);
	BT_ASSERT(arg != NULL);

	while (cb_data->count < cb_data->capacity) {

		msgstatus = ctf_msg_iter_get_next_message(
		    kafka_stream_iter_get_ctf_msg_iter(stream_iter), &message);
		switch (msgstatus) {
		case CTF_MSG_ITER_STATUS_OK:

			/*
			* Insert the next message to the message batch. This will set
			* stream iterator current messsage to NULL so that next time
			* we fetch the next message of that stream iterator
			*/
			BT_MESSAGE_MOVE_REF(cb_data->msgs[cb_data->count], message);
			/* Return the count of messages */
			cb_data->count++;
			break;
		case CTF_MSG_ITER_STATUS_AGAIN:
			/* FALLTHROUGH */
		case CTF_MSG_ITER_STATUS_ERROR:
			/* FALLTHROUGH */
		case CTF_MSG_ITER_STATUS_EOF:
			/* FALLTHROUGH */
		default:
			return;
		}
	}
}

static void
kafka_msg_iter_destroy(struct kafka_msg_iter * const self)
{
	rd_kafka_resp_err_t err;
	bt_logging_level log_level = self->log_level;
	bt_self_component *self_comp = self->self_comp;

	if (self == NULL) {

		return;
	}
	BT_ASSERT_DBG(self != NULL);

	/* Close and destroy the Kafka consumer. */
	BT_ASSERT_DBG(self->consumer != NULL);
	err = rd_kafka_consumer_close(self->consumer);
	if (err != RD_KAFKA_RESP_ERR_NO_ERROR) { 

		BT_COMP_LOGE("Failed to close Kafka consumer: %s\n",
		    rd_kafka_err2str(err));
	} else {

		BT_COMP_LOGD("Closed Kafka consumer\n");
	}
		
	rd_kafka_destroy(self->consumer);
	
	/* All stream iterators must be destroyed at this point. */
	BT_ASSERT_DBG(self->kafka_comp != NULL);
	BT_ASSERT_DBG(kafka_component_has_msg_iter(self->kafka_comp));
	kafka_component_set_has_msg_iter(self->kafka_comp, false);

	g_free(self);
	return;
}

BT_HIDDEN bt_message_iterator_class_initialize_method_status
kafka_msg_iter_init(bt_self_message_iterator *self_msg_it,
    bt_self_message_iterator_configuration *config,
    bt_self_component_port_output *self_port)
{
	bt_component_class_initialize_method_status ret =
	    BT_COMPONENT_CLASS_INITIALIZE_METHOD_STATUS_OK;
	bt_logging_level log_level;
	bt_self_component *self_comp;
	struct kafka_component *kafka;
	struct kafka_msg_iter *kafka_msg_iter;
	rd_kafka_conf_t *conf;
	rd_kafka_resp_err_t err;
	char errstr[256];
	rd_kafka_topic_partition_list_t *subscription;
	int rc;
	char *buf;
	size_t buf_sz;

	/* Validate the method's preconditions */
	BT_ASSERT(self_msg_it != NULL);
	BT_ASSERT(config != NULL);
	BT_ASSERT(self_port != NULL);

	/* Extract the kafka_component from the bt component */
	self_comp = bt_self_message_iterator_borrow_component(self_msg_it);

	BT_ASSERT_DBG(self_comp != NULL);
	kafka = bt_self_component_get_data(self_comp);

	BT_ASSERT_DBG(kafka != NULL);
	self_comp = kafka_component_get_self_comp(kafka);

	BT_ASSERT_DBG(self_comp != NULL);
	log_level = kafka_component_get_logging_level(kafka);

	/* There can be only one downstream iterator at the same time. */
	BT_ASSERT_DBG(!kafka_component_has_msg_iter(kafka));
	kafka_component_set_has_msg_iter(kafka, true);

	kafka_msg_iter = g_new0(struct kafka_msg_iter, 1);
	if (kafka == NULL) {

		BT_COMP_LOGE("Failed setting Kafka consumer poll\n");
		ret = BT_COMPONENT_CLASS_INITIALIZE_METHOD_STATUS_ERROR;
		goto end;
	}

	kafka_msg_iter->log_level = log_level;
	kafka_msg_iter->self_comp = self_comp;
	kafka_msg_iter->kafka_comp = kafka;
	kafka_msg_iter->self_msg_iter = self_msg_it;

	conf = rd_kafka_conf_new();
	if (conf == NULL) {

		BT_COMP_LOGE("Failed creating Kafka conf\n");
		ret = BT_COMPONENT_CLASS_INITIALIZE_METHOD_STATUS_ERROR;
		goto error;
	}

	if (rd_kafka_conf_set(conf, "bootstrap.servers",
	    kafka_component_get_bootstrap_servers(kafka), errstr,
	    sizeof(errstr)) != RD_KAFKA_CONF_OK) {

		BT_COMP_LOGE("Failed setting Kafka conf\n");
		rd_kafka_conf_destroy(conf);
		ret = BT_COMPONENT_CLASS_INITIALIZE_METHOD_STATUS_ERROR;
		goto error;
	}

	if (rd_kafka_conf_set(conf, "compression.codec", "gzip",
	    errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK) {

		BT_COMP_LOGE("Failed setting Kafka conf\n");
		rd_kafka_conf_destroy(conf);
		ret = BT_COMPONENT_CLASS_INITIALIZE_METHOD_STATUS_ERROR;
		goto error;
	}
	
	if (rd_kafka_conf_set(conf, "group.id",
	    kafka_component_get_group_id(kafka),
	    errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK) {

		BT_COMP_LOGE("Failed setting Kafka conf\n");
		rd_kafka_conf_destroy(conf);
		ret = BT_COMPONENT_CLASS_INITIALIZE_METHOD_STATUS_ERROR;
		goto error;
	}
	
	if (rd_kafka_conf_set(conf, "auto.offset.reset", "earliest",
	    errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK) {

		BT_COMP_LOGE("Failed setting Kafka conf\n");
		rd_kafka_conf_destroy(conf);
		goto error;
	}
	
	if (rd_kafka_conf_set(conf, "linger.ms", "10",
	    errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK) {

		BT_COMP_LOGE("Failed setting Kafka conf\n");
		rd_kafka_conf_destroy(conf);
		ret = BT_COMPONENT_CLASS_INITIALIZE_METHOD_STATUS_ERROR;
		goto error;
	}

	buf_sz = snprintf(NULL, 0, "%zu", kafka_get_max_request_sz(kafka));
	buf = alloca(buf_sz + 1);
	snprintf(buf, buf_sz + 1, "%zu", kafka_get_max_request_sz(kafka));
	BT_COMP_LOGI("receive.message.max.bytes = %s\n", buf);

	if (rd_kafka_conf_set(conf, "receive.message.max.bytes", buf,
	    errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK) {

		BT_COMP_LOGE("Failed setting Kafka conf\n");
		rd_kafka_conf_destroy(conf);
		ret = BT_COMPONENT_CLASS_INITIALIZE_METHOD_STATUS_ERROR;
		goto error;
	}

	kafka_msg_iter->consumer = rd_kafka_new(RD_KAFKA_CONSUMER, conf,
	    errstr, sizeof(errstr));
	if (kafka_msg_iter->consumer == NULL) {

		BT_COMP_LOGE("Failed creating Kafka consumer: %s\n",
		    errstr);
		rd_kafka_conf_destroy(conf);
		ret = BT_COMPONENT_CLASS_INITIALIZE_METHOD_STATUS_ERROR;
		goto error;
	}

	/* `conf` is owned by the consumer instance, and is
	 * freed on the consumer destroy.
	 */
	conf = NULL;

	err = rd_kafka_poll_set_consumer(kafka_msg_iter->consumer);
	if (err != RD_KAFKA_RESP_ERR_NO_ERROR) {

		BT_COMP_LOGE("Failed setting Kafka consumer poll\n");
		ret = BT_COMPONENT_CLASS_INITIALIZE_METHOD_STATUS_ERROR;
		goto error;
	}

	subscription = rd_kafka_topic_partition_list_new(1);
	if (subscription == NULL) {

		BT_COMP_LOGE("Failed creating Kafka topic/parition list\n");
		ret = BT_COMPONENT_CLASS_INITIALIZE_METHOD_STATUS_ERROR;
		goto error;
	}

	if (rd_kafka_topic_partition_list_add(subscription,
	    kafka_component_get_topic(kafka),
	    RD_KAFKA_PARTITION_UA) == NULL) {

		BT_COMP_LOGE("Failed adding Kafka topic/partition (%s) offset\n",
		    kafka_component_get_topic(kafka));
		ret = BT_COMPONENT_CLASS_INITIALIZE_METHOD_STATUS_ERROR;
		goto error;
	}

	if (rd_kafka_topic_partition_list_set_offset(subscription,
	    kafka_component_get_topic(kafka),
	    RD_KAFKA_PARTITION_UA, RD_KAFKA_OFFSET_END) !=RD_KAFKA_RESP_ERR_NO_ERROR) {


		BT_COMP_LOGE("Failed setting Kafka topic/partition (%s) offset\n",
		    kafka_component_get_topic(kafka));
		ret = BT_COMPONENT_CLASS_INITIALIZE_METHOD_STATUS_ERROR;
		goto error;
	}

	if (rd_kafka_subscribe(kafka_msg_iter_get_consumer(kafka_msg_iter),
	    subscription) !=RD_KAFKA_RESP_ERR_NO_ERROR) {

		BT_COMP_LOGE("Failed subscribing Kafka consumer poll\n");
		ret = BT_COMPONENT_CLASS_INITIALIZE_METHOD_STATUS_ERROR;
		goto error;
	}	

	rd_kafka_topic_partition_list_destroy(subscription);
	
	rc = kafka_metadata_create(&kafka_msg_iter->metadata, log_level,
	    self_comp);
	if (rc != 0) {

		BT_COMP_LOGE("Failed creating Kafka metadata\n");
		ret = BT_COMPONENT_CLASS_INITIALIZE_METHOD_STATUS_ERROR;
		goto error;
	}

	kafka_msg_iter->trace = kafka_create_trace(kafka_msg_iter,
	    kafka_msg_iter->log_level, kafka_msg_iter->self_comp,
	    kafka_msg_iter->metadata, 0);
	if (kafka_msg_iter->trace == NULL) {

		BT_COMP_LOGE("Failed creating Kafka trace\n");
		ret = BT_COMPONENT_CLASS_INITIALIZE_METHOD_STATUS_ERROR;
		goto error;
	}

	kafka_trace_set_metadata(kafka_msg_iter->trace, kafka_msg_iter->metadata);

	bt_self_message_iterator_set_data(self_msg_it, kafka_msg_iter);

	goto end;

error:
	BT_COMP_LOGE("Failed to instatiate kafka_msg_iter\n");
	ret = BT_COMPONENT_CLASS_INITIALIZE_METHOD_STATUS_ERROR;
	kafka_msg_iter_destroy(kafka_msg_iter);

end:
	return ret;
}

BT_HIDDEN bt_message_iterator_class_next_method_status
kafka_msg_iter_next(bt_self_message_iterator *self_msg_it,
    bt_message_array_const msgs, uint64_t capacity, uint64_t *count)
{
	struct kafka_msg_iter *kafka_msg_iter;
	bt_self_component *self_comp;
	bt_logging_level log_level;
	struct kafka_msg_iter_cb_data cb_data;

	kafka_msg_iter = bt_self_message_iterator_get_data(self_msg_it);
	BT_ASSERT_DBG(kafka_msg_iter);

	self_comp = kafka_msg_iter->self_comp;
	log_level = kafka_msg_iter->log_level;
		
	if (G_UNLIKELY(kafka_msg_iter->was_interrupted)) {

		return BT_MESSAGE_ITERATOR_CLASS_NEXT_METHOD_STATUS_ERROR;
	}

	/*
	 * Clear all the invalid message reference that might be left over in
	 * the output array.
	 */
	memset(msgs, 0, capacity * sizeof(*msgs));
	*count = 0;

	if (kafka_trace_is_new_metadata_needed(kafka_msg_iter->trace)){	
	
		enum kafka_iterator_status status;
	
		status = kafka_metadata_update(kafka_msg_iter->trace);
		switch (status) {
		case KAFKA_ITERATOR_STATUS_OK:
			kafka_lazy_msg_init(kafka_msg_iter->trace,
		    	    kafka_msg_iter->self_msg_iter);
			return BT_MESSAGE_ITERATOR_CLASS_NEXT_METHOD_STATUS_AGAIN;
		case KAFKA_ITERATOR_STATUS_AGAIN:
			return BT_MESSAGE_ITERATOR_CLASS_NEXT_METHOD_STATUS_AGAIN;
		default:
			return BT_MESSAGE_ITERATOR_CLASS_NEXT_METHOD_STATUS_ERROR;
		}
	}

	/* Iterator over the stream_iterators reading messages */
	cb_data.msgs = msgs;
	cb_data.count = 0;
	cb_data.capacity = capacity;

	kafka_trace_foreach_stream_iter(kafka_msg_iter->trace,
		kafka_msg_iter_callback, &cb_data);

	/* Return the count of messages */
	*count = cb_data.count;
	if (*count == 0) {

		BT_COMP_LOGD("No messages returned\n");
		return BT_MESSAGE_ITERATOR_CLASS_NEXT_METHOD_STATUS_AGAIN;
	}

	BT_COMP_LOGI("%lu message returned\n", *count);
	return BT_MESSAGE_ITERATOR_CLASS_NEXT_METHOD_STATUS_OK;
}

BT_HIDDEN void
kafka_msg_iter_finalize(bt_self_message_iterator *self_msg_iter)
{
	struct kafka_msg_iter *kafka_msg_iter;

	if (self_msg_iter == NULL) {

		return;
	}

	BT_ASSERT(self_msg_iter != NULL);
	
	kafka_msg_iter = bt_self_message_iterator_get_data(self_msg_iter);
	BT_ASSERT(kafka_msg_iter);

	kafka_msg_iter_destroy(kafka_msg_iter);
}

BT_HIDDEN struct kafka_component * 
kafka_msg_iter_get_kafka_component(struct kafka_msg_iter *self)
{

	BT_ASSERT(self != NULL);
	return self->kafka_comp;
}

BT_HIDDEN rd_kafka_t *
kafka_msg_iter_get_consumer(struct kafka_msg_iter *self)
{

	BT_ASSERT(self != NULL);
	return self->consumer;
}

BT_HIDDEN bt_self_message_iterator *
kafka_msg_iter_get_self_msg_iter(struct kafka_msg_iter *self)
{

	BT_ASSERT(self != NULL);
	return self->self_msg_iter;
}

BT_HIDDEN void
kafka_msg_iter_set_was_interrupted(struct kafka_msg_iter *self)
{

	BT_ASSERT(self != NULL);
	self->was_interrupted = true;
}
