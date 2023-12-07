#!/bin/bash

# Generates man page man/ugrep-indexer.1 from ./ugrep-indexer --help
# Robert van Engelen, Genivia Inc. All rights reserved.

if [ "$#" = 1 ]
then

if [ -x src/ugrep-indexer ] 
then

echo
echo "Creating ugrep-indexer man page"
mkdir -p man
echo '.TH UGREP-INDEXER "1" "'`date '+%B %d, %Y'`'" "ugrep-indexer '$1'" "User Commands"' > man/ugrep-indexer.1
cat >> man/ugrep-indexer.1 << 'END'
.SH NAME
\fBugrep-indexer\fR -- file indexer to accelerate recursive searching
.SH SYNOPSIS
.B ugrep-indexer [\fIPATH\fR] [\fB-0\fR...\fB9\fR] [\fB-c\fR|\fB-d\fR|\fB-f\fR] [\fB-I\fR] [\fB-q\fR] [\fB-S\fR] [\fB-s\fR] [\fB-X\fR] [\fB-z\fR]
.SH DESCRIPTION
The \fBugrep-indexer\fR utility recursively indexes files to accelerate ugrep
recursive searches with \fBugrep\fR option \fB--index\fR.
.PP
An optional \fIPATH\fR may be specified to the root of the directory tree to
index.  The default is to recursively index the working directory tree.
.PP
Indexes are incrementally updated unless option \fB-f\fR or \fB--force\fR is
specified.
.PP
When option \fB-I\fR or \fB--ignore-binary\fR is specified, binary files are
ignored and not indexed.  Searching with `ugrep --index' still searches binary
files unless ugrep option \fB-I\fR or \fB--ignore-binary\fR is specified also.
.PP
Archives and compressed files are incrementally indexed only when option
\fB-z\fR or \fB--decompress\fR is specified.  Otherwise, archives and
compressed files are indexed as binary files, or are ignored with option
\fB-I\fR or \fB--ignore-binary\fR.
.PP
To create an indexing log file, specify option \fB-v\fR or \fB--verbose\fR and
redirect standard output to a log file.  All messages are sent to standard
output.
.PP
The following options are available:
END
src/ugrep-indexer --help \
| tail -n+24 \
| sed -e 's/\([^\\]\)\\/\1\\\\/g' \
| sed \
  -e '/^$/ d' \
  -e '/^    Long options may start/ d' \
  -e '/^    The ugrep-indexer/ d' \
  -e '/^    0      / d' \
  -e '/^    1      / d' \
  -e 's/^ \{5,\}//' \
  -e 's/^\.\([A-Za-z]\)/\\\&.\1/g' \
  -e $'s/^    \(.*\)$/.TP\\\n\\1/' \
  -e 's/\(--[+0-9A-Za-z_-]*\)/\\fB\1\\fR/g' \
  -e 's/\([^-0-9A-Za-z_]\)\(-[a-zA-Z0-9%.+?]\) \([A-Z]\{1,\}\)/\1\\fB\2\\fR \\fI\3\\fR/g' \
  -e 's/\([^-0-9A-Za-z_]\)\(-[a-zA-Z0-9%.+?]\)/\1\\fB\2\\fR/g' \
  -e 's/^\(-.\) \([!A-Z]\{1,\}\)/\\fB\1\\fR \\fI\2\\fR/g' \
  -e 's/^\(-.\)/\\fB\1\\fR/g' \
  -e 's/\[\([-A-Z]\{1,\}\),\]\[\([-A-Z]\{1,\}\)\]/[\\fI\1\\fR,][\\fI\2\\fR]/g' \
  -e 's/\[\([-A-Z]\{1,\}\)\]/[\\fI\1\\fR]/g' \
  -e 's/\[,\([-A-Z]\{1,\}\)\]/[,\\fI\1\\fR]/g' \
  -e 's/\[=\([-A-Z]\{1,\}\)\]/[=\\fI\1\\fR]/g' \
  -e 's/=\([-A-Z]\{1,\}\)/=\\fI\1\\fR/g' \
| sed -e 's/-/\\-/g' >> man/ugrep-indexer.1
cat >> man/ugrep-indexer.1 << 'END'
.SH "EXIT STATUS"
The \fBugrep-indexer\fR utility exits with one of the following values:
.IP 0
Indexes are up to date.
.IP 1
Indexing check \fB-c\fR detected missing and outdated index files.
.SH EXAMPLES
Recursively and incrementally index all non-binary files showing progress:
.IP
$ ugrep-indexer -I -v
.PP
Recursively and incrementally index all non-binary files, including non-binary
files stored in archives and in compressed files, showing progress:
.IP
$ ugrep-indexer -z -I -v
.PP
Incrementally index all non-binary files, including archives and compressed
files, show progress, follow symbolic links to files (but not to directories),
but do not index files and directories matching the globs in .gitignore:
.IP
$ ugrep-indexer -z -I -v -S -X
.PP
Force re-indexing of all non-binary files, including archives and compressed
files, follow symbolic links to files (but not to directories), but do not
index files and directories matching the globs in .gitignore:
.IP
$ ugrep-indexer -f -z -I -v -S -X
.PP
Same, but decrease index file storage to a minimum by decreasing indexing
accuracy from 5 (default) to 0:
.IP
$ ugrep-indexer -f -0 -z -I -v -S -X
.PP
Increase search performance by increasing the indexing accuracy from 5
(default) to 7 at a cost of larger index files:
.IP
$ ugrep-indexer -f7zIvSX
.PP
Recursively delete all hidden ._UG#_Store index files to restore the directory
tree to non-indexed:
.IP
$ ugrep-indexer -d
.SH BUGS
Report bugs at:
.IP
https://github.com/Genivia/ugrep-indexer/issues
.PP
.SH LICENSE
\fBugrep\fR and \fBugrep-indexer\fR are released under the BSD\-3 license.  All
parts of the software have reasonable copyright terms permitting free
redistribution.  This includes the ability to reuse all or parts of the ugrep
and ugrep-indexer source tree.
END

man man/ugrep-indexer.1 | sed 's/.//g' > man.txt

echo "ugrep-indexer $1 manual page created and saved in man/ugrep-indexer.1"
echo "ugrep-indexer text-only man page created and saved as man.txt"

else

echo "ugrep-indexer is needed but was not found: build ugrep-indexer first"
exit 1

fi

else

echo "Usage: ./man.sh 1.v.v"
exit 1

fi
