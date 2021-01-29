OBJS = main.o mpvh.o util.o ui.o chat.o
OBJS += ./imgui/imgui_impl_sdl.o ./imgui/imgui.o ./imgui/imgui_draw.o
OBJS += ./imgui/imgui_impl_opengl3.o ./imgui/imgui_widgets.o
CFLAGS = -fPIC -pedantic -Wall -Wextra -Ofast -ffast-math
CXXFLAGS = --std=c++2a
LIBS = -lGL -ldl -lSDL2 -lmpv -lGLEW -lGLU

all: moov

moov: 
	g++ -std=c++2a main.cpp mpvh.cpp util.cpp ui.cpp chat.cpp imgui/imgui_impl_sdl.cpp imgui/imgui.cpp imgui/imgui_draw.cpp imgui/imgui_impl_opengl3.cpp imgui/imgui_widgets.cpp -o moov -lGL -ldl -lSDL2 -lmpv -lGLEW -lGLU -lm -lpthread

clean:
	rm moov $(OBJS)

test: all
	@./test.py

install: all
	@mkdir -p /usr/local/bin
	@echo 'Installing moov to /usr/local/bin.'
	@cp -f moov /usr/local/bin
	@chmod 755 /usr/local/bin/moov
	@mkdir -p /usr/local/share/moov
	@echo 'Installing fonts to /usr/local/share/moov.'
	@cp -f Roboto-Medium.ttf MaterialIcons-Regular.ttf /usr/local/share/moov

uninstall: all
	@echo 'Removing moov from /usr/local/bin.'
	@rm -f /usr/local/bin/moov
	@echo 'Removing moov directory from /usr/local/share.'
	@rm -rf /usr/local/share/moov
