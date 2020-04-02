SUBDIRS = influxdb

plugindir = "$(BABELTRACE_PLUGINS_DIR)"
plugin_LTLIBRARIES = babeltrace-plugin-db.la

babeltrace_plugin_db_la_SOURCES = plugin.c

babeltrace_plugin_db_la_CFLAGS = \
	$(AM_CFLAGS) \
	-I$(BABELTRACE_SOURCE_DIR)/include \
	-I$(BABELTRACE_SOURCE_DIR)/src \
	-I/usr/include/glib-2.0 \
	-I/usr/lib/x86_64-linux-gnu/glib-2.0/include

babeltrace_plugin_db_la_LDFLAGS = \
	$(LT_NO_UNDEFINED) \
	-avoid-version -module

babeltrace_plugin_db_la_LIBADD = \
	influxdb/libbabeltrace2-plugin-db-influxdb.la

babeltrace_plugin_db_la_LIBADD += \
	$(BABELTRACE_SOURCE_DIR)/src/lib/libbabeltrace2.la \
	$(BABELTRACE_SOURCE_DIR)/src/common/libbabeltrace2-common.la \
	$(BABELTRACE_SOURCE_DIR)/src/logging/libbabeltrace2-logging.la \
	$(BABELTRACE_SOURCE_DIR)/src/compat/libcompat.la \
	$(BABELTRACE_SOURCE_DIR)/src/plugins/common/param-validation/libbabeltrace2-param-validation.la
