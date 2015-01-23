include ../makefile.defs

LDLIBS=-lboost_program_options

all: logfixer logtruncate logchecker

logchecker: ../shared/wrapped_buffer.o ../shared/alloc_buffer.o ../shared/mmap_buffer.o 

clean_extra:
	rm -rf logfixer logtruncate logchecker

