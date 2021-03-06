#
# Asterisk -- A telephony toolkit for Linux.
# 
# Makefile for Menuselect
#
# Copyright (C) 2005-2006, Digium, Inc.
#
# Russell Bryant <russell@digium.com>
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

.PHONY: clean dist-clean distclean

PARENTSRC?=..
MENUSELECT_OBJS=menuselect.o menuselect_curses.o
MENUSELECT_CFLAGS=-g -c -D_GNU_SOURCE -DMENUSELECT -I$(PARENTSRC)/
MENUSELECT_LIBS=$(PARENTSRC)/mxml/libmxml.a

ifeq ($(OSARCH),SunOS)
  MENUSELECT_OBJS+=strcompat.o
endif

ifneq ($(NCURSES_LIB),)
  MENUSELECT_LIBS+=$(NCURSES_LIB)
  MENUSELECT_INCLUDE=$(NCURSES_INCLUDE)
else
  MENUSELECT_LIBS+=$(CURSES_LIB)
  MENUSELECT_INCLUDE=$(CURSES_INCLUDE)
endif

menuselect: $(MENUSELECT_OBJS)
	$(CC) -g -Wall -o $@ $(MENUSELECT_OBJS) $(MENUSELECT_LIBS)

menuselect.o: menuselect.c menuselect.h
	$(CC) -Wall $(CFLAGS) -o $@ $(MENUSELECT_CFLAGS) $<

menuselect_curses.o: menuselect_curses.c menuselect.h
	$(CC) -Wall $(CFLAGS) -o $@ $(MENUSELECT_CFLAGS) $(MENUSELECT_INCLUDE) $<

strcompat.o: strcompat.c
	$(CC) -Wall $(CFLAGS) -o $@ $(MENUSELECT_CFLAGS) $<

clean:
	rm -f menuselect *.o

dist-clean: distclean

distclean: clean
	rm -f menuselect-deps
