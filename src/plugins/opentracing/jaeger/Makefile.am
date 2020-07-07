noinst_LTLIBRARIES = libbabeltrace2-plugin-opentracing-jaeger.la

libbabeltrace2_plugin_opentracing_jaeger_la_SOURCES = \
	jaegertracing.cxx \
	jaegertracing.h

libbabeltrace2_plugin_opentracing_jaeger_la_CXXFLAGS = \
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
