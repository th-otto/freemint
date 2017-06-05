#
# the m68k arch specific files
#

SHELL = /bin/sh
SUBDIRS = magic

srcdir = .
top_srcdir = ..
subdir = arch

default: all

include $(top_srcdir)/CONFIGVARS
include $(top_srcdir)/RULES
include $(top_srcdir)/PHONY

all-here: buildkernel

# default overwrites

# default definitions
GENFILES = $(compile_all_dirs)

clean clean-recursive distclean distclean-recursive bakclean bakclean-recursive::
	@list='$(kerneltargets)'; for dir in $$list; do \
	  subdir=".compile_$${dir}"; \
	  target=`echo $@ | sed s/-recursive//`; \
	  test -d $$subdir && { echo "Making $$target in $$subdir"; \
	  (cd $$subdir && $(MAKE) -r $$target); } || true; \
	done
