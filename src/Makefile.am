bin_PROGRAMS   = ugrep-indexer
ugrep_indexer_CPPFLAGS = $(EXTRA_CFLAGS) -DPLATFORM=\"$(PLATFORM)\"
ugrep_indexer_SOURCES  = ugrep-indexer.cpp glob.hpp glob.cpp input.h input.cpp utf8.h zopen.h zopen.c zstream.hpp
ugrep_indexer_LDADD    = $(PTHREAD_LIBS)
