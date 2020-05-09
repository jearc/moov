#include <time.h>
#include <stdio.h>
#include <errno.h>
#include <GL/glew.h>
#include <SDL2/SDL.h>
#include <mpv/opengl_cb.h>
#include <unistd.h>
#include <fcntl.h>

#include "imgui/imgui.h"
#include "imgui/imgui_impl_sdl_gl3.h"
#include "moov.h"

#define MAX_MSG_LEN 1000

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
			ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
			ImGuiWindowFlags_NoSavedSettings);
	ImGui::BeginChild("LogRegion",
		ImVec2(0, -ImGui::GetItemsLineHeightWithSpacing()), false,
		ImGuiWindowFlags_NoScrollbar);
	for (size_t i = chatlog->msgfirst; i != chatlog->msgnext;
	     i = (i + 1) % CHAT_MAX_MESSAGE_COUNT) {
		message *msg = &chatlog->msgs[i];
		tm *now = localtime(&msg->time);
		char buf[20] = { 0 };
		strftime(buf, sizeof(buf), "%X", now);
		ImGui::TextWrapped("(%s) %s: %s", buf, msg->name, msg->text);
	}
	if (scroll_to_bottom)
		ImGui::SetScrollHere();
	ImGui::EndChild();
	ImGui::PushItemWidth(size.x - 8 * 2);
	static char CHAT_INPUT_BUF[256] = { 0 };
	if (ImGui::InputText("", CHAT_INPUT_BUF, 256,
		    ImGuiInputTextFlags_EnterReturnsTrue, NULL, NULL) &&
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
	while (read(0, &c, 1) != -1) {
		switch (c) {
		case EOF:
			break;
		case '\0': {
			char *text = logmsg(chatlog, buf, bufidx + 1);
			handlecmd(text, mpvh);
			new_msg = true;
			memset(buf, 0, sizeof buf);
			bufidx = 0;
			break;
		}
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

void explorewin(mpvhandler *mpvh, mpvinfo info)
{
	static bool display = true;
	ImGui::Begin("Explore", &display);

	playstate s = info.explore_state;

	if (ImGui::Button("Play/Pause")) {
		playstate s = info.explore_state;
		s.paused = !s.paused;
		mpvh_explore_set_state(mpvh, s);
	}

	float progress = s.time / info.duration;
	ImGui::ProgressBar(progress, ImVec2(380, 20));
	if (ImGui::IsItemClicked() && info.duration) {
		auto min = ImGui::GetItemRectMin();
		auto max = ImGui::GetItemRectMax();
		auto mouse = ImGui::GetMousePos();

		float fraction = (mouse.x - min.x) / (max.x - min.x);
		double time = fraction * info.duration;

		playstate s = info.explore_state;
		s.time = time;
		mpvh_explore_set_state(mpvh, s);
	}

	statusstr str = statestr(info, s);
	ImGui::Text("%s", str.str);

	if (ImGui::Button("Accept"))
		mpvh_explore_accept(mpvh);
	ImGui::SameLine();
	if (ImGui::Button("Cancel"))
		mpvh_explore_cancel(mpvh);

	ImGui::End();
}

void dbgwin(SDL_Window *win, mpvhandler *mpvh, mpvinfo info)
{
	static bool display = true;
	ImGui::Begin("Debug", &display);

	if (ImGui::Button("<"))
		sendmsg("PREV");
	ImGui::SameLine();
	ImGui::Text("T: %ld/%ld", info.state.track + 1, info.track_cnt);
	ImGui::SameLine();
	if (ImGui::Button(">"))
		sendmsg("NEXT");

	ImGui::Text("%s", info.title);

	if (ImGui::Button("Play"))
		sendmsg("PLAY");
	ImGui::SameLine();
	if (ImGui::Button("Pause"))
		sendmsg("PAUSE");

	ImGui::Text("%s", statestr(info, info.state).str);

	ImGui::Text("Delay: %.f", info.delay);

	if (info.sub_cnt > 1) {
		if (ImGui::Button("<"))
			mpvh_set_sub(mpvh, info.sub_curr - 1);
		ImGui::SameLine();
		ImGui::Text("S: %ld/%ld", info.sub_curr, info.sub_cnt);
		ImGui::SameLine();
		if (ImGui::Button(">"))
			mpvh_set_sub(mpvh, info.sub_curr + 1);
	}

	if (info.audio_cnt > 1) {
		if (ImGui::Button("<"))
			mpvh_set_audio(mpvh, info.audio_curr - 1);
		ImGui::SameLine();
		ImGui::Text("A: %ld/%ld", info.audio_curr, info.audio_cnt);
		ImGui::SameLine();
		if (ImGui::Button(">"))
			mpvh_set_audio(mpvh, info.audio_curr + 1);
	}

	if (ImGui::Button("Explore"))
		mpvh_explore(mpvh);
	ImGui::SameLine();
	ImGui::Text("Exploring: %d", info.exploring);
	if (info.exploring)
		explorewin(mpvh, info);

	if (ImGui::Button("Mute"))
		mpvh_toggle_mute(mpvh);
	ImGui::SameLine();
	ImGui::Text("Muted: %d", info.muted);

	if (ImGui::Button("Fullscreen"))
		toggle_fullscreen(win);

	ImGui::End();
}

struct seeker {
	double left, right;
	double last;
	double arg;
};

seeker seeker_new()
{
	seeker s;
	s.left = -1.0;
	s.right = -1.0;
	s.arg = -1.0;
	s.last = 5.0;

	return s;
}

bool handle_sdl_events(SDL_Window *win, mpvhandler *h)
{
	bool redraw = false;
	
	static double l = -1.0, r = -1.0;
	static double last = 5.0;
	static double arg = -1.0;

	SDL_Event e;
	while (SDL_PollEvent(&e)) {
		mpvinfo info = mpvh_getinfo(h);
		switch (e.type) {
		case SDL_QUIT:
			exit(EXIT_SUCCESS);
		case SDL_KEYDOWN:
			switch (e.key.keysym.sym) {
			case SDLK_F11:
				toggle_fullscreen(win);
				break;
			case SDLK_e:
				if (SDL_GetModState() & KMOD_CTRL) {
					mpvh_explore(h);
					arg = -1.0;
					l = r = -1.0;
					last = 5.0;
				}
				break;
			case SDLK_ESCAPE:
				mpvh_explore_cancel(h);
				break;
			case SDLK_RETURN: case SDLK_KP_ENTER:
				mpvh_explore_accept(h);
				break;
			case SDLK_PLUS: case SDLK_EQUALS: case SDLK_KP_PLUS:
				if (info.exploring) {
					l = info.explore_state.time;
					if (arg > 0) {
						info.explore_state.time += arg * 60;
						last = arg;
						arg = -1.0;
						r = -1.0;
					} else if (r >= 0) {
						info.explore_state.time = (info.explore_state.time + r) / 2;
					} else {
						info.explore_state.time += last * 60;
					}
					mpvh_explore_set_state(h, info.explore_state);
				}
				break;
			case SDLK_MINUS: case SDLK_KP_MINUS:
				if (info.exploring) {
					mpvinfo info = mpvh_getinfo(h);
					r = info.explore_state.time;
					if (arg > 0) {
						info.explore_state.time -= arg * 60;
						last = arg;
						arg = 0;
						l = -1.0;
					} else if (l >= 0) {
						info.explore_state.time = (info.explore_state.time + l) / 2;
					} else {
						info.explore_state.time -= last * 60;
					}
					mpvh_explore_set_state(h, info.explore_state);
				}
				break;
			case SDLK_1: case SDLK_2: case SDLK_3: case SDLK_4: case SDLK_5:
			case SDLK_6: case SDLK_7: case SDLK_8: case SDLK_9: case SDLK_0:
				if (e.key.keysym.sym == SDLK_5 && SDL_GetModState() & KMOD_SHIFT) {
					if (info.exploring && arg >= 0) {
						mpvinfo info = mpvh_getinfo(h);
						info.explore_state.time = info.duration * 0.01 * arg;
						mpvh_explore_set_state(h, info.explore_state);
						arg = -1.0;
						l = r = -1.0;
						last = 5.0;
					}
				} else {	
					if (arg < 0)
						arg = 0;
					arg *= 10;
					arg += e.key.keysym.sym - SDLK_0;
				}
				break;
			case SDLK_KP_1: case SDLK_KP_2: case SDLK_KP_3: case SDLK_KP_4: case SDLK_KP_5:
			case SDLK_KP_6: case SDLK_KP_7: case SDLK_KP_8: case SDLK_KP_9: case SDLK_KP_0:
				if (arg < 0)
					arg = 0;
				arg *= 10;
				if (e.key.keysym.sym != SDLK_KP_0)
					arg += e.key.keysym.sym - SDLK_KP_1 + 1;
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
			if (!info.exploring || !(e.type == SDL_KEYUP || e.type == SDL_KEYDOWN)) 
				ImGui_ImplSdlGL3_ProcessEvent(&e);
		}
	}

	return redraw;
}

SDL_Window *init_window()
{
	if (SDL_Init(SDL_INIT_VIDEO) < 0)
		die("SDL init failed");
	SDL_GL_SetAttribute(
		SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
	SDL_GL_SetAttribute(
		SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
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
		SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
	SDL_GL_CreateContext(window);
	glewInit();
	ImGui_ImplSdlGL3_Init(window);

	return window;
}

int main(int argc, char **argv)
{
	int start_track = 0;
	double start_time = 0;
	int opt;
	while ((opt = getopt(argc, argv, "s:i:")) != -1) {
		switch (opt) {
		case 'i':
			start_track = atoi(optarg);
			break;
		case 's':
			start_time = parsetime(optarg);
			break;
		default:
			exit(EXIT_FAILURE);
		}
	}
	int filec = argc - optind;
	char **filev = argv + optind;
	if (!filec)
		die("no files\n");

	fcntl(0, F_SETFL, O_NONBLOCK);

	SDL_Window *window = init_window();
	mpvhandler *mpvh = mpvh_create(filec, filev, start_track, start_time);
	mpv_opengl_cb_context *mpv_gl = mpvh_get_opengl_cb_api(mpvh);
	mpv_opengl_cb_init_gl(mpv_gl, NULL, get_proc_address_mpv, NULL);

	bool mpv_redraw = false;
	mpv_opengl_cb_set_update_callback(mpv_gl, on_mpv_redraw, &mpv_redraw);

	chatlog chatlog = init_chatlog();

	int64_t t_last = 0, t_now = 0;
	while (1) {
		SDL_Delay(10);
		t_now = SDL_GetPerformanceCounter();
		double delta = (t_now - t_last) /
			       (double)SDL_GetPerformanceFrequency();
		if (delta <= 1 / 60.0)
			continue;
		t_last = t_now;

		bool scroll_to_bottom = readstdin(&chatlog, mpvh);

		bool redraw = false;
		if (mpv_redraw) {
			redraw = true;
			mpv_redraw = false;
		}
		if (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS)
			redraw = true;

		if (handle_sdl_events(window, mpvh))
			redraw = true;
		mpvh_update(mpvh);

		//if (!redraw)
		//	continue;

		int w, h;
		SDL_GetWindowSize(window, &w, &h);
		glClear(GL_COLOR_BUFFER_BIT);
		mpv_opengl_cb_draw(mpv_gl, 0, w, -h);
		ImGui_ImplSdlGL3_NewFrame(window);
		chatbox(&chatlog, scroll_to_bottom);
		mpvinfo info = mpvh_getinfo(mpvh);
		dbgwin(window, mpvh, info);
		glViewport(0, 0, (int)ImGui::GetIO().DisplaySize.x,
			(int)ImGui::GetIO().DisplaySize.y);
		ImGui::Render();
		SDL_GL_SwapWindow(window);
	}

	return 0;
}
