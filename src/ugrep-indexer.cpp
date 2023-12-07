/******************************************************************************\
* Copyright (c) 2023, Robert van Engelen, Genivia Inc. All rights reserved.    *
*                                                                              *
* Redistribution and use in source and binary forms, with or without           *
* modification, are permitted provided that the following conditions are met:  *
*                                                                              *
*   (1) Redistributions of source code must retain the above copyright notice, *
*       this list of conditions and the following disclaimer.                  *
*                                                                              *
*   (2) Redistributions in binary form must reproduce the above copyright      *
*       notice, this list of conditions and the following disclaimer in the    *
*       documentation and/or other materials provided with the distribution.   *
*                                                                              *
*   (3) The name of the author may not be used to endorse or promote products  *
*       derived from this software without specific prior written permission.  *
*                                                                              *
* THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED *
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF         *
* MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO   *
* EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,       *
* SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, *
* PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;  *
* OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,     *
* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR      *
* OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF       *
* ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.                                   *
\******************************************************************************/

/**
@file      ugrep-indexer.cpp
@brief     file indexer for the ugrep search utility
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2023, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#define UGREP_INDEXER_VERSION "0.9.4 beta"

// use a task-parallel thread to decompress the stream into a pipe to search, also handles nested archives
#define WITH_DECOMPRESSION_THREAD

// check if we are compiling for a windows OS, but not Cygwin or MinGW
#if (defined(__WIN32__) || defined(_WIN32) || defined(WIN32) || defined(__BORLANDC__)) && !defined(__CYGWIN__) && !defined(__MINGW32__) && !defined(__MINGW64__)
# define OS_WIN
#endif

// 64 bits off_t and fseeko
#define _FILE_OFFSET_BITS 64

// PRId64 PRIu64
#define __STDC_FORMAT_MACROS

#ifdef OS_WIN // compiling for a windows OS

// disable min/max macros to use std::min and std::max
#define NOMINMAX

#include <windows.h>
#include <tchar.h>
#include <stdio.h>
#include <io.h>
#include <strsafe.h>
#include <string.h>
#include <fcntl.h>
#include <direct.h>
#include <errno.h>
#include <time.h>
#include <cstdint>
#include <string>

#define off_t int64_t
#define fseeko _fseeki64
#define ftruncate _chsize_s

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#define PATHSEPCHR '\\'
#define PATHSEPSTR "\\"

// convert a wide Unicode string to an UTF-8 string
std::string utf8_encode(const std::wstring &wstr)
{
  if (wstr.empty())
    return std::string();
  int size = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], static_cast<int>(wstr.size()), NULL, 0, NULL, NULL);
  std::string str(size, 0);
  WideCharToMultiByte(CP_UTF8, 0, &wstr[0], static_cast<int>(wstr.size()), &str[0], size, NULL, NULL);
  return str;
}

// convert a UTF-8 string to a wide Unicode String
std::wstring utf8_decode(const std::string &str)
{
  if (str.empty())
    return std::wstring();
  int size = MultiByteToWideChar(CP_UTF8, 0, &str[0], static_cast<int>(str.size()), NULL, 0);
  std::wstring wstr(size, 0);
  MultiByteToWideChar(CP_UTF8, 0, &str[0], static_cast<int>(str.size()), &wstr[0], size);
  return wstr;
}

// open Unicode wide string UTF-8 encoded filename
int fopenw_s(FILE **file, const char *filename, const char *mode)
{
  *file = NULL;
  std::wstring wfilename = utf8_decode(filename);
  HANDLE hFile;
  if (strchr(mode, 'a') == NULL && strchr(mode, 'w') == NULL)
    hFile = CreateFileW(wfilename.c_str(), (strchr(mode, '+') == NULL ? GENERIC_READ : GENERIC_READ | GENERIC_WRITE), FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
  else if (strchr(mode, 'a') == NULL)
    hFile = CreateFileW(wfilename.c_str(), (strchr(mode, '+') == NULL ? GENERIC_WRITE : GENERIC_READ | GENERIC_WRITE), FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  else
    hFile = CreateFileW(wfilename.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (hFile == INVALID_HANDLE_VALUE)
    return errno = (GetLastError() == ERROR_ACCESS_DENIED ? EACCES : ENOENT);
  int fd = _open_osfhandle(reinterpret_cast<intptr_t>(hFile), _O_RDONLY);
  if (fd == -1)
  { 
    CloseHandle(hFile);
    return errno = EINVAL; 
  }
  *file = _fdopen(fd, mode);
  if (*file == NULL)
  {
    _close(fd);
    return errno ? errno : (errno = EINVAL);
  }
  return 0;
}

// get modification time (micro seconds) from directory entry
inline uint64_t modified_time(const WIN32_FIND_DATAW& ffd)
{
  const struct _FILETIME& time = ffd.ftLastWriteTime;
  return static_cast<uint64_t>(time.dwLowDateTime) | (static_cast<uint64_t>(time.dwHighDateTime) << 32);
}

// get file size from directory entry
inline uint64_t file_size(const WIN32_FIND_DATAW& ffd)
{
  return static_cast<uint64_t>(ffd.nFileSizeLow) | (static_cast<uint64_t>(ffd.nFileSizeHigh) << 32);
}

#else // not compiling for a windows OS

#include <signal.h>
#include <dirent.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <string>

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#define PATHSEPCHR '/'
#define PATHSEPSTR "/"

// open Unicode wide string UTF-8 encoded filename
int fopenw_s(FILE **file, const char *filename, const char *mode)
{
  *file = NULL;
#if defined(HAVE_F_RDAHEAD)
  if (strchr(mode, 'a') == NULL && strchr(mode, 'w') == NULL)
  {
#if defined(O_NOCTTY)
    int fd = open(filename, O_RDONLY | O_NOCTTY);
#else
    int fd = open(filename, O_RDONLY);
#endif
    if (fd < 0)
      return errno;
    fcntl(fd, F_RDAHEAD, 1);
    return (*file = fdopen(fd, mode)) == NULL ? errno ? errno : (errno = EINVAL) : 0;
  }
#endif
  return (*file = fopen(filename, mode)) == NULL ? errno ? errno : (errno = EINVAL) : 0;
}

// get modification time (micro seconds) from stat
inline uint64_t modified_time(const struct stat& buf)
{
#if defined(HAVE_STAT_ST_ATIM) && defined(HAVE_STAT_ST_MTIM) && defined(HAVE_STAT_ST_CTIM)
  // tv_sec may be 64 bit, but value is small enough to multiply by 1000000 to fit in 64 bits
  return static_cast<uint64_t>(static_cast<uint64_t>(buf.st_mtim.tv_sec) * 1000000 + buf.st_mtim.tv_nsec / 1000);
#elif defined(HAVE_STAT_ST_ATIMESPEC) && defined(HAVE_STAT_ST_MTIMESPEC) && defined(HAVE_STAT_ST_CTIMESPEC)
  // tv_sec may be 64 bit, but value is small enough to multiply by 1000000 to fit in 64 bits
  return static_cast<uint64_t>(static_cast<uint64_t>(buf.st_mtimespec.tv_sec) * 1000000 + buf.st_mtimespec.tv_nsec / 1000);
#else
  return static_cast<uint64_t>(buf.st_mtime);
#endif
}

// get file size from stat
inline uint64_t file_size(const struct stat& buf)
{
  return static_cast<uint64_t>(buf.st_size);
}

#endif

// use zlib, libbz2, liblzma for option -z
#ifdef HAVE_LIBZ
# include "zstream.hpp"
#ifdef WITH_DECOMPRESSION_THREAD
# include "zthread.hpp"
#endif
#endif

#include "input.h"
#include "glob.hpp"
#include <cinttypes>
#include <iostream>
#include <algorithm>
#include <memory>
#include <vector>
#include <stack>

// number of bytes to gulp into the buffer to index a file
#define BUF_SIZE 65536

// fixed window size
#define WIN_SIZE static_cast<size_t>(8)

// smallest possible power-of-two size of an index of a file, shoud be > 61
#define MIN_SIZE 128

// default --ignore-files=FILE argument
#define DEFAULT_IGNORE_FILE ".gitignore"

typedef std::vector<std::string> StrVec;

// fixed constant strings
const char ugrep_index_filename[] = "._UG#_Store";
const char ugrep_index_file_magic[5] = "UG#\x03";

// command-line optional PATH argument
const char *arg_pathname = NULL;

// command-line options
int    flag_accuracy          = 5;     // -0 ... -9 (--accuracy) default is -5
bool   flag_check             = false; // -c (--check)
bool   flag_decompress        = false; // -z (--decompress)
bool   flag_delete            = false; // -d (--delete)
bool   flag_dereference_files = false; // -S (--dereference-files)
bool   flag_force             = false; // -f (--force)
bool   flag_hidden            = false; // -. (--hidden)
bool   flag_ignore_binary     = false; // -I (--ignore-binary)
bool   flag_no_messages       = false; // -s (--no-messages)
bool   flag_quiet             = false; // -q (--quiet)
bool   flag_verbose           = false; // -v (--verbose)
size_t flag_zmax              = 1;     // --zmax
StrVec flag_ignore_files;              // -X (--ignore-files)

// ignore (exclude) files/dirs globs, a glob prefixed with ! means override to include
struct Ignore {
  StrVec files;
  StrVec dirs;
};

// stack of ignore file/dir globs per ignore-file found
std::stack<Ignore> ignore_stack;

// entry data extracted from directory contents, constructor moves pathname string to this entry
struct Entry {

  // indexing is initiated with the pathname to the root of the directory to index
  Entry(const char *pathname = ".")
    :
      pathname(pathname), // the working dir by default
      base(0),
      mtime(~0ULL), // max time value to make sure we check the working directory for updates
      size(0)
  {
    const char *sep = strrchr(pathname, PATHSEPCHR);
    if (sep != NULL)
      base = strlen(sep) - 1;
  }

  // new pathname entry, note this moves the pathname to the entry that owns it now
  Entry(std::string& pathname, size_t base, uint64_t mtime, uint64_t size)
    :
      pathname(std::move(pathname)),
      base(base),
      mtime(mtime),
      size(size)
  { }

  ~Entry()
  { }

  // length of the basename
  size_t basename_size() const
  {
    return base;
  }

  // return the offset in the pathname of the basename
  size_t basename_offset() const
  {
    return pathname.size() - basename_size();
  }

  // return the file/dir basename
  const char *basename() const
  {
    return pathname.c_str() + basename_offset();
  }

  std::string pathname; // full pathname
  size_t      base;     // length of the basename in the pathname
  uint64_t    mtime;    // modification time
  uint64_t    size;     // file size

};

// Input stream to index
struct Stream {

  Stream()
    :
      file(NULL)
#ifdef HAVE_LIBZ
#ifdef WITH_DECOMPRESSION_THREAD
    , zthread(false, partname)
#else
    , is_compressed(false)
#endif
#endif
  { }

  ~Stream()
  {
    close();
  }

  bool open(const char *pathname)
  {
    return fopenw_s(&file, pathname, "rb") == 0;
  }

  void close()
  {
    if (file != NULL)
      fclose(file);
    file = NULL;
    input.clear();
  }

#ifdef HAVE_LIBZ

  bool read_file(const char *pathname, bool& archive)
  {
#ifdef WITH_DECOMPRESSION_THREAD

    // we don't know yet if this is an archive until we first read some data
    archive = false;

    if (flag_decompress)
    {
      partname.clear();

      // start decompression thread if not running, get pipe with decompressed input
      FILE *pipe_in = zthread.start(flag_zmax, pathname, file);
      if (pipe_in == NULL)
      {
        close();
        return false;
      }

      // read archive/compressed/plain data from the decompression thread chain pipe
      input = reflex::Input(pipe_in);
    }
    else
    {
      input = file;
    }

    return true;

#else

    // non-threaded decompression, one level only and no tar/pax/cpio and no compressed utf16/32 decoding
    if (flag_decompress)
    {
      // create or open a new zstreambuf
      if (!zstream)
        zstream = std::unique_ptr<zstreambuf>(new zstreambuf(pathname, file));
      else if (!archive)
        zstream->open(pathname, file);

      is_compressed = zstream->decompressing();

      // get zip info, if indexing a zip
      const zstreambuf::ZipInfo *zipinfo = zstream->zipinfo();

      // set archive flag when indexing an archive to get the next parts
      archive = zipinfo != NULL;
      if (archive)
        partname = zipinfo->name;
      else
        partname.clear();

      // new stream with zstreambuf, delete the old
      istream = std::unique_ptr<std::istream>(new std::istream(zstream.get()));

      // utf16/32 is no longer auto-converted like FILE*, so this does not index utf16/32 files
      input = istream.get();
    }
    else
    {
      archive = false;
      input = file;
    }

    return true;

#endif
  }

  bool read_next_file(const char *pathname, bool& archive)
  {
#ifdef WITH_DECOMPRESSION_THREAD

    // -z: open next archived file if any or close the compressed file/archive
    if (flag_decompress)
    {
      // close the input FILE* and its underlying pipe previously created with pipe() and fdopen()
      if (input.file() != NULL)
      {
        // close and unassign input, i.e. input.file() == NULL, also closes pipe_fd[0] per fdopen()
        fclose(input.file());
        input.clear();
      }

      partname.clear();

      // open pipe to the next file or part in an archive if there is a next file
      FILE *pipe_in = zthread.open_next(pathname);
      if (pipe_in != NULL)
      {
        // assign the next extracted file as input to search
        input = reflex::Input(pipe_in);

        // start searching the next file in the archive
        return true;
      }

      // if not extracting an archive, then read the next file
      if (!archive)
        return read_file(pathname, archive);

      // no more archive parts to extract
      return false;
    }

    archive = false;
    input = file;

    return true;

#else

    // -z: open next archived file if any or close the compressed file/archive
    if (flag_decompress)
    {
      partname.clear();

      if (archive)
      {
        const zstreambuf::ZipInfo *zipinfo = zstream->zipinfo();
        // if no more archive parts to extract, then return false
        if (zipinfo == NULL)
          return false;
      }
    }

    return read_file(pathname, archive);

#endif
  }

  // return true if decompressing a file in any of the decompression chain stages
  bool decompressing()
  {
#ifdef WITH_DECOMPRESSION_THREAD
    return zthread.decompressing();
#else
    return is_compressed;
#endif
  }

#endif

  FILE *file;
  reflex::Input input;
  std::string partname;

#ifdef HAVE_LIBZ
#ifdef WITH_DECOMPRESSION_THREAD
  Zthread zthread;
#else
  bool is_compressed;
  std::unique_ptr<zstreambuf> zstream;
  std::unique_ptr<std::istream> istream;
#endif
#endif
};

// display the version info and exit
void version()
{
  std::cout << "ugrep-indexer " UGREP_INDEXER_VERSION "\n"
    "License: BSD-3-Clause; ugrep user manual:  https://ugrep.com\n"
    "Written by Robert van Engelen and others:  https://github.com/Genivia/ugrep\n" << std::endl;
  exit(EXIT_SUCCESS);
}

// display a help message and exit
void help()
{
  std::cout << "\nUsage:\n\nugrep-indexer [-0|...|-9] [-.] [-c|-d|-f] [-I] [-q] [-S] [-s] [-X] [-z] [PATH]\n\n\
    Updates indexes incrementally unless option -f or --force is specified.\n\
    \n\
    When option -I or --ignore-binary is specified, binary files are ignored\n\
    and not indexed.  Searching with ugrep --index still searches binary files\n\
    unless ugrep option -I or --ignore-binary is specified also.\n\
    \n\
    Archives and compressed files are incrementally indexed only when option -z\n\
    or --decompress is specified.  Otherwise, archives and compressed files are\n\
    indexed as binary files, or are ignored with option -I or --ignore-binary.\n\
    \n\
    To create an indexing log file, specify option -v or --verbose and redirect\n\
    standard output to a log file.  All messages are sent to standard output.\n\
    \n\
    The following options are available:\n\
    \n\
    PATH    Optional pathname to the root of the directory tree to index.  The\n\
            default is to recursively index the working directory tree.\n\n\
    -0, -1, -2, -3, ..., -9, --accuracy=DIGIT\n\
            Specifies indexing accuracy.  A low accuracy reduces the indexing\n\
            storage overhead at the cost of a higher rate of false positive\n\
            pattern matches (more noise).  A high accuracy reduces the rate of\n\
            false positive regex pattern matches (less noise) at the cost of an\n\
            increased indexing storage overhead.  An accuracy between 3 and 7\n\
            is recommended.  The default accuracy is 5.\n\
    -., --hidden\n\
            Index hidden files and directories.\n\
    -?, --help\n\
            Display a help message and exit.\n\
    -c, --check\n\
            Recursively check and report indexes without reindexing files.\n\
    -d, --delete\n\
            Recursively remove index files.\n\
    -f, --force\n\
            Force reindexing of files, even those that are already indexed.\n\
    -I, --ignore-binary\n\
            Do not index binary files.\n\
    -q, --quiet, --silent\n\
            Quiet mode: do not display indexing statistics.\n\
    -S, --dereference-files\n\
            Follow symbolic links to files.  Symbolic links to directories are\n\
            never followed.\n\
    -s, --no-messages\n\
            Silent mode: nonexistent and unreadable files are ignored, i.e.\n\
            their error messages and warnings are suppressed.\n\
    -V, --version\n\
            Display version and exit.\n\
    -v, --verbose\n\
            Produce verbose output.  Indexed files are indicated with an A for\n\
            archive, C for compressed, B for binary or I for ignored binary.\n\
    -X, --ignore-files[=FILE]\n\
            Do not index files and directories matching the globs in a FILE\n\
            encountered during indexing.  The default FILE is `" DEFAULT_IGNORE_FILE "'.\n\
            This option may be repeated to specify additional files.\n\
    -z, --decompress\n\
            Index the contents of compressed files and archives.  When used\n\
            with option --zmax=NUM, indexes the contents of compressed files\n\
            and archives stored within archives up to NUM levels deep.\n"
#ifndef HAVE_LIBZ
            "\
            This option is not available in this build of ugrep-indexer.\n"
#else
            "\
            Supported compression formats: gzip (.gz), compress (.Z), zip"
#ifdef HAVE_LIBBZ2
            ",\n\
            bzip2 (requires suffix .bz, .bz2, .bzip2, .tbz, .tbz2, .tb2, .tz2)"
#endif
#ifdef HAVE_LIBLZMA
            ",\n\
            lzma and xz (requires suffix .lzma, .tlz, .xz, .txz)"
#endif
#ifdef HAVE_LIBLZ4
            ",\n\
            lz4 (requires suffix .lz4)"
#endif
#ifdef HAVE_LIBZSTD
            ",\n\
            zstd (requires suffix .zst, .zstd, .tzst)"
#endif
#ifdef HAVE_LIBBROTLI
            ",\n\
            brotli (requires suffix .br)"
#endif
#ifdef HAVE_LIBBZIP3
            ",\n\
            bzip3 (requires suffix .bz3)"
#endif
            ".\n"
#endif
            "\
    --zmax=NUM\n\
            When used with option -z (--decompress), indexes the contents of\n\
            compressed files and archives stored within archives by up to NUM\n\
            expansion levels deep.  The default --zmax=1 only permits indexing\n\
            uncompressed files stored in cpio, pax, tar and zip archives;\n\
            compressed files and archives are detected as binary files and are\n\
            effectively ignored.  Specify --zmax=2 to index compressed files\n\
            and archives stored in cpio, pax, tar and zip archives.  NUM may\n\
            range from 1 to 99 for up to 99 decompression and de-archiving\n\
            steps.  Increasing NUM values gradually degrades performance.\n"
#ifndef WITH_DECOMPRESSION_THREAD
            "\
            This option is not available in this build configuration of ugrep.\n"
#endif
"\n\
    The ugrep-indexer utility exits with one of the following values:\n\
    0      Indexes are up to date.\n\
    1      Indexing check -c detected missing and outdated index files.\n\
\n";

  exit(EXIT_SUCCESS);
}

// display usage information and exit
void usage(const char *message, const char *arg = NULL)
{
  std::cerr << "ugrep-indexer: " << message << (arg != NULL ? arg : "") << '\n';
  help();
}

// display a warning message unless option -s (--no-messages)
void warning(const char *message, const char *arg = NULL)
{
  if (flag_no_messages)
    return;
  fflush(stdout);
  printf("ugrep-indexer: warning: %s%s%s\n", message, arg != NULL ? " " : "", arg != NULL ? arg : "");
}

// display an error message unless option -s (--no-messages)
void error(const char *message, const char *arg)
{
  if (flag_no_messages)
    return;
  // use safe strerror_s() instead of strerror() when available
#if defined(__STDC_LIB_EXT1__) || defined(OS_WIN)
  char errmsg[256];
  strerror_s(errmsg, sizeof(errmsg), errno);
#else
  const char *errmsg = strerror(errno);
#endif
  fflush(stdout);
  printf("ugrep-indexer: error: %s%s%s: %s\n", message, arg != NULL ? " " : "", arg != NULL ? arg : "", errmsg);
}

#ifdef HAVE_LIBZ
// decompression error, function used by 
void cannot_decompress(const char *pathname, const char *message)
{
  if (!flag_verbose || flag_no_messages)
    return;
  fflush(stdout);
  printf("ugrep-indexer: warning: cannot decompress %s: %s\n", pathname, message != NULL ? message : "");
}
#endif

// convert unsigned decimal to non-negative size_t, produce error when conversion fails
size_t strtonum(const char *string, const char *message)
{
  char *rest = NULL;
  size_t size = static_cast<size_t>(strtoull(string, &rest, 10));
  if (rest == NULL || *rest != '\0')
    usage(message, string);
  return size;
}

// convert unsigned decimal to positive size_t, produce error when conversion fails or when the value is zero
size_t strtopos(const char *string, const char *message)
{
  size_t size = strtonum(string, message);
  if (size == 0)
    usage(message, string);
  return size;
}

// parse the command-line options
void options(int argc, const char **argv)
{
  bool options = true;

  for (int i = 1; i < argc; ++i)
  {
    const char *arg = argv[i];

    if ((*arg == '-'
#ifdef OS_WIN
         || *arg == '/'
#endif
        ) && arg[1] != '\0' && options)
    {
      bool is_grouped = true;

      // parse a ugrep command-line option
      while (is_grouped && *++arg != '\0')
      {
        switch (*arg)
        {
          case '-':
            is_grouped = false;
            if (*++arg == '\0')
            {
              options = false;
              continue;
            }

            if (strncmp(arg, "accuracy=", 9) == 0 && isdigit(arg[9]))
              flag_accuracy = arg[9] - '0';
            else if (strcmp(arg, "check") == 0)
              flag_check = true;
            else if (strcmp(arg, "decompress") == 0)
              flag_decompress = true;
            else if (strcmp(arg, "delete") == 0)
              flag_delete = true;
            else if (strcmp(arg, "dereference-files") == 0)
              flag_dereference_files = true;
            else if (strcmp(arg, "force") == 0)
              flag_force = true;
            else if (strcmp(arg, "help") == 0)
              help();
            else if (strcmp(arg, "hidden") == 0)
              flag_hidden = true;
            else if (strcmp(arg, "ignore-binary") == 0)
              flag_ignore_binary = true;
            else if (strcmp(arg, "ignore-files") == 0)
              flag_ignore_files.emplace_back(DEFAULT_IGNORE_FILE);
            else if (strncmp(arg, "ignore-files=", 13) == 0)
              flag_ignore_files.emplace_back(arg + 13);
            else if (strcmp(arg, "no-messages") == 0)
              flag_no_messages = true;
            else if (strcmp(arg, "quiet") == 0)
              flag_quiet = flag_no_messages = true;
            else if (strcmp(arg, "silent") == 0)
              flag_quiet = flag_no_messages = true;
            else if (strcmp(arg, "verbose") == 0)
              flag_verbose = true;
            else if (strcmp(arg, "version") == 0)
              version();
            else if (strncmp(arg, "zmax=", 5) == 0)
              flag_zmax = strtopos(arg + 5, "invalid argument --zmax=");
            else
              usage("invalid option --", arg);

            break;

          case 'c':
            flag_check = true;
            break;

          case 'd':
            flag_delete = true;
            break;

          case 'f':
            flag_force = true;
            break;

          case 'I':
            flag_ignore_binary = true;
            break;

          case 'q':
            flag_quiet = flag_no_messages = true;
            break;

          case 'S':
            flag_dereference_files = true;
            break;

          case 's':
            flag_no_messages = true;
            break;

          case 'V':
            version();
            break;

          case 'v':
            flag_verbose = true;
            break;

          case 'z':
            flag_decompress = true;
            break;

          case '.':
            flag_hidden = true;
            break;

          case 'X':
            flag_ignore_files.emplace_back(DEFAULT_IGNORE_FILE);
            break;

          case '?':
            help();
            break;

          default:
            if (isdigit(*arg))
              flag_accuracy = *arg - '0';
            else
              usage("invalid option -", arg);
        }
      }
    }
    else if (arg_pathname == NULL)
    {
      arg_pathname = arg;
    }
    else
    {
      usage("argument PATH already specified as ", arg_pathname);
    }
  }

  // -q overrides -v
  if (flag_quiet)
    flag_verbose = false;

  // -c silently overrides -d and -f
  if (flag_check)
    flag_delete = flag_force = false;

  // -d silently overrides -f
  if (flag_delete)
    flag_force = false;

#ifndef HAVE_LIBZ
  if (flag_decompress)
    usage("Option -z (--decompress) is not available");
#endif

#ifdef WITH_DECOMPRESSION_THREAD
  // --zmax: NUM argument exceeds limit?
  if (flag_zmax > 99)
    usage("option --zmax argument exceeds upper limit");
#else
  if (flag_zmax > 1)
    usage("Option --zmax is not available");
#endif
}

// return true if s[0..n-1] contains a \0 (NUL) or a non-displayable invalid UTF-8 sequence
bool is_binary(const char *s, size_t n)
{
  // file is binary if it contains a \0 (NUL)
  if (memchr(s, '\0', n) != NULL)
    return true;

  if (n == 1)
    return (*s & 0xc0) == 0x80;

  const char *e = s + n;

  while (s < e)
  {
    while (s < e && !(*s & 0x80))
      ++s;

    if (s >= e)
      return false;

    unsigned char c = static_cast<unsigned char>(*s);
    if (c < 0xc2 || c > 0xf4 || ++s >= e || (*s & 0xc0) != 0x80)
      return true;

    if (++s < e && (*s & 0xc0) == 0x80)
      if (++s < e && (*s & 0xc0) == 0x80)
        ++s;
  }

  return false;
}

// prime 61 file indexing hash function
inline uint16_t indexhash(uint16_t h, uint8_t b)
{
  return (h << 6) - h - h - h + b;
}

// index a file to produce hashes[0..hashes_size-1] table, noise, and archive/binary file detection flags
bool index(Stream& stream, const char *pathname, uint8_t *hashes, size_t& hashes_size, float& noise, bool& compressed, bool& archive, bool& binary, uint64_t& size)
{
  hashes_size = 0;
  noise = 0;
  size = 0;
  binary = false;

  // open next file when not currently indexing an archive, return false when failed
  if (!archive)
    if (!stream.open(pathname))
      return false;

#ifdef HAVE_LIBZ

  stream.read_next_file(pathname, archive);

#else

  stream.input = stream.file;

#endif

  char buffer[BUF_SIZE + WIN_SIZE]; // reserve WIN_SIZE (8) bytes padding for the window[] shifts
  size_t buflen = stream.input.get(buffer, BUF_SIZE);

#ifdef HAVE_LIBZ

  if (flag_decompress)
  {
    // now that we have some data, are we extracting it from an archive with parts?
    if (!stream.partname.empty())
    {
      // found an archive, do not close the pipe until all parts were extracted
      archive = true;

#ifndef WITH_DECOMPRESSION_THREAD
      // if extracting a directory part in an archive, then read it to skip it (decompression thread does this automatically)
      if (stream.partname.back() == '/')
      {
        while (stream.input.get(buffer, BUF_SIZE) != 0)
          continue;
        return true;
      }
#endif
    }
    else if (archive)
    {
      // archive extraction has ended, close the stream and return false
      stream.close();
      return archive = false;
    }

    // are we decompressing?
    compressed = stream.decompressing();
  }

#endif

  if (buflen == 0)
  {
    if (!archive)
      stream.close();
    return true;
  }

  // check if input is binary up to buflen bytes, but do not cut off right after the first UTF-8 byte
  binary = is_binary(buffer, buflen - ((buffer[buflen - 1] & 0xc0) == 0xc0));
  if (binary && flag_ignore_binary)
  {
    // if extracting a binary archive part, then read it to skip it
    if (archive)
    {
      while (stream.input.get(buffer, BUF_SIZE) != 0)
        continue;
    }
    else
    {
      stream.close();
    }

    return true;
  }

  const uint8_t *window = reinterpret_cast<uint8_t*>(buffer);
  size_t winlen = std::min(buflen, WIN_SIZE);
  size = buflen;
  buflen -= winlen;
  hashes_size = 65536;
  memset(hashes, 0xff, hashes_size);

  if (buflen > 0)
  {
    while (true)
    {
      // compute 8 staggered Bloom filters, hashing 1-grams to 8-grams
      uint16_t h = window[0];
      hashes[h] &= ~0x01;
      h = indexhash(h, window[1]);
      hashes[h] &= ~0x02;
      h = indexhash(h, window[2]);
      hashes[h] &= ~0x04;
      h = indexhash(h, window[3]);
      hashes[h] &= ~0x08;
      h = indexhash(h, window[4]);
      hashes[h] &= ~0x10;
      h = indexhash(h, window[5]);
      hashes[h] &= ~0x20;
      h = indexhash(h, window[6]);
      hashes[h] &= ~0x40;
      h = indexhash(h, window[7]);
      hashes[h] &= ~0x80;

      // shift window
      ++window;
      --buflen;

      // refill buffer[] when empty
      if (buflen == 0)
      {
        // move the remainder of the last window to the front of the buffer[] and append
        memmove(buffer, window, WIN_SIZE);
        buflen = stream.input.get(buffer + WIN_SIZE, BUF_SIZE);
        window = reinterpret_cast<uint8_t*>(buffer);
        if (buflen == 0)
          break;
        size += buflen;
      }
    }
  }

  for (size_t i = 0; i < winlen; ++i)
  {
    uint16_t h = window[i];
    hashes[h] &= ~0x01;
    for (size_t j = i + 1, k = 0x02; j < winlen; ++j, k <<= 1)
    {
      h = indexhash(h, window[j]);
      hashes[h] &= ~k;
    }
  }

  if (!archive)
    stream.close();

  for (size_t i = 0; i < hashes_size; ++i)
  {
    noise += (hashes[i] & 0x01) == 0;
    noise += (hashes[i] & 0x02) == 0;
    noise += (hashes[i] & 0x04) == 0;
    noise += (hashes[i] & 0x08) == 0;
    noise += (hashes[i] & 0x10) == 0;
    noise += (hashes[i] & 0x20) == 0;
    noise += (hashes[i] & 0x40) == 0;
    noise += (hashes[i] & 0x80) == 0;
  }

  noise /= 8 * hashes_size;

  // compress the table in place until max noise is reached or exceeded
  while (hashes_size > MIN_SIZE)
  {
    // compute noise of halved hashes table (zero bits are hits)
    size_t half = hashes_size / 2;
    float half_noise = 0;

    for (size_t i = 0; i < half; ++i)
    {
      half_noise += (hashes[i] & hashes[i + half] & 0x01) == 0;
      half_noise += (hashes[i] & hashes[i + half] & 0x02) == 0;
      half_noise += (hashes[i] & hashes[i + half] & 0x04) == 0;
      half_noise += (hashes[i] & hashes[i + half] & 0x08) == 0;
      half_noise += (hashes[i] & hashes[i + half] & 0x10) == 0;
      half_noise += (hashes[i] & hashes[i + half] & 0x20) == 0;
      half_noise += (hashes[i] & hashes[i + half] & 0x40) == 0;
      half_noise += (hashes[i] & hashes[i + half] & 0x80) == 0;
    }

    half_noise /= 8 * half;

    // stop at accuracy 0 -> 80% and 9 -> 10% default 5 -> 41.1% (4 -> 48.9%, 6 -> 33%)
    if (100.0 * half_noise >= 10.0 + 70.0 * (9 - flag_accuracy) / 9.0)
      break;

    // compress hashes table
    for (size_t i = 0; i < half; ++i)
      hashes[i] &= hashes[i + half];

    hashes_size = half;
    noise = half_noise;
  }

  return true;
}

// trim white space from either end of the line
void trim(std::string& line)
{
  size_t len = line.length();
  size_t pos;

  for (pos = 0; pos < len && isspace(line.at(pos)); ++pos)
    continue;

  if (pos > 0)
    line.erase(0, pos);

  len -= pos;

  for (pos = len; pos > 0 && isspace(line.at(pos - 1)); --pos)
    continue;

  if (len > pos)
    line.erase(pos, len - pos);
}

// read a line from buffered input, returns true when eof
inline bool getline(reflex::BufferedInput& input, std::string& line)
{
  int ch;

  line.erase();

  while ((ch = input.get()) != EOF && ch != '\n')
    line.push_back(ch);

  if (!line.empty() && line.back() == '\r')
    line.pop_back();

  return ch == EOF && line.empty();
}

// read globs from a file and split them into files or dirs to include or exclude by pushing them onto the vectors
void import_globs(FILE *file, StrVec& files, StrVec& dirs)
{
  // read globs from the specified file or files
  reflex::BufferedInput input(file);
  std::string line;

  while (true)
  {
    // read the next line
    if (getline(input, line))
      break;

    // trim white space from either end
    trim(line);

    // add glob to files or dirs using gitignore glob pattern rules
    if (!line.empty() && line.front() != '#')
    {
      if (line.front() != '!' || line.size() > 1)
      {
        if (line.back() == '/')
        {
          if (line.size() > 1)
            line.pop_back();
          dirs.emplace_back(line);
        }
        else
        {
          files.emplace_back(line);
          dirs.emplace_back(line);
        }
      }
    }
  }
}

// return true if pathname is a non-excluded directory
bool include_dir(const char *pathname, const char *basename)
{
  bool ok = true;

  if (!ignore_stack.empty())
  {
    // exclude directories whose pathname matches any one of the globs unless negated with !
    for (const auto& glob : ignore_stack.top().dirs)
    {
      if (glob.front() == '!')
      {
        if (!ok && glob_match(pathname, basename, glob.c_str() + 1))
          ok = true;
      }
      else if (ok && glob_match(pathname, basename, glob.c_str()))
      {
        ok = false;
      }
    }
  }

  return ok;
}

// return true if pathname is a non-excluded file
bool include_file(const char *pathname, const char *basename)
{
  bool ok = true;

  if (!ignore_stack.empty())
  {
    // exclude directories whose pathname matches any one of the globs unless negated with !
    for (const auto& glob : ignore_stack.top().files)
    {
      if (glob.front() == '!')
      {
        if (!ok && glob_match(pathname, basename, glob.c_str() + 1))
          ok = true;
      }
      else if (ok && glob_match(pathname, basename, glob.c_str()))
      {
        ok = false;
      }
    }
  }

  return ok;
}

// catalog directory contents
void cat(const std::string& pathname, std::stack<Entry>& dir_entries, std::vector<Entry>& file_entries, uint64_t& num_dirs, uint64_t& num_links, uint64_t& num_other, int64_t& ign_dirs, int64_t& ign_files, uint64_t& index_time, uint64_t& last_time, bool dir_only = false)
{
  // start populating file and link entries, append directory entries (not cleared)
  file_entries.clear();
  last_time = 0;
  index_time = 0;

#ifdef OS_WIN

  WIN32_FIND_DATAW ffd;

  std::string glob;

  if (pathname != ".")
    glob.assign(pathname).append("/*");
  else
    glob.assign("*");

  std::wstring wglob = utf8_decode(glob);
  HANDLE hFind = FindFirstFileW(wglob.c_str(), &ffd);

  if (hFind == INVALID_HANDLE_VALUE)
  {
    if (GetLastError() != ERROR_FILE_NOT_FOUND)
      warning("cannot open directory", pathname.c_str());
    return;
  }

#else

  DIR *dir = opendir(pathname.c_str());

  if (dir == NULL)
  {
    error("cannot open directory", pathname.c_str());
    return;
  }

#endif

  // check for ignore files, read them and push globs on the ignore_stack
  if (!flag_ignore_files.empty() && !dir_only)
  {
    std::string ignore_filename;

    for (const auto& ignore : flag_ignore_files)
    {
      ignore_filename.assign(pathname).append(PATHSEPSTR).append(ignore);

      FILE *file = NULL;

      if (fopenw_s(&file, ignore_filename.c_str(), "r") == 0)
      {
        // push globs imported from the ignore file to the back of the vectors
        ignore_stack.emplace();

        // mark dir_entries stack with an empty pathname as a sentinel to pop the ignore_stack afterwards
        dir_entries.emplace("");
        import_globs(file, ignore_stack.top().files, ignore_stack.top().dirs);
        fclose(file);
      }
    }
  }

  ++num_dirs;
  std::string entry_pathname;

#ifdef OS_WIN

  std::string cFileName;

  do
  {
    cFileName.assign(utf8_encode(ffd.cFileName));

    if (pathname.empty() || pathname == ".")
      entry_pathname.assign(cFileName);
    else if (pathname.back() == PATHSEPCHR)
      entry_pathname.assign(pathname).append(cFileName);
    else
      entry_pathname.assign(pathname).append(PATHSEPSTR).append(cFileName);

    DWORD attr = GetFileAttributesW(utf8_decode(entry_pathname).c_str());

    if (attr == INVALID_FILE_ATTRIBUTES)
    {
      errno = ENOENT;
      error("cannot read", entry_pathname.c_str());
    }
    else if ((attr & (FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_DEVICE)) == 0 && cFileName == ugrep_index_filename)
    {
      // get index file modification time
      index_time = modified_time(ffd);
    }
    else
    {
      // search directory entries that aren't hidden
      if ((cFileName[0] != '.' && (attr & (FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_SYSTEM)) == 0) ||
          (flag_hidden && cFileName[1] != '\0' && cFileName[1] != '.'))
      {
        if ((attr & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
        {
          /* TODO consider following symlink, but ugrep.exe doesn't do that yet either
          if (flag_dereference_files && (attr & (FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_DEVICE)) == 0 && !dir_only)
          {
          }
          */
        }
        else if ((attr & FILE_ATTRIBUTE_DIRECTORY) != 0)
        {
          if (dir_only || include_dir(entry_pathname.c_str(), cFileName.c_str()))
            dir_entries.emplace(entry_pathname, cFileName.size(), modified_time(ffd), file_size(ffd));
          else
            ++ign_dirs;
        }
        else if ((attr & FILE_ATTRIBUTE_DEVICE) == 0 && !dir_only)
        {
          if (include_file(entry_pathname.c_str(), cFileName.c_str()))
          {
            uint64_t file_time = modified_time(ffd);
            last_time = std::max(last_time, file_time);
            file_entries.emplace_back(entry_pathname, cFileName.size(), file_time, file_size(ffd));
          }
          else
          {
            ++ign_files;
          }
        }
        else
        {
          ++num_other;
        }
      }
    }
  } while (FindNextFileW(hFind, &ffd) != 0);

  FindClose(hFind);

#else

  struct dirent *dirent = NULL;

  while ((dirent = readdir(dir)) != NULL)
  {
    if (pathname.empty() || pathname == ".")
      entry_pathname.assign(dirent->d_name);
    else if (pathname.back() == PATHSEPCHR)
      entry_pathname.assign(pathname).append(dirent->d_name);
    else
      entry_pathname.assign(pathname).append(PATHSEPSTR).append(dirent->d_name);

    struct stat buf;

    if (lstat(entry_pathname.c_str(), &buf) != 0)
    {
      error("cannot stat", entry_pathname.c_str());
    }
    else if (S_ISREG(buf.st_mode) && strcmp(dirent->d_name, ugrep_index_filename) == 0)
    {
      // get index file modification time
      index_time = modified_time(buf);
    }
    else
    {
      // search directory entries that aren't . or .. or hidden
      if (dirent->d_name[0] != '.' || (flag_hidden && dirent->d_name[1] != '\0' && dirent->d_name[1] != '.'))
      {
        if (S_ISDIR(buf.st_mode))
        {
          if (dir_only || include_dir(entry_pathname.c_str(), dirent->d_name))
            dir_entries.emplace(entry_pathname, strlen(dirent->d_name), modified_time(buf), file_size(buf));
          else
            ++ign_dirs;
        }
        else if (S_ISREG(buf.st_mode) && !dir_only)
        {
          if (include_file(entry_pathname.c_str(), dirent->d_name))
          {
            uint64_t file_time = modified_time(buf);
            last_time = std::max(last_time, file_time);
            file_entries.emplace_back(entry_pathname, strlen(dirent->d_name), file_time, file_size(buf));
          }
          else
          {
            ++ign_files;
          }
        }
        else if (S_ISLNK(buf.st_mode) && !dir_only)
        {
          if (flag_dereference_files && stat(entry_pathname.c_str(), &buf) == 0 && S_ISREG(buf.st_mode))
          {
            if (include_file(entry_pathname.c_str(), dirent->d_name))
            {
              uint64_t file_time = modified_time(buf);
              last_time = std::max(last_time, file_time);
              file_entries.emplace_back(entry_pathname, strlen(dirent->d_name), file_time, file_size(buf));
            }
            else
            {
              ++ign_files;
            }
          }
          else
          {
            ++num_links;
          }
        }
        else
        {
          ++num_other;
        }
      }
    }
  }

  closedir(dir);

#endif

  for (; !ignore_stack.empty() && !dir_entries.empty() && dir_entries.top().pathname.empty(); dir_entries.pop())
    ignore_stack.pop();
}

// recursively delete index files
void deleter(const char *pathname)
{
  std::stack<Entry> dir_entries;
  std::vector<Entry> file_entries;
  std::string index_filename;
  Entry visit;

  uint64_t num_dirs = 0;
  uint64_t num_links = 0;
  uint64_t num_other = 0;
  int64_t ign_dirs = 0;
  int64_t ign_files = 0;
  uint64_t index_time;
  uint64_t last_time;
  uint64_t num_removed = 0;

  // pathname to the directory tree to index or .
  if (pathname == NULL)
    dir_entries.emplace();
  else
    dir_entries.emplace(pathname);

  // recurse subdirectories breadth-first to remove index files
  while (!dir_entries.empty())
  {
    visit = dir_entries.top();
    dir_entries.pop();

    cat(visit.pathname, dir_entries, file_entries, num_dirs, num_links, num_other, ign_dirs, ign_files, index_time, last_time, true);

    // if index time is nonzero, there is a valid index file in this directory that we should remove
    if (index_time > 0)
    {
      index_filename.assign(visit.pathname).append(PATHSEPSTR).append(ugrep_index_filename);
      if (remove(index_filename.c_str()) != 0)
      {
        error("cannot remove", index_filename.c_str());
      }
      else
      {
        ++num_removed;
        if (flag_verbose)
          printf("%13" PRIu64 " %s\n", num_removed, index_filename.c_str());
      }
    }
  }

  if (!flag_quiet)
    printf("\n%13" PRIu64 " indexes removed from %" PRIu64 " directories\n\n", num_removed, num_dirs);
}

// recursively index files
void indexer(const char *pathname)
{
  std::stack<Entry> dir_entries;
  std::vector<Entry> file_entries;
  std::string index_filename;
  Entry visit;

  uint64_t num_dirs = 0;
  uint64_t num_files = 0;
  uint64_t num_links = 0;
  uint64_t num_other = 0;
  int64_t add_dirs = 0;
  int64_t add_files = 0;
  int64_t mod_files = 0;
  int64_t del_files = 0;
  int64_t ign_dirs = 0;
  int64_t ign_files = 0;
  int64_t bin_files = 0;
  int64_t not_files = 0;
  int64_t zip_files = 0;
  int64_t sum_hashes_size = 0;
  int64_t sum_files_size = 0;
  float sum_noise = 0;
  uint8_t hashes[65536];

  // pathname to the directory tree to index or .
  if (pathname == NULL)
    dir_entries.emplace();
  else
    dir_entries.emplace(pathname);

  // recurse subdirectories
  while (!dir_entries.empty())
  {
    FILE *index_file = NULL;
    uint64_t index_time;
    uint64_t last_time;

    visit = dir_entries.top();
    dir_entries.pop();

    cat(visit.pathname, dir_entries, file_entries, num_dirs, num_links, num_other, ign_dirs, ign_files, index_time, last_time);

    index_filename.assign(visit.pathname).append(PATHSEPSTR).append(ugrep_index_filename);

    if (!flag_force)
    {
      if (index_time > 0)
      {
        // if the index file was the last modified file in this directory, then visit the next directory
        if (last_time <= index_time && visit.mtime <= index_time)
        {
          num_files += file_entries.size();

          continue;
        }

        if (fopenw_s(&index_file, index_filename.c_str(), flag_check ? "rb" : "r+b") == 0)
        {
          char check_magic[sizeof(ugrep_index_file_magic)];

          if (fread(check_magic, sizeof(ugrep_index_file_magic), 1, index_file) != 0 &&
              memcmp(check_magic, ugrep_index_file_magic, sizeof(ugrep_index_file_magic)) == 0)
          {
            uint8_t header[4];
            char basename[65536];
            off_t inpos = sizeof(ugrep_index_file_magic);
            off_t outpos = sizeof(ugrep_index_file_magic);

            std::vector<Entry>::iterator archive_entry = file_entries.end();

            while (true)
            {
              if (fseeko(index_file, inpos, SEEK_SET) != 0 || fread(header, sizeof(header), 1, index_file) == 0)
                break;

              // hashes table size, zero to skip empty files and binary files when -I is specified
              size_t hashes_size = 0;
              uint8_t logsize = header[1] & 0x1f;
              if (logsize > 0)
                for (hashes_size = 1; logsize > 0; --logsize)
                  hashes_size <<= 1;

              // sanity check
              if (hashes_size > 65536)
                break;

              uint16_t basename_size = header[2] | (header[3] << 8);
              if (fread(basename, 1, basename_size, index_file) < basename_size)
                break;

              // properly terminate
              basename[basename_size] = '\0';

              std::vector<Entry>::iterator entry = archive_entry;

              // if not the same archive filename, then remove the postponed archive entry from the cat file entries
              if (entry != file_entries.end() &&
                  (entry->basename_size() != basename_size ||
                   strncmp(entry->basename(), basename, basename_size) != 0))
              {
                file_entries.erase(entry);
                entry = archive_entry = file_entries.end();
              }

              // search the directory contents to find the indexed file
              if (entry == file_entries.end())
                for (entry = file_entries.begin(); entry != file_entries.end(); ++entry)
                  if (entry->basename_size() == basename_size && strncmp(entry->basename(), basename, basename_size) == 0)
                    break;

              // if file is present in the directory and not updated, then preserve entry in the index
              if (entry != file_entries.end() && entry->mtime <= index_time)
              {
                ++num_files;

                // binary files registered but not indexed
                bool binary = (header[1] & 0x80) != 0;
                bin_files += binary;
                not_files += binary && hashes_size == 0;

                if (inpos > outpos)
                {
                  // move header, basename, and hashes to the front of the index file (only happens when not just checking)
                  if (fread(hashes, 1, hashes_size, index_file) < hashes_size ||
                      fseeko(index_file, outpos, SEEK_SET) != 0 ||
                      fwrite(header, sizeof(header), 1, index_file) == 0 ||
                      fwrite(basename, 1, basename_size, index_file) < basename_size ||
                      fwrite(hashes, 1, hashes_size, index_file) < hashes_size)
                  {
                    error("cannot update index file in", visit.pathname.c_str());
                    break;
                  }
                }

                // remove file entry from the cat file entries unless multi-part archive
                bool archive = (header[1] & 0x40) != 0;
                if (archive)
                {
                  // postpone removing this archive entry
                  archive_entry = entry;
                }
                else
                {
                  file_entries.erase(entry);
                  archive_entry = file_entries.end();
                }

                outpos += sizeof(header) + basename_size + hashes_size;
              }
              else if (entry == file_entries.end())
              {
                ++del_files;

                if (flag_check)
                {
                  outpos += sizeof(header) + basename_size + hashes_size;
                }
                else
                {
                  if (flag_verbose)
                    printf("-           -  -%% %s\n", basename);

                  sum_hashes_size -= sizeof(header) + basename_size + hashes_size;
                }
              }
              else
              {
                ++mod_files;
                --add_files;

                if (flag_check)
                {
                  outpos += sizeof(header) + basename_size + hashes_size;
                }
                else
                {
                  sum_hashes_size -= sizeof(header) + basename_size + hashes_size;
                }
              }

              inpos += sizeof(header) + basename_size + hashes_size;
            }

            // make sure to remove postponed archive file entry
            if (archive_entry != file_entries.end())
              file_entries.erase(archive_entry);

            if (inpos > outpos &&
                (fseeko(index_file, outpos, SEEK_SET) != 0 ||
                 ftruncate(fileno(index_file), outpos) != 0))
              error("cannot update index file in", visit.pathname.c_str());
          }
          else
          {
            ++add_dirs;

            fclose(index_file);
            index_file = NULL;
          }
        }
        else
        {
          ++add_dirs;
        }
      }
      else
      {
        ++add_dirs;
      }
    }

    // create a new index file when none is present
    if (index_file == NULL && !flag_check)
    {
      if (fopenw_s(&index_file, index_filename.c_str(), "wb") != 0 ||
          fwrite(ugrep_index_file_magic, sizeof(ugrep_index_file_magic), 1, index_file) == 0)
      {
        error("cannot create index file in", visit.pathname.c_str());
        if (index_file != NULL)
          fclose(index_file);
        index_file = NULL;
      }
    }

    if (index_file != NULL && !flag_check)
    {
      Stream stream;

      for (const auto& entry : file_entries)
      {
        size_t hashes_size = 0;
        float noise = 0;
        bool binary = false;

        // if the file is a a zip archive, then index archived content for each part
        bool archive = false;
        bool compressed = false;
        uint64_t size = entry.size;
        const char *pathname = entry.pathname.c_str();

        if (size == 0 || index(stream, pathname, hashes, hashes_size, noise, compressed, archive, binary, size))
        {
          do
          {
            if (flag_verbose)
            {
              int classification = ' ';
              if (compressed)
                classification = 'C';
              if (archive)
                classification = 'A';
              if (binary)
                classification = size == 0 ? 'I' : 'B';
              if (archive)
                printf("%c%12" PRIu64 "%3u%% %s{%s}\n", classification, size, static_cast<unsigned>(100.0 * noise + 0.5), pathname, stream.partname.c_str());
              else
                printf("%c%12" PRIu64 "%3u%% %s\n", classification, size, static_cast<unsigned>(100.0 * noise + 0.5), pathname);
            }

            // binary files registered but not indexed
            bin_files += binary;
            not_files += binary && hashes_size == 0;

            if (!archive || size > 0)
            {
              // log2 of the hashes table size, zero to skip empty files and binary files when -I is specified
              uint8_t logsize = 0;
              for (size_t k = hashes_size; k > 1; k >>= 1)
                ++logsize;

              // mark high bits
              logsize |= (binary << 7) | (archive << 6) | (compressed << 5);

              const char *basename = entry.basename();
              uint16_t basename_size = static_cast<uint16_t>(std::min(entry.basename_size(), static_cast<size_t>(65535)));
              uint8_t header[4] = {
                static_cast<uint8_t>(flag_accuracy + '0'),
                logsize,
                static_cast<uint8_t>(basename_size),
                static_cast<uint8_t>(basename_size >> 8)
              };

              // write header with basename, log of the hashes size and hashes
              if (fwrite(header, sizeof(header), 1, index_file) == 0 ||
                  fwrite(basename, 1, basename_size, index_file) < basename_size ||
                  fwrite(hashes, 1, hashes_size, index_file) < hashes_size)
              {
                error("cannot write index file in", visit.pathname.c_str());
                if (!archive)
                  break;
              }

              zip_files += archive;
              ++num_files;
              ++add_files;
              sum_files_size += size;
              sum_noise += noise;
              sum_hashes_size += sizeof(header) + basename_size + hashes_size;
            }
          } while (archive && index(stream, pathname, hashes, hashes_size, noise, compressed, archive, binary, size));
        }
        else
        {
          printf("cannot index %s\n", pathname);
        }
      }
    }
    else
    {
      add_files += file_entries.size();
    }

    if (index_file != NULL)
      fclose(index_file);
  }

  if (sum_files_size > 0)
  {
    if (flag_verbose)
      printf(" ------------ ---\n%13" PRIu64 "%3u%%\n", sum_files_size, static_cast<unsigned>(100.0 * sum_noise / (mod_files + add_files) + 0.5));
    else if (!flag_no_messages)
      printf("\n%13" PRId64 " bytes scanned and indexed with %u%% noise on average", sum_files_size, static_cast<unsigned>(100.0 * sum_noise / (mod_files + add_files) + 0.5));
  }

  if (flag_check)
  {
    printf("\n%13" PRIu64 " files indexed in %" PRIu64 " directories\n%13" PRId64 " directories not indexed\n%13" PRId64 " new files not indexed\n%13" PRId64 " modified files not indexed\n%13" PRId64 " deleted files are needlessly indexed\n%13" PRId64 " binary files indexed\n%13" PRId64 " binary files ignored with --ignore-binary\n", num_files, num_dirs, add_dirs, add_files, mod_files, del_files - ign_files, bin_files - not_files, not_files);
    if (!flag_ignore_files.empty())
      printf("%13" PRIu64 " directories ignored with --ignore-files\n%13" PRIu64 " files ignored with --ignore-files\n", ign_dirs, ign_files);
    printf("%13" PRIu64 " symbolic links skipped\n%13" PRIu64 " devices skipped\n\n", num_links, num_other);

    if (add_dirs == 0 && add_files == 0 && mod_files == 0 && del_files == 0)
    {
      if (!flag_quiet)
        printf("Checked: indexes are fresh and up to date\n\n");
      exit(EXIT_SUCCESS);
    }
    else
    {
      if (!flag_quiet)
        printf("Warning: some indexes appear to be stale and are outdated or missing\n\n");
      exit(EXIT_FAILURE);
    }
  }
  else
  {
    if (!flag_quiet)
    {
      if (flag_decompress && zip_files > 0)
        printf("\n%13" PRIu64 " files indexed in %" PRIu64 " directories\n%13" PRId64 " new directories indexed\n%13" PRId64 " new files indexed (%" PRIu64 " in archives)\n%13" PRId64 " modified files indexed\n%13" PRId64 " deleted files removed from indexes\n%13" PRId64 " binary files indexed\n%13" PRId64 " binary files ignored with --ignore-binary\n", num_files, num_dirs, add_dirs, add_files, zip_files, mod_files, del_files, bin_files - not_files, not_files);
      else
        printf("\n%13" PRIu64 " files indexed in %" PRIu64 " directories\n%13" PRId64 " new directories indexed\n%13" PRId64 " new files indexed\n%13" PRId64 " modified files indexed\n%13" PRId64 " deleted files removed from indexes\n%13" PRId64 " binary files indexed\n%13" PRId64 " binary files ignored with --ignore-binary\n", num_files, num_dirs, add_dirs, add_files, mod_files, del_files, bin_files - not_files, not_files);
      if (!flag_ignore_files.empty())
        printf("%13" PRIu64 " directories ignored with --ignore-files\n%13" PRIu64 " files ignored with --ignore-files\n", ign_dirs, ign_files);
      if (sum_hashes_size > 0)
        printf("%13" PRIu64 " symbolic links skipped\n%13" PRIu64 " devices skipped\n%13" PRId64 " bytes indexing storage increase at %" PRId64 " bytes/file\n\n", num_links, num_other, sum_hashes_size, sum_hashes_size / num_files);
      else
        printf("%13" PRIu64 " symbolic links skipped\n%13" PRIu64 " devices skipped\n%13" PRId64 " bytes indexing storage decrease\n\n", num_links, num_other, sum_hashes_size);
      printf("Indexes are fresh and up to date\n\n");
    }
  }
}

int main(int argc, const char **argv)
{
#if !defined(OS_WIN) && defined(HAVE_LIBZ) && defined(WITH_DECOMPRESSION_THREAD)
  // ignore SIGPIPE, should never happen, but just in case
  signal(SIGPIPE, SIG_IGN);
#endif

  options(argc, argv);

  if (flag_delete)
    deleter(arg_pathname);
  else
    indexer(arg_pathname);

  return EXIT_SUCCESS;
}
