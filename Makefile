CC=g++
CXX_COMPILE_FLAGS=-std=c++17 -O3 `pkg-config --cflags --libs gtkmm-3.0`

mandelbrot.o: mandelbrot.cpp
	$(CC) mandelbrot.cpp -o mandelbrot $(CXX_COMPILE_FLAGS)

clean:
	rm -f *.o mandelbrot
