COMM = g++ -std=c++11 -Wall -Wextra -pedantic -Wno-class-memaccess -fno-exceptions -fno-rtti -g \
       -I/usr/local/include -L/usr/local/Cellar -L/usr/local/lib \
       -o cavetiles main.cpp -lglfw -ldl

linux: server
	${COMM} ./fmod/libfmod.so.10.4 -Wl,-rpath=./fmod

mac: server
	${COMM} ./fmod/libfmod.dylib

.PHONY: server
server:
	g++ -std=c++11 -Wall -Wextra -pedantic -fno-rtti -fno-exceptions -g server.cpp -o server
