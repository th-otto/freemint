#
# Makefile for libkern asm-m68k part
#

SHELL = /bin/sh
SUBDIRS = 

srcdir = .
top_srcdir = ../..
subdir = asm

default: all

include $(top_srcdir)/CONFIGVARS
include $(top_srcdir)/RULES
include $(top_srcdir)/PHONY

all-here: 

# default overwrites

# default definitions
GENFILES = .compile_*

#
# main target
#
asm-files: .compile_$(CPU)
	cd .compile_$(CPU); $(MAKE)

.compile_$(CPU): Makefile.objs
	$(MKDIR) -p $@
	$(CP) $< $@/Makefile

clean clean-recursive distclean distclean-recursive bakclean bakclean-recursive::
	@list='$(cputargets)'; for dir in $$list; do \
	  subdir=".compile_$${dir}"; \
	  target=`echo $@ | sed s/-recursive//`; \
	  test -d $$subdir && { echo "Making $$target in $$subdir"; \
	  (cd $$subdir && $(MAKE) -r $$target); } || true; \
	done
