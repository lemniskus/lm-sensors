#  Module.mk - Makefile for a Linux module for reading sensor data.
#  Copyright (c) 1998  Frodo Looijaard <frodol@dds.nl>
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; either version 2 of the License, or
#  (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; if not, write to the Free Software
#  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

# Note that MODULE_DIR (the directory in which this file resides) is a
# 'simply expanded variable'. That means that its value is substituted
# verbatim in the rules, until it is redefined. 
MODULE_DIR := lib

# The main and minor version of the library
LIBMAINVER := 0
LIBMINORVER := 0.0
LIBVER := $(LIBMAINVER).$(LIBMINORVER)

# The static lib name, the shared lib name, and the internal ('so') name of
# the shared lib.
LIBSHLIBNAME := libsensors.so.$(LIBVER)
LIBSTLIBNAME := libsensors.a
LIBSHSONAME := libsensors.so.$(LIBMAINVER)

LIBTARGETS := $(MODULE_DIR)/$(LIBSTLIBNAME) $(MODULE_DIR)/$(LIBSHLIBNAME)

LIBCSOURCES :=
LIBSHOBJECTS := $(LIBCSOURCES:.c=.lo) 
LIBSTOBJECTS := $(LIBCSOURCES:.c=.ao)

LIBHEADERFILES := 

# How to create the shared library
$(MODULE_DIR)/$(LIBSHLIBNAME): $(LIBSHOBJECTS)
	$(CC) -shared -Wl,-soname,$(LIBSONAME) -o $@ $< -lc

# And the static library
$(MODULE_DIR)/$(LIBSTLIBNAME): $(LIBSTOBJECTS)
	$(RM) $@
	$(AR) rcvs $@ $^

# Include all dependency files
INCLUDEFILES += $(LIBCSOURCES:.c=.ld) $(LIBCSOURCES:.c=.ad)

all-lib: $(LIBTARGETS)
all :: all-lib

install-lib:
	$(MKDIR) $(LIBDIR) $(LIBINCLUDEDIR)
	install -o root -g root -m 644 $(LIBTARGETS) $(LIBDIR)
	install -o root -g root -m 644 $(LIBHEADERFILES) $(LIBINCLUDEDIR)
install :: install-lib

clean-lib:
	$(RM) $(LIBTARGETS) $(LIBSHOBJECTS) $(LIBSTOBJECTS)
	$(RM) $(LIBSHOBJECTS:.lo=.ld) $(LIBSTOBJECTS:.ao=.ad)
clean :: clean-lib
