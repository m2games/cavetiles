all:
	g++ -Wall -Wextra -pedantic -fno-exceptions -fno-rtti -O3 \
	    -I/usr/local/include \
	    -o cavetiles \
	    main.cpp glad.c \
	    -L/usr/local/lib -lglfw -ldl
