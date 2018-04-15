CC= g++ -std=c++11 -Wall -Wextra -pedantic -fno-exceptions -fno-rtti -O3

all: stb_image.o
	$(CC) main.cpp glad.c stb_image.o -o cavetiles \
	    -I/usr/local/include \
	    -L/usr/local/Cellar -lglfw -ldl

stb_image.o:
	$(CC) -c stb_image.c

clean:
	rm cavetiles stb_image.o
