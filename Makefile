#
# Makefile for freemint
#

SHELL = /bin/sh
SUBDIRS = shared sys xaaes tools

srcdir = .
top_srcdir = .
subdir = 

default: all

include $(top_srcdir)/CONFIGVARS
include $(top_srcdir)/RULES
include $(top_srcdir)/PHONY

all-here: 

# default overwrites

# default definitions
