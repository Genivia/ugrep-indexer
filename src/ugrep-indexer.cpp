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

#define UGREP_INDEXER_VERSION "0.9.1 beta"

// check if we are compiling for a windows OS, but not Cygwin or MinGW
#if (defined(__WIN32__) || defined(_WIN32) || defined(WIN32) || defined(__BORLANDC__)) && !defined(__CYGWIN__) && !defined(__MINGW32__) && !defined(__MINGW64__)
# define OS_WIN
#endif

#ifdef OS_WIN

// compiling for a windows OS

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
#include <string>

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#define PATHSEPCHR '\\'
#define PATHSEPSTR "\\"

#else

// not compiling for a windows OS

#define _FILE_OFFSET_BITS 64
#define __STDC_FORMAT_MACROS

#include <signal.h>
#include <dirent.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <cinttypes>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#define PATHSEPCHR '/'
#define PATHSEPSTR "/"

#endif

// use zlib, libbz2, liblzma for option -z
#ifdef HAVE_LIBZ
# include "zstream.hpp"
#endif

#include "input.h"
#include "glob.hpp"
#include <iostream>
#include <vector>
#include <stack>

// number of bytes to gulp into the buffer to index a file
#define BUF_SIZE 65536

// smallest possible power-of-two size of an index of a file, shoud be > 61
#define MIN_SIZE 128

// default --ignore-files=FILE argument
#define DEFAULT_IGNORE_FILE ".gitignore"

// fixed constant strings
const char ugrep_index_filename[] = "._UG#_Store";
const char ugrep_index_file_magic[5] = "UG#\x03";

// command-line optional PATH argument
const char *arg_pathname = NULL;

// command-line options
int flag_accuracy = 6;
bool flag_check = false;
bool flag_decompress = false;
bool flag_delete = false;
bool flag_dereference_files = false;
bool flag_force = false;
bool flag_hidden = false;
bool flag_ignore_binary = false;
bool flag_no_messages = false;
bool flag_quiet = false;
bool flag_verbose = false;
std::vector<std::string> flag_ignore_files;

// ignore (exclude) files/dirs globs, a starting ! means override to include
struct Ignore {
  std::vector<std::string> files;
  std::vector<std::string> dirs;
};

// stack of ignore file/dir globs per ignore-file found
std::stack<Ignore> ignore_stack;

// entry data extracted from directory contents, moves pathname to this entry
struct Entry {

  // indexing is initiated with the pathname to the root of the directory to index
  Entry(const char *pathname = ".")
    :
      pathname(pathname), // the working dir by default
      base(0),
      mtime(~0ULL), // max time to make sure we check the working directory for updates
      size(0)
  {
    const char *sep = strrchr(pathname, PATHSEPCHR);
    if (sep != NULL)
      base = strlen(sep) - 1;
  }

  // new pathname entry, note this moves the pathname to the entry that owns it now
  Entry(std::string& pathname, size_t base, uint64_t mtime, off_t size)
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
  off_t       size;     // file size

};

// display the version info and exit
void version()
{
  std::cout << "ugrep-indexer " UGREP_INDEXER_VERSION "\n"
    "License BSD-3-Clause: <https://opensource.org/licenses/BSD-3-Clause>\n"
    "Written by Robert van Engelen and others: <https://github.com/Genivia/ugrep>" << std::endl;
  exit(EXIT_SUCCESS);
}

// display a help message and exit
void help()
{
  std::cout << "\nUsage:\n\nugrep-indexer [-0|...|-9] [-.] [-c|-d|-f] [-I] [-q] [-S] [-s] [-X] [-z] [PATH]\n\n\
    PATH    Optional pathname to the root of the directory tree to index.\n\n\
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
            Produce verbose output.\n\
    -X, --ignore-files[=FILE]\n\
            Do not index files and directories matching the globs in a FILE\n\
            encountered during indexing.  The default FILE is `" DEFAULT_IGNORE_FILE "'.\n\
    -z, --decompress\n\
            Index the contents of compressed files and archives.\n\
            This option is not yet available in this version.\n\
\n";
  version();
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
  fprintf(stderr, "ugrep-indexer: warning: %s%s%s\n", message, arg != NULL ? " " : "", arg != NULL ? arg : "");
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
  fprintf(stderr, "ugrep-indexer: error: %s%s%s: %s\n", message, arg != NULL ? " " : "", arg != NULL ? arg : "", errmsg);
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

  if (flag_check)
    flag_delete = flag_force = false;

  if (flag_decompress)
    usage("Option -z (--decompress) is not yet available in this version");
}

// open Unicode wide string UTF-8 encoded filename
inline int fopenw_s(FILE **file, const char *filename, const char *mode)
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

// return true if s[0..n-1] contains a \0 (NUL) or a non-displayable invalid UTF-8 sequence
bool is_binary(const char *s, size_t n)
{
  // file is binary if it contains a \0 (NUL)
  if (memchr(static_cast<const void*>(s), '\0', n) != NULL)
    return true;

  if (n == 1)
    return (*s & 0xc0) == 0x80;

  const char *e = s + n;

  while (s < e)
  {
    do
    {
      if ((*s & 0xc0) == 0x80)
        return true;
    } while ((*s & 0xc0) != 0xc0 && ++s < e);

    if (s >= e)
      return false;

    if (++s >= e || (*s & 0xc0) != 0x80)
      return true;

    if (++s < e && (*s & 0xc0) == 0x80)
      if (++s < e && (*s & 0xc0) == 0x80)
        if (++s < e && (*s & 0xc0) == 0x80)
          ++s;
  }

  return false;
}

// file indexing hash function
inline uint16_t indexhash(uint16_t h, uint8_t b)
{
  return (h << 6) - h - h - h + b;
}

// index a file to produce hashes[0..size-1] table, noise and binary file detection flag
bool index(const std::string& pathname, uint8_t *hashes, size_t& size, float& noise, bool& binary)
{
  FILE *file = NULL;

  if (fopenw_s(&file, pathname.c_str(), "r") != 0)
    return false;

  reflex::Input input(file);

  size = 0;
  noise = 0;

  char buffer[BUF_SIZE];
  size_t buflen = input.get(buffer, BUF_SIZE);
  if (buflen == 0)
  {
    binary = false;
    fclose(file);
    return true;
  }

  binary = is_binary(buffer, buflen);
  if (binary && flag_ignore_binary)
  {
    fclose(file);
    return true;
  }

  uint8_t window[8];
  size_t winlen = std::min(buflen, sizeof(window));
  char *bufptr = buffer + winlen;
  memcpy(static_cast<void*>(window), buffer, winlen);
  buflen -= winlen;
  size = 65536;
  memset(hashes, 0xff, size);

  if (buflen > 0)
  {
    while (true)
    {
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

      // shift window and append next character from the stream
      window[0] = window[1];
      window[1] = window[2];
      window[2] = window[3];
      window[3] = window[4];
      window[4] = window[5];
      window[5] = window[6];
      window[6] = window[7];
      window[7] = *bufptr++;
      --buflen;

      // refill buffer[] when empty
      if (buflen == 0)
      {
        bufptr = buffer;
        buflen = input.get(buffer, BUF_SIZE);
        if (buflen == 0)
          break;
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

  fclose(file);

  noise = 0;

  for (size_t i = 0; i < size; ++i)
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

  noise /= 8 * size;

  // compress the table in place until max noise is reached or exceeded
  while (size > MIN_SIZE)
  {
    // compute noise of halved hashes table (zero bits are hits)
    size_t half = size / 2;
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

    size = half;
    noise = half_noise;
  }

  return true;
}

#ifndef OS_WIN
// get modification time (micro seconds) from stat
uint64_t modified_time(const struct stat& buf)
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
#endif

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
void import_globs(FILE *file, std::vector<std::string>& files, std::vector<std::string>& dirs)
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

// catalog of directory contents
void cat(const std::string& pathname, std::stack<Entry>& dir_entries, std::vector<Entry>& file_entries, uint64_t& num_dirs, uint64_t& num_links, uint64_t& num_other, int64_t& ign_dirs, int64_t& ign_files, uint64_t& index_time, uint64_t& last_time, bool dir_only = false)
{
  // start populating file and link entries, append directory entries (not cleared)
  file_entries.clear();
  last_time = 0;
  index_time = 0;

  DIR *dir = opendir(pathname.c_str());

  if (dir == NULL)
  {
    error("cannot open directory", pathname.c_str());
    return;
  }

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

        // mark dir_entries stack with an empty pathname as a sentinel to pop the ignore_stack aferwards
        dir_entries.emplace("");

        import_globs(file, ignore_stack.top().files, ignore_stack.top().dirs);

        fclose(file);
      }
    }
  }

  ++num_dirs;
  std::string entry_pathname;
  struct dirent *dirent = NULL;

  while ((dirent = readdir(dir)) != NULL)
  {
    if (pathname.empty() || (pathname.size() == 1 && pathname[0] == '.'))
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
            dir_entries.emplace(entry_pathname, strlen(dirent->d_name), modified_time(buf), buf.st_size);
          else
            ++ign_dirs;
        }
        else if (S_ISREG(buf.st_mode) && !dir_only)
        {
          if (include_file(entry_pathname.c_str(), dirent->d_name))
          {
            uint64_t file_time = modified_time(buf);
            last_time = std::max(last_time, file_time);
            file_entries.emplace_back(entry_pathname, strlen(dirent->d_name), file_time, buf.st_size);
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
              file_entries.emplace_back(entry_pathname, strlen(dirent->d_name), file_time, buf.st_size);
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

  for (; !ignore_stack.empty() && !dir_entries.empty() && dir_entries.top().pathname.empty(); dir_entries.pop())
    ignore_stack.pop();
}

// recursively delete index files
void deleter(const char *pathname)
{
  flag_no_messages = true;

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

    // if index time is nonzero, there is a valid index file in this directory we should remove
    if (index_time > 0)
    {
      index_filename.assign(visit.pathname).append(PATHSEPSTR).append(ugrep_index_filename);
      remove(index_filename.c_str());
    }
  }
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

        if (fopenw_s(&index_file, index_filename.c_str(), "r+b") == 0)
        {
          char check_magic[sizeof(ugrep_index_file_magic)];

          if (fread(check_magic, sizeof(ugrep_index_file_magic), 1, index_file) != 0 &&
              memcmp(check_magic, ugrep_index_file_magic, sizeof(ugrep_index_file_magic)) == 0)
          {
            uint8_t header[4];
            char basename[65536];
            off_t inpos = sizeof(ugrep_index_file_magic);
            off_t outpos = sizeof(ugrep_index_file_magic);

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

              // search the directory contents to find the indexed file
              std::vector<Entry>::iterator entry = file_entries.begin();
              for (; entry != file_entries.end(); ++entry)
                if (entry->basename_size() == basename_size && strncmp(entry->basename(), basename, basename_size) == 0)
                  break;

              // if file is present in the directory and not updated, then preserve entry in the index
              if (entry != file_entries.end() && entry->mtime <= index_time)
              {
                ++num_files;

                // binary files registered but not indexed
                bool binary = (header[1] & 0x80) != 0;
                bin_files += binary && hashes_size == 0;

                if (inpos > outpos)
                {
                  // move header, basename, and hashes to the front of the index file
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

                // remove file from the cat file entries unless multi-part archive for we do not compute new hashes
                if ((header[1] & 0x40) == 0 || (header[1] & 0x20) != 0)
                  file_entries.erase(entry);

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
                  sum_hashes_size -= sizeof(header) + basename_size + hashes_size;
                }
              }
              else
              {
                ++mod_files;

                if (flag_check)
                {
                  outpos += sizeof(header) + basename_size + hashes_size;
                }
                else
                {
                  --add_files;
                  sum_hashes_size -= sizeof(header) + basename_size + hashes_size;
                }
              }

              inpos += sizeof(header) + basename_size + hashes_size;
            }

            if (fseeko(index_file, outpos, SEEK_SET) != 0 || ftruncate(fileno(index_file), outpos) != 0)
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

    if (index_file == NULL && !flag_check)
    {
      index_file = fopen(index_filename.c_str(), "wb");
      if (index_file == NULL || fwrite(ugrep_index_file_magic, sizeof(ugrep_index_file_magic), 1, index_file) == 0)
        error("cannot create index file in", visit.pathname.c_str());
    }

    if (index_file != NULL && !flag_check)
    {
      num_files += file_entries.size();

      for (const auto& file : file_entries)
      {
        size_t hashes_size = 0;
        float noise = 0;
        bool binary = false;

        // TODO if the file is a compressed file or an archive, then index compressed content for each part
        bool compressed = false;
        bool final_part = false;

        if (file.size == 0 || index(file.pathname, hashes, hashes_size, noise, binary))
        {
          // log2 of the hashes table size, zero to skip empty files and binary files when -I is specified
          uint8_t logsize = 0;
          for (size_t k = hashes_size; k > 1; k >>= 1)
            ++logsize;

          // binary files registered but not indexed
          bin_files += binary && hashes_size == 0;

          if (!binary || !flag_ignore_binary)
          {
            if (flag_verbose)
              printf("%c%12" PRId64 "%3u%% %s\n", compressed ? 'A' : binary ? 'B' : ' ', file.size, static_cast<unsigned>(100.0 * noise + 0.5), file.pathname.c_str());

            sum_files_size += file.size;
            sum_noise += noise;
          }

          // mark high bits
          logsize |= (binary << 7) | (compressed << 6) | (final_part << 5);

          const char *basename = file.basename();
          uint16_t basename_size = std::min(file.basename_size(), static_cast<size_t>(65535));
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
            break;
          }

          ++add_files;
          sum_hashes_size += sizeof(header) + basename_size + hashes_size;
        }
        else
        {
          printf("cannot index %s\n", file.pathname.c_str());
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
    printf("\n%13" PRIu64 " files indexed in %" PRIu64 " directories\n%13" PRId64 " directories not indexed\n%13" PRId64 " new files not indexed\n%13" PRId64 " modified files not indexed\n%13" PRId64 " deleted files are still indexed\n%13" PRId64 " binary files skipped with --ignore-binary\n", num_files, num_dirs, add_dirs, add_files, mod_files, del_files - ign_files, bin_files);
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
      printf("\n%13" PRIu64 " files indexed in %" PRIu64 " directories\n%13" PRId64 " new directories indexed\n%13" PRId64 " new files indexed\n%13" PRId64 " modified files indexed\n%13" PRId64 " deleted files removed from indexes\n%13" PRId64 " binary files skipped with --ignore-binary\n", num_files, num_dirs, add_dirs, add_files, mod_files, del_files, bin_files);
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
  options(argc, argv);

  if (flag_delete)
    deleter(arg_pathname);
  else
    indexer(arg_pathname);

  return EXIT_SUCCESS;
}
