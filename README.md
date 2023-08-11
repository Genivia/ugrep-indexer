A monotonic file indexer to accelerate grepping
===============================================

The *ugrep-indexer* utility recursively indexes files to accelerate recursive
grepping.

*Note: this is a 0.9 beta version of a new generation of "monotonic indexers",
subject to change and improvements based on experiments and user feedback.
Regardless, this implementation has been extensively tested for correctness.
Additional features and performance improvements are planned.*

[ugrep](https://github.com/Genivia/ugrep) is a grep-compatible ultra fast file
searcher that supports index-based searching as of v3.12.5.

Index-based search can be significantly faster on slow file systems and when
file system caching is ineffective: if the file system on a drive searched is
not cached in RAM, then indexing will speed up search.  Therefore, it helps to
speed up searching files that weren't recently accessed.  On the other hand, if
files are already cached in RAM, because files were read recently, then
indexing will not necesarily speed up search, obviously.  See also Q&A below.

If any files and directories were changed after indexing, then ugrep `--index`
will always search these additions and changes made to the file system by
comparing file and directory time stamps.  If many files were added or changed,
then simply re-index to bring the indexing up to date.  Re-indexing is
incremental, so it will not take as much time as the initial indexing process.

A typical example of an index-based search, e.g. on the ugrep repository:

    $ cd drive/ugrep
    $ ugrep-indexer -I

    12245871 bytes scanned and indexed with 19% noise on average
        1317 files indexed in 28 directories
           0 new directories indexed
        1317 new files indexed
           0 modified files indexed
           0 deleted files removed from indexes
         128 binary files skipped with --ignore-binary
           0 symbolic links skipped
           0 devices skipped
     5588843 bytes indexing storage increase at 4243 bytes/file

Normal searching without indexing takes 1.07 seconds after unmounting the
`drive` and mounting again to clear FS cache to see the effect of indexing:

    $ cd drive/ugrep
    $ ugrep -I -l 'std::chrono' --stats
    src/ugrep.cpp

    Searched 1317 files in 28 directories in 1.07 seconds with 8 threads: 1 matching (0.07593%)

With indexing, searching only takes 0.109 seconds, which is 10 times faster,
after unmounting `drive` and mounting again to clear FS cache to see the effect
of indexing:

    $ cd drive/ugrep
    $ ugrep --index -I -l 'std::chrono' --stats
    src/ugrep.cpp

    Searched 1317 files in 28 directories in 0.109 seconds with 8 threads: 1 matching (0.07593%)
    Skipped 1316 of 1317 files with indexes not matching any search patterns

Speed increases to recursively search file systems for regex pattern matches
may be significantly higher in general compared to the 10x for this small demo,
depending on several factors, the size of the files indexed and the read speed
of the file system.

The indexing algorithm that I designed is *provably monotonic*: a higher
accuracy guarantees an increased search performance by reducing the false
positive rate, but also increases index storage overhead.  Likewise, a lower
accuracy decreases search performance, but also reduces the index storage
overhead.  Therefore, I call this indexer a *monotonic indexer*.

If file storage space is at a premium, you can dial down the index storage
overhead by specifying a lower indexing accuracy.

Indexing the example from above with level 0 (option `-0`) reduces the indexing
storage overhead 8.6 times, from 4243 bytes per file to only 490 bytes per file:

    12245871 bytes scanned and indexed with 42% noise on average
        1317 files indexed in 28 directories
           0 new directories indexed
        1317 new files indexed
           0 modified files indexed
           0 deleted files removed from indexes
         128 binary files skipped with --ignore-binary
           0 symbolic links skipped
           0 devices skipped
      646123 bytes indexing storage increase at 490 bytes/file

Indexed search is still a lot faster than non-indexed search by 7.7x for this
example instead of 10x, with 16 files actually searched instead of just one:

    Searched 1317 files in 28 directories in 0.139 seconds with 8 threads: 1 matching (0.07593%)
    Skipped 1301 of 1317 files with indexes not matching any search patterns

On the other hand, regex patterns that are more complex than this example may
have a higher false positive rate, which is the rate of files that are
considered possibly matching when they are not.  A higher false positive rate
may reduce search speeds.

Index-based search is most effective when searching a lot of files and when
your regex patterns aren't matching too much, i.e. we want to limit the use of
unlimited repeats `*` and `+` and limit the use of Unicode character classes
when possible.  This reduces the ugrep start-up time and limits the rate of
false positive pattern matches (see Q&A below).

Quick examples
--------------

Recursively and incrementally index all non-binary files showing progress:

    ugrep-indexer -I -v

Index all non-binary files, show progress, follow symbolic links to files (but
not to directories), and do not index files and directories matching the globs
in .gitignore:

    ugrep-indexer -I -v -S -X

Recursively force re-indexing of all non-binary files:

    ugrep-indexer -f -I

Recursively delete all hidden `._UG#_Store` index files to restore the
directory tree to non-indexed:

    ugrep-indexer -d

Decrease index file storage to a minimum by decreasing indexing accuracy from 5
(default) to 0:

    ugrep-indexer -If0

Increase search performance by increasing the indexing accuracy from 5
(default) to 7 at a cost of larger index files:

    ugrep-indexer -If7

Build steps
-----------

Configure and compile with:

    ./build.sh

If desired but not required, install with:

    sudo make install

Future enhancements
-------------------

- Index the contents of compressed files and archives to search them faster by
  skipping non-matching archives.

- Add an option to create one index file, e.g. specified explicitly to ugrep.
  This could further improve indexed search speed if the index file is located
  on a fast file system.  Otherwise, do not expect much improvement or even
  possible slow down, since a single index file cannot be searched concurrently
  and more index entries will be checked when in fact directories are skipped
  (skipping their indexes too).  Experiments will tell.

Q&A
---

### Q: How does it work?

Indexing adds a hidden index file `._UG#_Store` to each directory indexed.
Files indexed are scanned (never changed!) by ugrep-indexer to generate index
files.

The size of the index files depends on the specified accuracy, with `-0` the
lowest (small index files) and `-9` the highest (large index files).  The
default accuracy is `-5`.  See the next Q for details on the impact of accuracy
on indexing size versus search speed.

If any files or directories were updated, added or deleted after indexing, then
you can run ugrep-indexer again.  This incrementally updates all indexes.

Indexing *never follows symbolic links to directories*, because symbolically
linked directories may be located anywhere in a file system, or in another file
system, where we do not want to add index files.  You can still index symbolic
links to files with ugrep-indexer option `-S`.

Option `-v` (`--verbose`) displays the indexing progress and "noise" of each
file indexed.  Noise is a measure of *entropy* or *randomness* in the input.  A
higher level of noise means that indexing was less accurate in representing the
contents of a file.  For example, a file with random data is hard to index
accurately and will have a high level of noise.

Indexing is not a fast process (ugrep-indexer 0.9 is not yet multi-threaded)
and can take some time to complete.  When indexing completes, ugrep-indexer
displays the results of indexing.  The total size of the indexes added and
average indexing noise is also reported.

The ugrep-indexer understands "binary files", which can be skipped and not
indexed with ugrep-indexer option `-I` (`--ignore-binary`).  This is useful
when searching with ugrep option `-I` (`--ignore-binary`) to ignore binary
files.

The ugrep-indexer also supports .gitignore files (and similar), specified with
ugrep-indexer option `-X` (`--ignore-files`).  Ignored files and directories
will not be indexed to save file system space.  This works well when searching
for files with ugrep option `--ignore-files`.

Indexing can be aborted, for example with CTRL-C, which will not result in a
loss of search capability with ugrep, but will leave the directory structure
only partially indexed.  Option `-c` checks for the presence of the indexed
files and directories.  Run ugrep-indexer again to continue indexing.

Indexes are deleted with ugrep-indexer option `-d`.

The ugrep-indexer has been extensively tested by comparing `ugrep --index`
search results to the "slow" non-indexed `ugrep` search results on thousands of
files with thousands of random search patterns.

Indexed-based search works with all ugrep options except with option `-v`
(`--invert-match`), `--filter`, `-P` (`--perl-regexp`) and `-Z` (`--fuzzy`).
Option `-c` (`--count`) with `--index` automatically enables `--min-count=1` to
skip all files with zero matches.

Regex patterns are converted internally by ugrep with option `--index` to hash
tables for up to the first 16 bytes of the regex patterns specified, possibly
shorter in order to reduce construction time.  Therefore, the first characters
of a regex pattern to search are most critical to limit so-called false
positive matches that will slow down searching.

### Q: What is indexing accuracy?

Indexing is a form of lossy compression.  The higher the indexing accuracy, the
faster ugrep search performance should be by skipping more files that do not
match.  A higher accuracy reduces noise (less lossy).  A high level of noise
causes ugrep to sometimes search indexed files that do not match.  We call
these "false positive matches".  Higher accuracy requires larger index files.
Normally we expect 4K or less indexing storage per file on average.  The
minimum is 128 bytes of index storage per file, excluding the file name and
a 4-byte index header.  The maximum is 64K bytes storage per file for very
large noisy files.

When searching indexed files, ugrep option `--stats` shows the search
statistics after the indexing-based search completed.  When many files are not
skipped from searching due to indexing noise (i.e. false positives), then a
higher accuracy helps to increase the effectiveness of indexing, which may
speed up searching.

### Q: Why is the start-up time of ugrep higher with option --index?

The start-up overhead of `ugrep --index` to construct indexing hash tables
depends on the regex patterns.  If a regex pattern is very "permissive", i.e.
matches a lot of possible patterns, then the start-up time of `ugrep --index`
significantly increases to compute hash tables.  This may happen when large
Unicode character classes and wildcards are used, especially with the unlimited
`*` and `+` repeats.  To find out how the start-up time increases, use option
`ugrep --index -r PATTERN /dev/null --stats=vm` to search /dev/null with your
PATTERN.

### Q: Why are index files not compressed?

Index files should be very dense in information content and that is the case
with this new indexing algorithm for ugrep that I designed and implemented.
The denser an index file is, the more compact it accurately represents the
original file data.  That makes it hard or impossible to compress index files.
This is also a good indicator of how effective an index file will be in
practice.
