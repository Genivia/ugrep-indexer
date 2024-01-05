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

AX_CHECK_ZLIB([], [echo "recommended zlib not found: ugrep-indexer option -z is disabled"])

if test -n "${zlib_places}"; then

AX_CHECK_BZ2LIB([], [echo "recommended libbz2 not found: ugrep-indexer option -z cannot index .bz and .bz2 files"])

AX_CHECK_LZMALIB([], [echo "recommended liblzma not found: ugrep-indexer option -z cannot index .lzma and .xz files"])

AX_CHECK_LZ4LIB([], [echo "optional liblz4 not found: install if you want ugrep-indexer option -z to index .lz4 files"])

AX_CHECK_ZSTDLIB([], [echo "optional libzstd not found: install if you want ugrep-indexer option -z to index .zst files"])

AX_CHECK_BROTLILIB([], [echo "optional brotli library not found: install if you want ugrep-indexer option -z to index .br files"])

AC_ARG_ENABLE(7zip,
  [AS_HELP_STRING([--disable-7zip],
                  [to disable 7zip and no longer index .7z files])],
  [with_no_7zip="yes"],
  [with_no_7zip="no"])
AC_MSG_CHECKING(for --disable-7zip)
if test "x$with_no_7zip" = "xno"; then
  AC_MSG_RESULT(no)
  CPPFLAGS="$CPPFLAGS -I../lzma/C"
  LDFLAGS="$LDFLAGS -L../lzma/C"
  LIBS="-lviiz $LIBS"
else
  EXTRA_CFLAGS="-DWITH_NO_7ZIP ${EXTRA_CFLAGS}"
  AC_MSG_RESULT(yes)
fi

AC_ARG_WITH(bzip3,
  [AS_HELP_STRING([--with-bzip3],
                  [to enable bzip3 library to decompress .bz3 files])],
  [with_bzip3_library="$withval"],
  [with_bzip3_library=""])
if test "x$with_bzip3_library" != "x"; then
  if test "x$with_bzip3" = "xyes"; then
    unset with_bzip3
  fi
  AX_CHECK_BZIP3LIB([], [echo "optional libbzip3 not found: install if you want ugrep-indexer option -z to index .bz3 files"])
fi

fi

AC_SUBST(EXTRA_CFLAGS)
AC_SUBST([PTHREAD_CFLAGS], [""])
AC_SUBST([PTHREAD_LIBS], ["-lpthread"])

AC_CONFIG_FILES([Makefile src/Makefile lzma/C/Makefile])

AC_OUTPUT
