OBJS = main.o mpvh.o util.o ui.o chat.o
OBJS += ./imgui/imgui_impl_sdl.o ./imgui/imgui.o ./imgui/imgui_draw.o
OBJS += ./imgui/imgui_impl_opengl3.o ./imgui/imgui_widgets.o
CFLAGS = -fPIC -pedantic -Wall -Wextra -Ofast -ffast-math
CXXFLAGS = --std=c++2a
LIBS = -lGL -ldl -lSDL2 -lmpv -lGLEW -lGLU

all: moov

moov:
	g++ -Ofast -std=c++2a main.cpp mpvh.cpp util.cpp ui.cpp chat.cpp exepath.cpp Timer.cpp imgui/imgui_impl_sdl.cpp imgui/imgui.cpp imgui/imgui_draw.cpp imgui/imgui_impl_opengl3.cpp imgui/imgui_widgets.cpp -o moov -lGL -ldl -lSDL2 -lSDL2_image -lmpv -lGLEW -lGLU -lm -lpthread

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
	@echo 'Installing assets to /usr/local/share/moov.'
	@cp -f Roboto-Medium.ttf MaterialIcons-Regular.ttf icon.png /usr/local/share/moov

installgajim:
	@echo 'Installing moov plugin for Gajim'
	@mkdir -p ~/.local/share/gajim/plugins/moovgajim
	@cp -f contrib/moovgajim/manifest.ini ~/.local/share/gajim/plugins/moovgajim
	@cp -f contrib/moovgajim/__init__.py ~/.local/share/gajim/plugins/moovgajim
	@cp -f contrib/moovgajim/moovdb.py ~/.local/share/gajim/plugins/moovgajim
	@cp -f contrib/moovgajim/plugin.py ~/.local/share/gajim/plugins/moovgajim
	@cp -f contrib/moovgajim/config_dialog.py ~/.local/share/gajim/plugins/moovgajim
	@cp -f contrib/moovgajim/moov.py ~/.local/share/gajim/plugins/moovgajim

uninstall:
	@echo 'Removing moov from /usr/local/bin.'
	@rm -f /usr/local/bin/moov
	@echo 'Removing moov directory from /usr/local/share.'
	@rm -rf /usr/local/share/moov

uninstallgajim:
	@echo 'Removing moov plugin for Gajim'
	@rm -rf ~/.local/share/gajim/plugins/moovgajim
