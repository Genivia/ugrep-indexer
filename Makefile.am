SUBDIRS = lzma/C src

man1_MANS = man/ugrep-indexer.1

EXTRA_DIST = README.md LICENSE.txt CONTRIBUTING.md CODE_OF_CONDUCT.md

all-local:	cp2bin

.PHONY:		cp2bin

# to copy the ugrep-indexer binary to the local bin/ugrep-indexer
cp2bin:		$(top_builddir)/src/ugrep-indexer$(EXEEXT)
		rm -f $(top_builddir)/bin/ugrep-indexer$(EXEEXT)
		mkdir -p $(top_builddir)/bin; \
		  cp -f $< $(top_builddir)/bin/ugrep-indexer$(EXEEXT)
		@echo
		@echo "OK"
		@echo

install-exec-hook:

install-data-hook:
		@echo " __________________________________________________________ "; \
		 echo "|                                                          |"; \
		 echo "| You have successfully built and installed ugrep-indexer. |"; \
		 echo "|                                                          |"; \
		 echo "| Thank you for using ugrep!                               |"; \
		 echo "|                                                          |"; \
		 echo "|             https://ugrep.com                            |"; \
		 echo "|             https://github.com/Genivia/ugrep             |"; \
		 echo "|             https://github.com/Genivia/ugrep-indexer     |"; \
		 echo "|__________________________________________________________|";

uninstall-hook:
		rm -f $(DESTDIR)$(bindir)/ugrep-indexer$(EXEEXT)
