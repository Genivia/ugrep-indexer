A monotonic indexer to speed up grepping
========================================

[![ci](https://github.com/Genivia/ugrep-indexer/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/Genivia/ugrep-indexer/actions?query=branch%3Amain++)

The *ugrep-indexer* utility recursively indexes files to speed up recursive
grepping.

*Note: this is a 0.9 beta version of a new generation of "monotonic indexers".
This release is subject to change and improvements based on experiments and
user feedback.  Regardless, this implementation has been extensively tested for
correctness.  Additional features and performance improvements are planned.*

[ugrep](https://github.com/Genivia/ugrep) is a grep-compatible ultra fast file
searcher that supports index-based searching as of v3.12.5.

Index-based search can be significantly faster on slow file systems and when
file system caching is ineffective: if the file system on a drive searched is
not cached in RAM, i.e. it is "cold", then indexing will speed up search.
It only searches those files that may match a specified regex pattern by using
an index of the file.  This index allows for a quick check if there is a
potential match, thus we avoid searching all files.

Indexed-based search should be safe and not skip updated files that may now
match.  If any files and directories were changed after indexing, then
searching will always search these additions and changes made to the file
system by comparing file and directory time stamps.

If many files were added or changed, then we might want to re-index to bring
the indexing up to date.  Re-indexing is incremental, so it will not take as
much time as the initial indexing process.

A typical but small example of an index-based search, e.g. on the ugrep v3.12.6
repository placed on a separate drive:

    $ cd drive/ugrep
    $ ugrep-indexer -I

    12247077 bytes scanned and indexed with 19% noise on average
        1317 files indexed in 28 directories
          28 new directories indexed
        1317 new files indexed
           0 modified files indexed
           0 deleted files removed from indexes
         128 binary files ignored with --ignore-binary
           0 symbolic links skipped
           0 devices skipped
     5605227 bytes indexing storage increase at 4256 bytes/file

Normal searching on a cold file system without indexing takes 1.02 seconds
after unmounting the `drive` and mounting again to clear FS cache to record the
effect of indexing:

    $ ugrep -I -l 'std::chrono' --stats
    src/ugrep.cpp

    Searched 1317 files in 28 directories in 1.02 seconds with 8 threads: 1 matching (0.07593%)

Ripgrep 13.0.0 takes longer with 1.18 seconds for the same cold search (ripgrep
skips binary files by default, so option `-I` is not specified):

    $ time rg -l 'std::chrono'
    src/ugrep.cpp
        1.18 real         0.01 user         0.06 sys

By contrast, with indexing, searching a cold file system only takes 0.0487
seconds with ugrep, which is 21 times faster, after unmounting `drive` and
mounting again to clear FS cache to record the effect of indexing:

    $ ugrep --index -I -l 'std::chrono' --stats
    src/ugrep.cpp

    Searched 1317 files in 28 directories in 0.0487 seconds with 8 threads: 1 matching (0.07593%)
    Skipped 1316 of 1317 files with indexes not matching any search patterns

There is always some variance in the elapsed time with 0.0487 seconds the best
time of four search runs that produced a search time range of 0.0487 (21x speed
up) to 0.0983 seconds (10x speed up).

The speed increase may be significantly higher in general compared to this
small demo, depending on several factors, the size of the files indexed, the
read speed of the file system and assuming most files are cold.

The indexing algorithm that I designed is *provably monotonic*: a higher
accuracy guarantees an increased search performance by reducing the false
positive rate, but also increases index storage overhead.  Likewise, a lower
accuracy decreases search performance, but also reduces the index storage
overhead.  Therefore, I named my indexer a *monotonic indexer*.

If file storage space is at a premium, then we can dial down the index storage
overhead by specifying a lower indexing accuracy.

Indexing the example from above with level 0 (option `-0`) reduces the indexing
storage overhead by 8.6 times, from 4256 bytes per file to a measly 490 bytes
per file:

    12247077 bytes scanned and indexed with 42% noise on average
        1317 files indexed in 28 directories
           0 new directories indexed
        1317 new files indexed
           0 modified files indexed
           0 deleted files removed from indexes
         128 binary files ignored with --ignore-binary
           0 symbolic links skipped
           0 devices skipped
      646123 bytes indexing storage increase at 490 bytes/file

Indexed search is still a lot faster by 12x than non-indexed for this example,
with 16 files actually searched (15 false positives):

    Searched 1317 files in 28 directories in 0.0722 seconds with 8 threads: 1 matching (0.07593%)
    Skipped 1301 of 1317 files with indexes not matching any search patterns

Regex patterns that are more complex than this example may have a higher false
positive rate naturally, which is the rate of files that are considered
possibly matching when they are not.  A higher false positive rate may reduce
search speeds when the rate is large enough to be impactful.

The following table shows how indexing accuracy affects indexing storage
and the average noise per file indexed.  The rightmost columns show the search
speed and false positive rate for `ugrep --index -I -l 'std::chrono'`:

| acc. | index storage (KB) | average noise | false positives | search time (s) |
| ---- | -----------------: | ------------: | --------------: | --------------: |
| `-0` |                631 |           42% |              15 |          0.0722 |
| `-1` |               1276 |           39% |               1 |          0.0506 |
| `-2` |               1576 |           36% |               0 |          0.0487 |
| `-3` |               2692 |           31% |               0 |            unch |
| `-4` |               2966 |           28% |               0 |            unch |
| `-5` |               4953 |           23% |               0 |            unch |
| `-6` |               5474 |           19% |               0 |            unch |
| `-7` |               9513 |           15% |               0 |            unch |
| `-8` |              10889 |           11% |               0 |            unch |
| `-9` |              13388 |            7% |               0 |            unch |

If the specified regex matches many more possible patterns, for example with
the search `ugrep --index -I -l '(todo|TODO)[: ]'`, then we may observe a
higher rate of false positives among the 1317 files searched, resulting in
slightly longer search times:

| acc. | false positives | search time (s) |
| ---- | --------------: | --------------: |
| `-0` |             189 |           0.292 |
| `-1` |              69 |           0.122 |
| `-2` |              43 |           0.103 |
| `-3` |              19 |           0.101 |
| `-4` |              16 |           0.097 |
| `-5` |               2 |           0.096 |
| `-6` |               1 |            unch |
| `-7` |               0 |            unch |
| `-8` |               0 |            unch |
| `-9` |               0 |            unch |

Accucacy `-5` is the default, which tends to work very well to search with
regex patterns of modest complexity.

One word of caution.  There is always a tiny bit of overhead to check the
indexes.  This means that if all files are already cached in RAM, because files
were searched or read recently, then indexing will not necesarily speed up
search, obviously.  In that case a non-indexed search might be faster.
Furthermore, an index-based search has a longer start-up time.  This start-up
time increases when Unicode character classes and wildcards are used that must
be converted to hash tables.

To summarize, index-based search is most effective when searching a lot of
cold files and when regex patterns aren't matching too much, i.e. we want to
limit the use of unlimited repeats `*` and `+` and limit the use of Unicode
character classes when possible.  This reduces the ugrep start-up time and
limits the rate of false positive pattern matches (see also Q&A below).

Quick examples
--------------

Recursively and incrementally index all non-binary files showing progress:

    ugrep-indexer -I -v

Recursively and incrementally index all non-binary files, including non-binary
files stored in archives and in compressed files, showing progress:

    ugrep-indexer -z -I -v

Incrementally index all non-binary files, including archives and compressed
files, show progress, follow symbolic links to files (but not to directories),
but do not index files and directories matching the globs in .gitignore:

    ugrep-indexer -z -I -v -S -X

Force re-indexing of all non-binary files, including archives and compressed
files, follow symbolic links to files (but not to directories), but do not
index files and directories matching the globs in .gitignore:

    ugrep-indexer -f -z -I -v -S -X

Same, but decrease index file storage to a minimum by decreasing indexing
accuracy from 5 (default) to 0:

    ugrep-indexer -f -0 -z -I -v -X

Increase search performance by increasing the indexing accuracy from 5
(default) to 7 at a cost of larger index files:

    ugrep-indexer -f7zIvX

Recursively delete all hidden `._UG#_Store` index files to restore the
directory tree to non-indexed:

    ugrep-indexer -d

Build steps
-----------

Configure and compile with:

    ./build.sh

If desired but not required, install with:

    sudo make install

Future enhancements
-------------------

- Add an option to create one index file, e.g. specified explicitly to ugrep.
  This could further improve indexed search speed if the index file is located
  on a fast file system.  Otherwise, do not expect much improvement or even
  possible slow down, since a single index file cannot be searched concurrently
  and more index entries will be checked when in fact directories are skipped
  (skipping their indexes too).  Experiments will tell.  A critical caveat of
  this approach is that index-based search with `ugrep --index` is no longer
  "safe": new and modified files that are not indexed yet will not be searched.

- Indexing tiny files might not be effective to speed up grepping.  This needs
  further investigation.  The indexer could skip such tiny files for example.

- Each N-gram Bloom filter has its own "bit tier" in the hash table to avoid
  hash conflicts.  For example 2-grams do not share any bits with 3-grams.
  This ensures that we never have any false positives with characters being
  matched that are not part of the pattern.  However, the 1-gram (single
  character) bit space is small (at most 256 bits).  Therefore, we waste some
  bits in larger hash tables.  A possible approach to reduce waste is to
  combine 1-grams with 2-grams to share the same bit space.  This is easy to do
  if we consider a 1-gram being equal to a 2-gram with the second character set
  to `\0` (NUL).  We can lower the false positive rate with a second 2-gram
  hash based on a different hash method.  Or we can expand the "bit tiers" from
  8 to 9 to store 9-grams.  This will increase the indexing accuracy for longer
  patterns (9 or longer) at no additional cost.  On the other hand, with this
  change there will be more false positives with characters being matched that
  are not part of the pattern when hash tables are small.

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

Indexing *never follows symbolic links to directories*, because symbolically
linked directories may be located anywhere in a file system, or in another file
system, where we do not want to add index files.  You can still index symbolic
links to files with ugrep-indexer option `-S`.

Option `-v` (`--verbose`) displays the indexing progress and "noise" of each
file indexed.  Noise is a measure of *entropy* or *randomness* in the input.  A
higher level of noise means that indexing was less accurate in representing the
contents of a file.  For example, a large file with random data is hard to
index accurately and will have a high level of noise.

The complexity of indexing is linear in the size of a given file to index.
In practice it is not a fast process, not as fast a searching, and may take
some time to complete a full indexing pass over a large directory tree.  When
indexing completes, ugrep-indexer displays the results of indexing.  The total
size of the indexes added and average indexing noise is also reported.

Scanning a file to index results in a 64KB indexing hashes table.  Then, the
ugrep-indexer halves the table with bit compression using bitwise-and as long
as the target accuracy is not exceeded.  Halving is made possible by the fact
that the table encodes hashes for 8 windows at offsets from the start of the
pattern, corresponding to the 8 bits per index hashing table cell.  Combining
the two halves of the table may flip some bits to zero from one, which may
cause a false positive match.  This proves the monotonicity of the indexer.

The ugrep-indexer understands "binary files", which can be ignored and not
indexed with ugrep-indexer option `-I` (`--ignore-binary`).  This is useful
when searching with ugrep option `-I` (`--ignore-binary`) to ignore binary
files, which is a typical scenario.

The ugrep-indexer also supports .gitignore files (and similar), specified with
ugrep-indexer option `-X` (`--ignore-files`).  Ignored files and directories
will not be indexed to save file system space.  This works well when searching
for files with ugrep option `--ignore-files`.

Indexing can be aborted, for example with CTRL-C, which will not result in a
loss of search capability with ugrep, but will leave the directory structure
only partially indexed.

Option `-c` checks indexes for stale references and non-indexed files and
directories.  Run ugrep-indexer again to incrementally update all indexes.

Indexes are deleted with ugrep-indexer option `-d`.

The ugrep-indexer has been extensively tested by comparing `ugrep --index`
search results to the "slow" non-indexed `ugrep` search results on thousands of
files with thousands of random search patterns.

Indexed-based search works with all ugrep options except with option `-v`
(`--invert-match`), `--filter`, `-P` (`--perl-regexp`) and `-Z` (`--fuzzy`).
Option `-c` (`--count`) with `--index` automatically sets `--min-count=1` to
skip all files with zero matches.

If any files or directories were updated, added or deleted after indexing, then
ugrep `--index` will always search these files and directories when they are
present on the recursive search path.  You can run ugrep-indexer again to
incrementally update all indexes.

Regex patterns are converted internally by ugrep with option `--index` to a
form of hash tables for up to the first 16 bytes of the regex patterns
specified, possibly shorter in order to reduce construction time when regex
patterns are complex.  Therefore, the first 8 to 16  characters of a regex
pattern to search are most critical and should not match too much to limit
so-called false positive matches that may slow down searching.

In ugrep, a regex pattern is converted to a DFA.  An indexing hash finite
automaton (HFA) is constructed on top of the DFA to compactly represent hash
tables as state transitions with labelled edges.  This HFA consists of up to
eight layers, each shifted by one byte to represent the next 8-byte window over
the pattern.  Each HFA layer encodes index hashes for that part of the pattern.
The index hash function chosen is "additive", meaning the next byte is added
when hashed with the previous hash.  This is very important as it critically
reduces the HFA construction overhead.  We can now encode labelled HFA
transitions to states as multiple edges with 16-bit hash value ranges instead
of a set of single edges each with an individual hash value.  To this end, I
use my open-ended ranges library `reflex::ORanges<T>` derived from
`std::set<T>`.

A very simple single string `maybe_match()` function with the prime 61 index
hash function is given below to demonstrate index-based searching of a single
string:

    // prime 61 hashing
    uint16_t indexhash(uint16_t h, uint8_t b, size_t size)
    {
      return ((h << 6) - h - h - h + b) & (size - 1);
    }

    // return possible match of string given array of hashes of size <= 64K (power of two)
    bool maybe_match(const char *string, uint8_t *hashes, size_t size)
    {
      size_t len = strlen(string); // practically we can and should limit len to e.g. 15 or 16
      for (const char *window = string; len > 0; ++window, --len)
      {
        uint16_t h = window[0] & (size - 1);
        if (hashes[h] & 0x01)
          return false
        size_t k, n = len < 8 ? len : 8;
        for (k = 1; k < n; ++k)
        {
          h = indexhash(h, window[k], size);
          if (hashes[h] & (1 << k))
            return false;
        }
      }
      return true;
    }

The prime 61 hash was chosen among many other possible hashing functions using
a realistic experimental setup.  A candidate hashing function was tested by
repreatedly searching a randomly-drawn word from a 100MB Wikipedia file.
The word was mutated with one, two or three random letters.  This mutation is
checked to make sure it does not correspond to an actual valid word in the
Wikipedia file.  Then the false positive rate was recorded whenever a mutated
word matches the file.  A hash function with a minimal false positive rate
should be a good candidate overall.

By using a window of 8 (or shorter depending on the pattern length) the false
positive rate is lower compared to standard Bloom filters.  More specifically,
*N²* hash functions are used instead of *N* in a Bloom filter.  For shorter
patterns, *N* is often too small to limit false positives.  Therefore, *N²* is
more effective.  It also rejects any pattern from a match that has a character
anywhere in the first 8 bytes of the pattern does not actually occur anywhere
in an indexed file, whereas a standard Bloom filter might have a false positive
match.  Furthermore, the bit addressing used to index the hashes table enables
efficient table compression.

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

When searching indexed files with `ugrep --index --stats`, option `--stats`
shows the search statistics after the indexing-based search completed.  When
many files are not skipped from searching due to indexing noise (i.e. false
positives), then a higher accuracy helps to increase the effectiveness of
indexing, which may speed up searching.

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

### Q: Why index archives and compressed files?

Archiving (zip/tar/pax/cpio) and compressing files saves disk space.  On the
other hand, searching archives and compressed files is slower than searching
regular files.  Indexing archives and compressed files with `ugrep-indexer -z
-I` and searching them with `ugrep -z -I --index PATTERN` can speed up
searching when the archives and compressed files are skipped when the pattern
does not match.  On the other hand, disk store requirements will increase with
the addition of index file entries for archives and compressed files.  Note
that when archives and compressed files contain binaries, option `-I` ignores
these archived/compressed binaries.
