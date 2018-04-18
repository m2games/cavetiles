all:
	g++ -std=c++11 -Wall -Wextra -pedantic -fno-exceptions -fno-rtti -g \
	    -o cavetiles main.cpp -I/usr/local/include -L/usr/local/Cellar -lglfw -ldl
