#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([dtrace2babletrace], [0.01], [gcj21@cl.cam.ac.uk])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CC
AC_PROG_CXX
AM_PROG_AR

# Checks for libraries.

# Checks for header files.
AC_CHECK_HEADERS([stddef.h stdint.h strings.h sys/param.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_INLINE
AC_TYPE_INT16_T
AC_TYPE_INT32_T
AC_TYPE_INT64_T
AC_TYPE_INT8_T
AC_TYPE_SIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T

# User variables

# BABELTRACE_SOURCE_DIR: Babeltrace source directory
AC_ARG_VAR([BABELTRACE_SOURCE_DIR], [babeltrace source directory [~/babeltrace]])
AS_IF([test "x$BABELTRACE_SOURCE_DIR" = x], [BABELTRACE_SOURCE_DIR='~/babeltrace'])

# BABELTRACE_PLUGINS_DIR: Plugins directory
AC_ARG_VAR([BABELTRACE_PLUGINS_DIR], [built-in plugins install directory [LIBDIR/babeltrace2/plugins]])
AS_IF([test "x$BABELTRACE_PLUGINS_DIR" = x], [BABELTRACE_PLUGINS_DIR='${libdir}/babeltrace2/plugins'])

AM_INIT_AUTOMAKE([-Wall -Werror foreign subdir-objects])
AC_CONFIG_FILES([Makefile
	src/Makefile
	src/plugins/Makefile
	src/plugins/db/Makefile
	src/plugins/db/influxdb/Makefile
	src/plugins/db/influxdb/influxdb-cxx/Makefile
	src/plugins/db/influxdb/influxdb-cxx/src/Makefile
	src/plugins/dtrace/Makefile
	src/plugins/dtrace/kafka-src/Makefile
	src/plugins/dtrace/kafka-src/ctf-writer/Makefile])
LT_INIT([shared disable-static])

# Checks for library functions.

AC_OUTPUT
