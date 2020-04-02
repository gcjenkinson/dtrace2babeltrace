SUBDIRS = ctf-writer

libbabeltrace2_plugin_dtrace_kafka_la_SOURCES = \
		bbuf.c \
		bbuf.h \
		kafka.c \
		kafka.h \
		kafka_metadata.c \
		kafka_metadata.h \
		kafka_msg_iter.c \
		kafka_msg_iter.h \
		kafka_stream_iter.c \
		kafka_stream_iter.h \
		kafka_trace.c \
		kafka_trace.h \
		dof.c \
		dof.h \
		dof2ctf.c \
		dof2ctf.h 

libbabeltrace2_plugin_dtrace_kafka_la_CFLAGS = \
	$(AM_CFLAGS) \
	-I$(BABELTRACE_SOURCE_DIR)/include \
	-I$(BABELTRACE_SOURCE_DIR)/src \
	-I$(BABELTRACE_SOURCE_DIR)/src/logging \
	-I$(BABELTRACE_SOURCE_DIR)/src/plugins/ctf \
	-I/usr/include/sys \
	-I/usr/include/glib-2.0 \
	-I/usr/lib/x86_64-linux-gnu/glib-2.0/include \
	-Wno-unknown-pragmas \
	-Wno-error=redundant-decls

libbabeltrace2_plugin_dtrace_kafka_la_LIBADD = \
	ctf-writer/libbabeltrace2-plugin-ctf-writer.la \
	-lrdkafka \
	-lsbuf

noinst_LTLIBRARIES = libbabeltrace2-plugin-dtrace-kafka.la
