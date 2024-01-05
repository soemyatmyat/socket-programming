# Compiler and flags
CXX = g++
CXXFLAGS = -Wall -Wextra -O2
LDFLAGS = $(CXXFLAGS) -pthread

all: shfd

util.o: util.h util.cpp
	$(CXX) $(CXXFLAGS) -c -o util.o util.cpp

shell_server.o: util.h shfd.h shell_server.cpp
	$(CXX) $(CXXFLAGS) -c -o shell_server.o shell_server.cpp

file_server.o: util.h shfd.h file_server.cpp
	$(CXX) $(CXXFLAGS) -c -o file_server.o file_server.cpp

replica_server.o: util.h shfd.h replica_server.cpp
	$(CXX) $(CXXFLAGS) -c -o replica_server.o replica_server.cpp

udp.o: util.h udp.cpp
	$(CXX) $(CXXFLAGS) -c -o udp.o udp.cpp 

shfd.o: util.h shfd.h shfd.cpp
	$(CXX) $(CXXFLAGS) -c -o shfd.o shfd.cpp 

shfd: util.o shell_server.o file_server.o replica_server.o udp.o shfd.o
	$(CXX) $(LDFLAGS) -o shfd util.o shell_server.o file_server.o replica_server.o udp.o shfd.o


# Clean target: Remove object files and executable
clean:
	rm -f *~ *.o *.bak core \#*
	rm -f shfd client *.log *.pid *.txt

