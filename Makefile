COMM = g++ -std=c++11 -Wall -Wextra -pedantic -fno-exceptions -fno-rtti -g \
       -o cavetiles main.cpp -I/usr/local/include -L/usr/local/Cellar -lglfw -ldl \
       -Wl,-rpath=./fmod

linux:
	${COMM} ./fmod/libfmod.so.10.4

mac:
	${COMM} ./fmod/libfmod.dylib
