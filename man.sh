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
.B ugrep-indexer [\fB-0\fR...\fB9\fR] [\fB-c\fR|\fB-d\fR|\fB-f\fR] [\fB-I\fR] [\fB-q\fR] [\fB-S\fR] [\fB-s\fR] [\fB-X\fR] [\fB-z\fR] [\fIPATH\fR]
.SH DESCRIPTION
The \fBugrep-indexer\fR utility recursively indexes files to accelerate
recursive searching with the \fBug --index\fR \fIPATTERN\fR commands:
.IP
$ \fBugrep-indexer\fR [\fB-I\fR] [\fB-z\fR]
.IP
  ...
.IP
$ \fBug\fR \fB--index\fR [\fB-I\fR] [\fB-z\fR] [\fB-r\fR|\fB-R\fR] \fIOPTIONS\fR \fIPATTERN\fR
.IP
$ \fBugrep\fR \fB--index\fR [\fB-I\fR] [\fB-z\fR] [\fB-r\fR|\fB-R\fR] \fIOPTIONS\fR \fIPATTERN\fR
.PP
where option \fB-I\fR or \fB--ignore-binary\fR ignores binary files, which is
recommended to limit indexing storage overhead and to reduce search time.
Option \fB-z\fR or \fB--decompress\fR indexes and searches archives and
compressed files.
.PP
Indexing speeds up searching file systems that are large and cold (not recently
cached in RAM) and file systems that are generally slow to search.  Note that
indexing may not speed up searching few files or recursively searching fast
file systems.
.PP
Searching with \fBug --index\fR is safe and never skips modified files that may
match after indexing; the \fBug --index\fR \fIPATTERN\fR command always
searches files and directories that were added or modified after indexing.
When option \fB--stats\fR is used with \fBug --index\fR, a search report is
produced showing the number of files skipped not matching any indexes and the
number of files and directories that were added or modified after indexing.
Note that searching with \fBug --index\fR may significantly increase the
start-up time when complex regex patterns are specified that contain large
Unicode character classes combined with `*' or `+' repeats, which should be
avoided.
.PP
\fBugrep-indexer\fR stores a hidden index file in each directory indexed.  The
size of an index file depends on the number of files indexed and the specified
indexing accuracy.  Higher accuracy produces larger index files to improve
search performance by reducing false positives (a false positive is a match
prediction for a file when the file does not match the regex pattern.)
.PP
\fBugrep-indexer\fR accepts an optional \fIPATH\fR to the root of the directory
tree to index.  The default is to index the working directory tree.
.PP
\fBugrep-indexer\fR incrementally updates indexes.  To force reindexing,
specify option \fB-f\fR or \fB--force\fR.  Indexes are deleted with option
\fB-d\fR or \fB--delete\fR.
.PP
\fBugrep-indexer\fR may be stopped and restarted to continue indexing at any
time.  Incomplete index files do not cause errors.
.PP
ASCII, UTF-8, UTF-16 and UTF-32 files are indexed and searched as text files
unless their UTF encoding is invalid.  Files with other encodings are indexed
as binary files and can be searched with non-Unicode regex patterns using
\fBug --index \fB-U\fR.
.PP
When \fBugrep-indexer\fR option \fB-I\fR or \fB--ignore-binary\fR is specified,
binary files are ignored and not indexed.  Avoid searching these non-indexed
binary files with \fBug --index -I\fR using option \fB-I\fR.
.PP
\fBugrep-indexer\fR option \fB-X\fR or \fB--ignore-files\fR respects gitignore
rules.  Likewise, avoid searching non-indexed ignored files with \fBug --index
--ignore-files\fR using option \fB--ignore-files\fR.
.PP
Archives and compressed files are indexed with \fBugrep-indexer\fR option
\fB-z\fR or \fB--decompress\fR.  Otherwise, archives and compressed files are
indexed as binary files or are ignored with option \fB-I\fR or
\fB--ignore-binary\fR.  Note that once an archive or compressed file is indexed
as a binary file, it will not be reindexed with option \fB-z\fR to index the
contents of the archive or compressed file.  Only files that are modified after
indexing are reindexed, which is determined by comparing time stamps.
.PP
Symlinked files are indexed with \fBugrep-indexer\fR option \fB-S\fR or
\fB--dereference-files\fR.  Symlinks to directories are never followed.  
.PP
To save a log file of the indexing process, specify option \fB-v\fR or
\fB--verbose\fR and redirect standard output to a log file.  All messages and
warnings are sent to standard output and captured by the log file.
.PP
A .ugrep-indexer configuration file with configuration options is loaded when
present in the working directory or in the home directory.  A configuration
option consists of the name of a long option and its argument when applicable.
.PP
The following options are available:
END
src/ugrep-indexer --help \
| tail -n+28 \
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
accuracy from 4 (the default) to 0:
.IP
$ ugrep-indexer -f -0 -z -I -v -S -X
.PP
Increase search performance by increasing the indexing accuracy from 4
(the default) to 7 at a cost of larger index files:
.IP
$ ugrep-indexer -f7zIvSX
.PP
Recursively delete all hidden ._UG#_Store index files to restore the directory
tree to non-indexed:
.IP
$ ugrep-indexer -d
.SH COPYRIGHT
Copyright (c) 2021-2024 Robert A. van Engelen <engelen@acm.org>
.PP
\fBugrep-indexer\fR is released under the BSD\-3 license.  All parts of the
software have reasonable copyright terms permitting free redistribution.  This
includes the ability to reuse all or parts of the ugrep source tree.
.SH BUGS
Report bugs at:
.IP
https://github.com/Genivia/ugrep-indexer/issues
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
