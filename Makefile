CC=g++
CXX_COMPILE_FLAGS=-std=c++17 -O3 `pkg-config --cflags --libs gtkmm-3.0`
#CXX_LINK_FLAGS=-gencode arch=compute_72,code=sm_72 `pkg-config --libs gtkmm-3.0`

#all: mandelbrot.o
#	$(CC) $(CXX_LINK_FLAGS) mandelbrot.o -o mandelbrot

mandelbrot.o: mandelbrot.cpp
	$(CC) mandelbrot.cpp -o mandelbrot $(CXX_COMPILE_FLAGS)

clean:
	rm -f *.o mandelbrot
