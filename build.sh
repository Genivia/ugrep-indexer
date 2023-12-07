#!/bin/bash

# help wanted?
case $1 in
  --help|-h)
    ./configure --help
    exit 0
    ;;
esac

echo
echo "Building ugrep-indexer..."

# configure with the specified command arguments

echo
echo "./configure $@"
echo

# appease automake when the original timestamps are lost, when using git clone
touch aclocal.m4 Makefile.am src/Makefile.am
sleep 1
touch config.h.in Makefile.in src/Makefile.in
sleep 1
touch configure

if ! ./configure "$@" ; then
echo "Failed to complete ./configure $@"
echo "See config.log for more details"
exit 1
fi

echo
echo "make -j clean all"
echo

make clean

if ! make -j ; then
echo "Failed to build ugrep-indexer: please run the following two commands:"
echo "$ autoreconf -fi"
echo "$ ./build.sh"
echo
echo "If that does not work, please open an issue at:"
echo "https://github.com/Genivia/ugrep-indexer/issues"
exit 1
fi

echo
echo "ugrep-indexer was successfully built in $(pwd)/bin:"
ls -l bin/ugrep-indexer
echo
echo "Copy bin/ugrep-indexer to a bin/ on your PATH"
echo
echo "Or install the ugrep-indexer utility on your system by executing:"
echo "sudo make install"
echo
