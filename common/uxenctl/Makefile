#
# Copyright 2012-2015, Bromium, Inc.
# SPDX-License-Identifier: ISC
#

TOPDIR = $(abspath ../..)
include $(TOPDIR)/Config.mk

all:
%::
	@$(MAKE) --no-print-directory -C $(TOPDIR)/$(TARGET_HOST)/uxenctl $@
