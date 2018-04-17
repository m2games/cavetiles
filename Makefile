all:
	g++ -std=c++11 -Wall -Wextra -pedantic -fno-exceptions -fno-rtti -g \
	    -o cavetiles \
	    main.cpp glad.c imgui/imgui.cpp imgui/imgui_demo.cpp imgui/imgui_draw.cpp \
	    imgui/imgui_impl_glfw_gl3.cpp \
	    -I/usr/local/include \
	    -L/usr/local/Cellar -lglfw -ldl
