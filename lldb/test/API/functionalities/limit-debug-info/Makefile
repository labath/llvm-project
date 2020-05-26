DYLIB_NAME := foo
DYLIB_CXX_SOURCES := foo.cpp
CXX_SOURCES := main.cpp
CFLAGS_EXTRAS += -flimit-debug-info

include Makefile.rules

ifeq "$(OS)" "Linux"
LD_EXTRAS += -Wl,-rpath=$(BUILDDIR)
endif
