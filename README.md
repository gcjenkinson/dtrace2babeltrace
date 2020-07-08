# Description

Experimental plugins for libbabeltrace that process trace records produced by dtrace(1) (note a minor change has been made to the dtrace record format).

# Dependencies

## thrift

## opentracing-cpp

## Jaegertracing-client-cpp

TODO this is really messy.

Copy `libyaml-cppd.so` to `/usr/lib/x68_64-linux-gnu` and add link `sudo ln -s /usr/lib/x86_64-linux-gnu/libyanl-cppd.so /usr/lib/x86_64-linux-gnu/libyaml-cppd.so.0.6`.

# Build

```
./autogen
./configure
make
sudo make install
```

The directory containing the source files of `libbabeltrace` can be specified through the environmental variable `BABELTRACE_SOURCE_DIR`; if this value is not explicitly set it is assumed to be `${HOME}/babletrace`.
