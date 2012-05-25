CXX = /usr/bin/g++
#We want to use g++ to compile our C files
CC = /usr/bin/g++

# Compiler Flags
# -g		generate debugging symbols
# -O0		no optimizations (for now)
# -Wall provide diagnostic warnings
CXXFLAGS =
CFLAGS =
DBGFLAGS = -g -O0 -Wall -DDEBUG
RLSFLAGS = -O3 -Wall

#Linker flags
LDFLAGS =

HEADERS = packets.h replfs_net.h client.h log.h
SOURCES = replfs_net.cpp client.cpp server.cpp test.c
OBJECTS = replfs_net.o client.o server.o test.o
TARGETS = replFsServer libclientReplFs.a testRFS

default: CXXFLAGS += $(RLSFLAGS)
default: CFLAGS += $(RLSFLAGS)
default: $(TARGETS)

debug: CXXFLAGS += $(DBGFLAGS)
debug: CFLAGS += $(DBGFLAGS)
debug: $(TARGETS)

replFsServer: server.o replfs_net.o
	$(CXX) $(CXXFLAGS) -o $@ $^

libclientReplFs.a: client.o replfs_net.o
	ar rcs $@ $^

testRFS: test.o libclientReplFs.a
	$(CXX) -o $@ $^

Makefile.dependencies:: $(SOURCES) $(HEADERS)
	$(CXX) $(CXXFLAGS) -MM $(SOURCES) > Makefile.dependencies

-include Makefile.dependencies

.PHONY: clean

clean:
	@rm -f $(TARGETS) *.o Makefile.dependecies core
