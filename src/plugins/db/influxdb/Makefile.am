SUBDIRS = influxdb-cxx

noinst_LTLIBRARIES = libbabeltrace2-plugin-db-influxdb.la

# influxdb plugin
libbabeltrace2_plugin_db_influxdb_la_SOURCES = \
	influx.cxx \
	influx.h \
	influxdb.h

libbabeltrace2_plugin_db_influxdb_la_CXXFLAGS = \
	$(AM_CXXFLAGS) \
	-std=c++17 \
	-fpermissive \
	-I$(BABELTRACE_SOURCE_DIR)/include \
	-I$(BABELTRACE_SOURCE_DIR)/src \
	-I$(BABELTRACE_SOURCE_DIR)/src/logging \
	-I/usr/include/glib-2.0 \
	-I/usr/lib/x86_64-linux-gnu/glib-2.0/include \
	-Wno-unknown-pragmas \
	-Wno-redundant-decls

libbabeltrace2_plugin_db_influxdb_la_LIBADD = \
	influxdb-cxx/src/libinfluxdb-cxx.la
