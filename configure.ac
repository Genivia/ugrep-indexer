AC_INIT([ugrep-indexer],[0.9])
AM_INIT_AUTOMAKE([foreign])
AC_CONFIG_HEADERS([config.h])
AC_COPYRIGHT([Copyright (C) 2023 Robert van Engelen, Genivia Inc.])

AC_CONFIG_MACRO_DIR([m4])

# if CXXFLAGS is undefined, set it to our preferred default flags
: ${CXXFLAGS="-Wall -Wextra -Wunused -O2"}
: ${CFLAGS="-Wall -Wextra -Wunused -O2"}

AC_LANG([C++])
AX_CXX_COMPILE_STDCXX_11([ext], [mandatory])

AC_HEADER_DIRENT
AC_STRUCT_DIRENT_D_INO
AC_STRUCT_DIRENT_D_TYPE

AC_CHECK_MEMBERS([struct stat.st_atim, struct stat.st_mtim, struct stat.st_ctim])
AC_CHECK_MEMBERS([struct stat.st_atimespec, struct stat.st_mtimespec, struct stat.st_ctimespec])

AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T

AC_PROG_CXX
AM_PROG_AR
AC_PROG_RANLIB
AC_PROG_INSTALL
AC_PROG_LN_S

AC_CANONICAL_HOST

PLATFORM=${host}
AC_SUBST(PLATFORM)

AX_CHECK_ZLIB([], [echo "zlib not found: ugrep-indexer option -z is not available"])

AX_CHECK_BZLIB([], [echo "libbz2 not found: ugrep-indexer option -z won't index .bz and .bz2 files"])

AX_CHECK_LZMALIB([], [echo "liblzma not found: ugrep-indexer option -z won't index .lzma and .xz files"])

AX_CHECK_LZ4LIB([], [echo "liblz4 not found: ugrep-indexer option -z won't index .lz4 files"])

AX_CHECK_ZSTDLIB([], [echo "libzstd not found: ugrep-indexer option -z won't index .zst files"])

AC_SUBST([PTHREAD_CFLAGS], [""])
AC_SUBST([PTHREAD_LIBS], ["-lpthread"])

AC_CONFIG_FILES([Makefile src/Makefile])

AC_OUTPUT
