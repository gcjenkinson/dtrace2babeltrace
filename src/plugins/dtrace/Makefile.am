SUBDIRS = kafka-src

plugindir = "$(BABELTRACE_PLUGINS_DIR)"
plugin_LTLIBRARIES = babeltrace-plugin-dtrace.la

# dtrace plugin
babeltrace_plugin_dtrace_la_SOURCES = plugin.c

babeltrace_plugin_dtrace_la_CFLAGS = \
	$(AM_CFLAGS) \
	-I$(BABELTRACE_SOURCE_DIR)/src \
	-I$(BABELTRACE_SOURCE_DIR)/src/plugins/ctf \
	-I/usr/include/glib-2.0 \
	-I/usr/lib/x86_64-linux-gnu/glib-2.0/include
	
babeltrace_plugin_dtrace_la_LDFLAGS = \
	$(LT_NO_UNDEFINED) \
	-avoid-version -module

babeltrace_plugin_dtrace_la_LIBADD = \
	kafka-src/libbabeltrace2-plugin-dtrace-kafka.la

#if !ENABLE_BUILT_IN_PLUGINS
babeltrace_plugin_dtrace_la_LIBADD += \
	$(BABELTRACE_SOURCE_DIR)/src/plugins/common/muxing/libbabeltrace2-plugins-common-muxing.la \
	$(BABELTRACE_SOURCE_DIR)/src/lib/libbabeltrace2.la \
	$(BABELTRACE_SOURCE_DIR)/src/logging/libbabeltrace2-logging.la \
	$(BABELTRACE_SOURCE_DIR)/src/common/libbabeltrace2-common.la \
	$(BABELTRACE_SOURCE_DIR)/src/ctfser/libbabeltrace2-ctfser.la \
	$(BABELTRACE_SOURCE_DIR)/src/plugins/common/param-validation/libbabeltrace2-param-validation.la \
	$(BABELTRACE_SOURCE_DIR)/src/plugins/ctf/common/libbabeltrace2-plugin-ctf-common.la
#endif

#if BABELTRACE_BUILD_WITH_MINGW
#babeltrace_plugin_dtrace_la_LIBADD += -lws2_32
#endif

