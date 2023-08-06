A file indexer to accelerate file searching
===========================================

The **ugrep-indexer** utility recursively indexes files to accelerate **ugrep**
recursive searches with **ugrep** option `--index`.

[**ugrep**](https://github.com/Genivia/ugrep) is an ultra-fast file searcher
that supports index-based searching as of v3.12.5.

Indexing-based search makes sense if you're doing a recursive search on a lot
of files.  Index-based searching is typically faster, except for pathelogical
cases when searching a few files with patterns that match a lot (see notes
below).  Index-based search is significantly faster on slow file systems or
when file system caching is ineffective.

A typical example of indexing and index-based search:




Index-based search is most effective when searching a lot of files and when
our regex patterns aren't matching too much, i.e. we want to limit the use of
unlimited repeats `*` and `+` and limit the use of Unicode character classes
when possible.  This reduces the **ugrep** start-up time (see notes below).

Indexing adds a hidden index file `._UG#_Store` to each directory indexed.
Files indexed are scanned (never changed!) by **ugrep-indexer** to generate
index files.

If any files or directories were updated, added or deleted after indexing, then
you can run **ugrep-indexer** again.  This incrementally updates all indexes.

Indexing *never follows symbolic links to directories*, because symbolically
linked directories may be located anywhere in a file system, or in another file
system, where we do not want to add index files.  You can still index symbolic
links to files with **ugrep-indexer** option `-S`.

Option `-v` (`--verbose`) displays the indexing progress and "noise" of each
file indexed.  Noise is a measure of *entropy* or *randomness* in the input.  A
higher level of noise means that indexing was less accurate in representing the
contents of a file.  For example, a file with random data is hard to index
accurately and will have a high level of noise.

Indexing is not a fast process (**ugrep-indexer** 0.9 is not multi-threaded
yet) and can take some time to complete.  When indexing completes,
**ugrep-indexer** displays the results of indexing.  The total size of the
indexes added and average indexing noise is also reported.

The **ugrep-indexer** understands "binary files", which can be skipped and not
indexed with **ugrep-indexer** option `-I` (`--ignore-binary`).  This is useful
when searching with **ugrep** option `-I` (`--ignore-binary`) to ignore binary
files.

The **ugrep-indexer** also supports .gitignore files (and similar), specified
with **ugrep-indexer** option `-X` (`--ignore-files`).  Ignored files and
directories will not be indexed to save file system space.  This works well
when searching for files with **ugrep** option `--ignore-files`.

Indexing can be aborted, for example with CTRL-C, which will not result in a
loss of search capability with **ugrep**, but will leave the directory
structure only partially indexed.  Option `-c` checks for the presence of the
indexed files and directories.  Run **ugrep-indexer** again to continue
indexing.

Indexes are deleted with **ugrep-indexer** option `-d`.

The **ugrep-indexer** has been extensively tested by comparing `ugrep --index`
search results to the "slow" non-indexed `ugrep` search results on thousands of
files with thousands of random search patterns.

Indexed-based search works with all **ugrep** options except with option `-v`
(`--invert-match`), `--filter`, `-P` (`--perl-regexp`) and `-Z` (`--fuzzy`).
Option `-c` (`--count`) with `--index` automatically enables `--min-count=1` to
skip all files with zero matches.

Regex patterns are converted internally by **ugrep** to hash tables for up to
the first 16 bytes of the regex patterns specified, possibly shorter in order
to reduce construction time.  Therefore, first characters of a regex pattern to
search are most critical to limit so-called false positive matches that will
slow down searching.

Examples
--------

Recursively and incrementally index all non-binary files showing progress:

    $ ugrep-indexer -I -v

Index all non-binary files, show progress, follow symbolic links to files (but
not to directories), and do not index files and directories matching the globs
in .gitignore:

    $ ugrep-indexer -I -v -S -X

Recursively force re-indexing of all non-binary files:

    $ ugrep-indexer -f -I

Recursively delete all hidden `._UG#_Store` index files to restore the
directory tree to non-indexed:

    $ ugrep-indexer -d

Increase search performance by increasing the indexing accuracy from 5
(default) to 7 at a cost of larger index files:

    $ ugrep-indexer -If7

What is indexing accuracy?
--------------------------

Indexing is a form of lossy compression.  The higher the indexing accuracy, the
faster **ugrep** search performance should be by skipping more files that do
not match.  A higher accuracy reduces noise (less lossy).  A high level of
noise causes **ugrep** to sometimes search indexed files that do not match.  We
call these "false positive matches".  Higher accuracy requires larger index
files. Normally we expect 4K or less indexing storage per file on average.  The
maximum is 64KB of index storage per file.

When searching indexed files, **ugrep** option `--stats` shows the search
statistics after the indexing-based search completed.  When many files are not
skipped from searching due to indexing noise (i.e. false positives), then a
higher accuracy helps to increase the effectiveness of indexing, which may
speed up searching.

Why is the start-up time of ugrep higher with option --index?
-------------------------------------------------------------

The start-up overhead of `ugrep --index` to construct indexing hash tables
depends on the regex patterns.  If a regex pattern is very "permissive", i.e.
matches a lot of possible patterns, then the start-up time of `ugrep --index`
significantly increases to compute hash tables.  This may happen when large
Unicode character classes and wildcards are used, especially with the unlimited
`*` and `+` repeats.  To find out how the start-up time increases, use option
`ugrep --index -r PATTERN /dev/null --stats=vm` to search /dev/null with your
PATTERN.
