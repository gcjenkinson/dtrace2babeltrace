noinst_LTLIBRARIES = libinfluxdb-cxx.la

# influxdb-cxx library
libinfluxdb_cxx_la_SOURCES = \
	HTTP.cxx \
	HTTP.h \
	InfluxDB.cxx \
	InfluxDB.h \
	InfluxDBFactory.cxx \
	InfluxDBFactory.h \
	Point.cxx \
	UDP.cxx \
	UDP.h \
	UnixSocket.cxx \
	UnixSocket.h \
	UriParser.h

libinfluxdb_cxx_la_CXXFLAGS = \
	$(AM_CXXFLAGS) \
	-DINFLUXDB_WITH_BOOST \
	-std=gnu++17 \
	-I../include \
	-Wno-unknown-pragmas \
	-Wno-redundant-decls

libinfluxdb_cxx_la_LDFLAGS = \
	-L/usr/lib/gcc/x86_64-linux-gnu/7

libinfluxdb_cxx_la_LIBADD = \
	-lstdc++ \
	-lboost_system \
	-lcurl
