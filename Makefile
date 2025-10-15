# Compiler
CXX = g++
# Compiler flags
CXXFLAGS = -std=c++11 -Wall -g

# Target executables
TARGETS = oss worker

# Default target
all: $(TARGETS)

oss: oss.cpp
	$(CXX) $(CXXFLAGS) -o oss oss.cpp

worker: worker.cpp
	$(CXX) $(CXXFLAGS) -o worker worker.cpp

# Clean up object files and executables
clean:
	rm -f $(TARGETS) *.o
