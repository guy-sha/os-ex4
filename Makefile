CXX=g++

all: $(patsubst %.cpp, %.out, $(wildcard *.cpp))

%.out: %.cpp
	$(CXX) $< -o $@

clean:
	rm *.out