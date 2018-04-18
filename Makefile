COMM = g++ -std=c++11 -Wall -Wextra -pedantic -fno-exceptions -fno-rtti -g \
       -I/usr/local/include -L/usr/local/Cellar -L/usr/local/lib \
       -Wno-nested-anon-types -o cavetiles main.cpp  -lglfw -ldl

linux:
	${COMM} ./fmod/libfmod.so.10.4 -Wl,-rpath=./fmod

mac:
	${COMM} ./fmod/libfmod.dylib
