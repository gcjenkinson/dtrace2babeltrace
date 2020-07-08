Experimental plugins for libbabeltrace that process trace records produced by dtrace(1) (note a minor change has been made to the dtrace record format).

# Dependencies

TODO this is really messy.

# Build

```
./autogen
./configure
make
sudo make install
```

The directory containing the source files of `libbabeltrace` can be specified through the environmental variable `BABELTRACE_SOURCE_DIR`; if this value is not explicitly set it is assumed to be `${HOME}/babletrace`.
