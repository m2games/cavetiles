COMM = g++ -std=c++11 -Wall -Wextra -pedantic -fno-exceptions -fno-rtti -g \
       -o cavetiles main.cpp  -lglfw -ldl -Wl,-rpath=./fmod

linux:
	${COMM} ./fmod/libfmod.so.10.4

mac:
	${COMM} -I/usr/local/include -L/usr/local/Cellar ./fmod/libfmod.dylib
