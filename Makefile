#
# package version & distribution type
#
MAJOR    ?= 0
MINOR    ?= 0
SUB      ?= 1
PATCH    ?= 2
CODENAME ?= $(shell lsb_release -cs)

# Sub directories containing source code, except for the main programs
SUBDIRS := src src/hmm src/thirdparty src/common src/alignment

#
# Set libraries, paths, flags and options
#

#Basic flags every build needs
LIBS=-lz
CPPFLAGS=-fopenmp -O3 -std=c++11 -g 
CFLAGS=-O3
CXX=g++
CC=gcc
HDF5=install

# Check operating system, OSX doesn't have -lrt
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
    LIBS += -lrt
endif

# Default to automatically installing hdf5
ifeq ($(HDF5), install)
    H5_LIB=./lib/libhdf5.a
    H5_INCLUDE=-I./include
    LIBS += -ldl
else
    # Use system-wide hdf5
    H5_LIB=
    H5_INCLUDE=
    LIBS += -lhdf5
endif

# Bulild and link the libhts submodule
HTS_LIB=./htslib/libhts.a
HTS_INCLUDE=-I./htslib

# Include the header-only fast5 library
FAST5_INCLUDE=-I./fast5

# Include the src subdirectories
NP_INCLUDE=$(addprefix -I./, $(SUBDIRS))

# Add include flags
CPPFLAGS += $(H5_INCLUDE) $(HTS_INCLUDE) $(FAST5_INCLUDE) $(NP_INCLUDE)

# Main programs to build
PROGRAM=nanopolish
TEST_PROGRAM=nanopolish_test

all: $(PROGRAM) $(TEST_PROGRAM)

#
# Build libhts
#
htslib/libhts.a:
	cd htslib; make

#
# If this library is a dependency the user wants HDF5 to be downloaded and built.
#
lib/libhdf5.a:
	wget https://www.hdfgroup.org/ftp/HDF5/releases/hdf5-1.8.14/src/hdf5-1.8.14.tar.gz
	tar -xzf hdf5-1.8.14.tar.gz
	cd hdf5-1.8.14; ./configure --enable-threadsafe --prefix=`pwd`/..; make; make install

#
# Source files
#

# Find the source files by searching subdirectories
CPP_SRC := $(foreach dir, $(SUBDIRS), $(wildcard $(dir)/*.cpp))
C_SRC := $(foreach dir, $(SUBDIRS), $(wildcard $(dir)/*.c))
EXE_SRC=src/main/nanopolish.cpp src/test/nanopolish_test.cpp

# Automatically generated object names
CPP_OBJ=$(CPP_SRC:.cpp=.o)
C_OBJ=$(C_SRC:.c=.o)

# Generate dependencies
PHONY=depend
depend: .depend

.depend: $(CPP_SRC) $(C_SRC) $(EXE_SRC) $(H5_LIB)
	rm -f ./.depend
	$(CXX) $(CPPFLAGS) -MM $(CPP_SRC) $(C_SRC) > ./.depend;

include .depend

# Compile objects
.cpp.o:
	$(CXX) -o $@ -c $(CPPFLAGS) -fPIC $<

.c.o:
	$(CC) -o $@ -c $(CFLAGS) -fPIC $<

# Link main executable
$(PROGRAM): src/main/nanopolish.o $(CPP_OBJ) $(C_OBJ) $(HTS_LIB) $(H5_LIB)
	$(CXX) -o $@ $(CPPFLAGS) -fPIC $< $(CPP_OBJ) $(C_OBJ) $(HTS_LIB) $(H5_LIB) $(LIBS)

# Link test executable
$(TEST_PROGRAM): src/test/nanopolish_test.o $(CPP_OBJ) $(C_OBJ) $(HTS_LIB) $(H5_LIB)
	$(CXX) -o $@ $(CPPFLAGS) -fPIC $< $(CPP_OBJ) $(C_OBJ) $(HTS_LIB) $(H5_LIB) $(LIBS)

test: $(TEST_PROGRAM)
	./$(TEST_PROGRAM)

clean:
	touch tmp
	rm -rf nanopolish nanopolish_test $(CPP_OBJ) $(C_OBJ) src/main/nanopolish.o src/test/nanopolish_test.o tmp deb-src/DEBIAN/md5sums

checksums: $(PROGRAM)
	find . -type f ! -regex '.*deb-src.*' -exec openssl md5 -r {} \; | sed 's/\*.\///g' > deb-src/DEBIAN/md5sums

deb: checksums
	touch tmp
	rm -rf tmp
	mkdir -p tmp/opt/nanopolish/bin
	rsync -va --exclude .svn deb-src/* tmp/
	sed "s/MAJOR/$(MAJOR)/g" < tmp/DEBIAN/control.tmpl > tmp/DEBIAN/control.tmp
	mv tmp/DEBIAN/control.tmp tmp/DEBIAN/control.tmpl
	sed "s/MINOR/$(MINOR)/g" < tmp/DEBIAN/control.tmpl > tmp/DEBIAN/control.tmp
	mv tmp/DEBIAN/control.tmp tmp/DEBIAN/control.tmpl
	sed "s/PATCH/$(PATCH)/g" < tmp/DEBIAN/control.tmpl > tmp/DEBIAN/control.tmp
	mv tmp/DEBIAN/control.tmp tmp/DEBIAN/control.tmpl
	sed "s/SUB/$(SUB)/g" < tmp/DEBIAN/control.tmpl > tmp/DEBIAN/control.tmp
	mv tmp/DEBIAN/control.tmp tmp/DEBIAN/control.tmpl
	sed "s/CODENAME/$(CODENAME)/g" < tmp/DEBIAN/control.tmpl > tmp/DEBIAN/control
	rsync -va $(PROGRAM) tmp/opt/nanopolish/bin/
	rsync -va scripts tmp/opt/nanopolish/
	(cd tmp; fakeroot dpkg -b . ../nanopolish-$(MAJOR).$(MINOR)-$(PATCH)~$(CODENAME).deb)
