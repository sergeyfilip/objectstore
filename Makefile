#
# Makefile for the ObjectStore project
#
# $Id: Makefile,v 1.23 2015/10/15 12:47:14 sf Exp $
#

.PHONY: first prebuild build packit TAGS linecount

first: build
prebuild:

# Project version and build identifier
MAJORVERSION = 1
MINORVERSION = 1
RELEASE = 12
MSVERSION = $(MAJORVERSION),$(MINORVERSION)
ifeq ($(BUILDNO),)
BUILDNO := manual
endif
VERSION = $(MAJORVERSION).$(MINORVERSION).$(RELEASE)
ifeq ($(MAKECMDGOALS),TAGS)
NODEPS := 1
endif

ARCH_NAME ?= win7-amd64
TARGET_PATH := targets/$(ARCH_NAME)

include Makefile.arch.$(ARCH_NAME)
include Makefile.targets

TAGS:
	rm -f TAGS
	find . \( -name '*.cc'  -o -name '*.hh' -o  -name '*.hcc' -o -name '*.c' -o  -name '*.h' -o  -name '*.rc' \) | while read f; do echo -n "."; etags -a "$${f}"; done

ifndef NODEPS
-include $(foreach s, $(all-sources), $(TARGET_PATH)/$(s)$(DEPEXT))
endif
