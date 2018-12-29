#include <time.h>
#include <stdio.h>
#include <errno.h>
#include <GL/glew.h>
#include <SDL2/SDL.h>
#include <mpv/client.h>
#include <mpv/opengl_cb.h>
#include <unistd.h>
#include <fcntl.h>

#include "mpvhandler.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_sdl_gl3.h"
#include "util.h"
#include "chat.h"
#include "cmd.h"
#include "ui.h"

#define MAX_MSG_LEN 1000

void die(const char *msg)
{
	fprintf(stderr, "error: %s\n", msg);
	exit(1);
}

void *get_proc_address_mpv(void *fn_ctx, const char *name)
{
	UNUSED(fn_ctx);

	return SDL_GL_GetProcAddress(name);
}

void on_mpv_redraw(void *mpv_redraw)
{
	*(bool *)mpv_redraw = true;
}

void toggle_fullscreen(SDL_Window *win)
{
	bool fullscreen = SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN;
	SDL_SetWindowFullscreen(
		win, fullscreen ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
	SDL_ShowCursor(SDL_ENABLE);
}

void chatbox(chatlog *chatlog, bool scroll_to_bottom)
{
	int margin = 8;
	ImVec2 size = ImVec2(400, 200);
	ImVec2 display_size = ImGui::GetIO().DisplaySize;
	ImVec2 pos = ImVec2((int)display_size.x - size.x - margin,
	                    (int)display_size.y - size.y - margin);

	bool display = true;
	ImGui::SetNextWindowSize(size, ImGuiSetCond_FirstUseEver);
	ImGui::SetNextWindowPos(pos);
	ImGui::Begin("ChatBox", &display, size, 0.7f,
		     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
			     ImGuiWindowFlags_NoMove |
			     ImGuiWindowFlags_NoScrollbar |
			     ImGuiWindowFlags_NoSavedSettings);
	ImGui::BeginChild("LogRegion",
			  ImVec2(0, -ImGui::GetItemsLineHeightWithSpacing()),
			  false, ImGuiWindowFlags_NoScrollbar);
	for (size_t i = 0; i < chatlog->msg_cnt; i++) {
		Message *msg = &chatlog->msg[i];
		tm *now = localtime(&msg->time);
		char buf[20] = { 0 };
		strftime(buf, sizeof(buf), "%X", now);
		ImGui::TextWrapped("[%s] %s: %s", buf, msg->from, msg->text);
	}
	if (scroll_to_bottom)
		ImGui::SetScrollHere();
	ImGui::EndChild();
	ImGui::PushItemWidth(size.x - 8 * 2);
	static char CHAT_INPUT_BUF[256] = { 0 };
	if (ImGui::InputText("", CHAT_INPUT_BUF, 256,
			     ImGuiInputTextFlags_EnterReturnsTrue, NULL,
			     NULL) &&
	    strlen(CHAT_INPUT_BUF)) {
		sendmsg(CHAT_INPUT_BUF);
		strcpy(CHAT_INPUT_BUF, "");
	}
	ImGui::PopItemWidth();
	if (ImGui::IsItemHovered() ||
	    (ImGui::IsRootWindowOrAnyChildFocused() &&
	     !ImGui::IsAnyItemActive() && !ImGui::IsMouseClicked(0)))
		ImGui::SetKeyboardFocusHere(-1);
	ImGui::End();
}

bool readstdin(chatlog *chatlog, mpvhandler *mpvh)
{
	bool new_msg = false;

	static char buf[MAX_MSG_LEN];
	memset(buf, 0, sizeof buf);
	static size_t bufidx = 0;
	
	char c;
	long res;
	while (read(0, &c, 1) != -1) {
		switch (c) {
		case EOF:
			break;
		case '\0':
			char *username, *text;
			splitinput(buf, &username, &text);
			logmsg(chatlog, username, text);
			handlecmd(text, mpvh);
			new_msg = true;
			memset(buf, 0, sizeof buf);
			bufidx = 0;
			break;
		default:
			bool significant = bufidx != 0 || !isspace(c);
			bool space_available = bufidx < (sizeof buf) - 1;
			if (significant && space_available)
				buf[bufidx++] = c;
			break;
		}
	}
	
	return new_msg;
}

bool handle_sdl_events(SDL_Window *win)
{
	bool redraw = false;
	
	SDL_Event e;
	while (SDL_PollEvent(&e)) {
		switch (e.type) {
		case SDL_QUIT:
			exit(0);
		case SDL_KEYDOWN:
			switch (e.key.keysym.sym) {
			case SDLK_F11:
				toggle_fullscreen(win);
				break;
			default:
				ImGui_ImplSdlGL3_ProcessEvent(&e);
			}
			break;
		case SDL_WINDOWEVENT:
			if (e.window.event == SDL_WINDOWEVENT_EXPOSED)
				redraw = true;
			break;
		default:
			ImGui_ImplSdlGL3_ProcessEvent(&e);
		}
	}
	
	return redraw;
}

struct argconf {
	double seekto = 0.0;
	bool resume = false;
	char *uri[100] = {};
	size_t uri_cnt = 0;
};

argconf parseargs(int argc, char *argv[])
{
	argconf conf;

	if (argc <= 1)
		return conf;

	bool expecting_seekto = false;
	for (int i = 1; i < argc; i++) {
		fprintf(stderr, "arg: %s\n", argv[i]);
		if (argv[i][0] == '-') {
			switch (argv[i][1]) {
			case 'r':
				conf.resume = true;
				break;
			case 's':
				expecting_seekto = true;
				break;
			default:
				die("invalid arg");
				break;
			}
		} else if (expecting_seekto) {
			conf.seekto = parsetime(argv[i], strlen(argv[i]));
		} else if (conf.uri_cnt < 100) {
			conf.uri[conf.uri_cnt++] = argv[i];
		}
	}

	return conf;
}

SDL_Window *init_window()
{
	if (SDL_Init(SDL_INIT_VIDEO) < 0)
		die("SDL init failed");
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS,
			    SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
			    SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
	SDL_DisplayMode current;
	SDL_GetCurrentDisplayMode(0, &current);
	SDL_Window *window;
	window = SDL_CreateWindow("Moov", SDL_WINDOWPOS_CENTERED,
				  SDL_WINDOWPOS_CENTERED, 1280, 720,
				  SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN |
					  SDL_WINDOW_RESIZABLE);
	SDL_GL_CreateContext(window);
	glewInit();
	ImGui_ImplSdlGL3_Init(window);
	
	return window;
}

int main(int argc, char **argv)
{
	argconf conf = parseargs(argc, argv);
	if (conf.uri_cnt == 0)
		die("no uris");
	
	fcntl(0, F_SETFL, O_NONBLOCK);

	SDL_Window *window = init_window();
	mpvhandler *mpvh = mpvh_create(conf.uri[0]);
	mpv_opengl_cb_context *mpv_gl = mpvh_get_opengl_cb_api(mpvh);

	ImGuiIO &io = ImGui::GetIO();
	io.Fonts->AddFontFromFileTTF(
		"/usr/local/share/moov/liberation_sans.ttf", 14.0f);

	mpv_opengl_cb_init_gl(mpv_gl, NULL, get_proc_address_mpv, NULL);

	bool mpv_redraw = false;
	mpv_opengl_cb_set_update_callback(mpv_gl, on_mpv_redraw, &mpv_redraw);
	
	chatlog chatlog;

	int64_t last_tick = 0, current_tick = 0;
	while (1) {
		SDL_Delay(10);
		current_tick = SDL_GetTicks();
		if (current_tick - last_tick <= 16)
			continue;
		last_tick = current_tick;

		bool scroll_to_bottom = readstdin(&chatlog, mpvh);

		bool redraw = false;
		if (mpv_redraw) {
			redraw = true;
			mpv_redraw = false;
		}
		if (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS)
			redraw = true;

		if (handle_sdl_events(window))
			redraw = true;
		mpvh_update(mpvh);

		if (!redraw)
			continue;
			
		int w, h;
		SDL_GetWindowSize(window, &w, &h);
		glClear(GL_COLOR_BUFFER_BIT);
		mpv_opengl_cb_draw(mpv_gl, 0, w, -h);
		ImGui_ImplSdlGL3_NewFrame(window);
		chatbox(&chatlog, scroll_to_bottom);
		glViewport(0, 0, (int)ImGui::GetIO().DisplaySize.x,
			   (int)ImGui::GetIO().DisplaySize.y);
		ImGui::Render();
		SDL_GL_SwapWindow(window);
	}

	return 0;
}
