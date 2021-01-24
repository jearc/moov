#include <time.h>
#include <errno.h>
#include <GL/glew.h>
#include <SDL2/SDL.h>
#include <mpv/render.h>
#include <mpv/render_gl.h>
#include <vector>
#include <string>
#include <iostream>
#include <mutex>
#include <queue>
#include <string_view>
#include <charconv>
#include <algorithm>

#include "imgui/imgui.h"
#include "imgui/imgui_impl_sdl.h"
#include "imgui/imgui_impl_opengl3.h"
#include "moov.h"
#include "ui.h"
#include "json.h"

using json = nlohmann::json;

ImFont *text_font;
ImFont *icon_font;

bool focus_chat = false;

void *get_proc_address_mpv(void *fn_ctx, const char *name)
{
	return SDL_GL_GetProcAddress(name);
}

void on_mpv_redraw(void *mpv_redraw)
{
	//*(bool *)mpv_redraw = true;
}

bool is_fullscreen(SDL_Window *win)
{
	return SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN;
}

void toggle_fullscreen(SDL_Window *win)
{
	SDL_SetWindowFullscreen(
		win, is_fullscreen(win) ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
	SDL_ShowCursor(SDL_ENABLE);
}

void chatbox(Chat &c)
{
	auto chat_window_name = "chat_window";
	auto chat_window_flags =
		ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMouseInputs;
	auto chat_window_size = ImVec2(300, 400);
	auto chat_window_pos = ImVec2(300, 100);

	auto chat_log_size = ImVec2(300, 260);
	auto chat_log_pos = chat_window_pos;

	auto chat_input_size = ImVec2(300, 20);
	auto chat_input_pos = ImVec2(chat_window_pos.x, chat_window_pos.y + chat_log_size.y);

	ImGui::SetNextWindowSize(chat_window_size);
	ImGui::SetNextWindowPos(chat_window_pos);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);

	bool display = true;
	ImGui::Begin(chat_window_name, &display, chat_window_flags);

	auto draw_list = ImGui::GetWindowDrawList();
	ImVec2 message_pos = chat_log_pos;
	message_pos.y = chat_log_pos.y + chat_log_size.y;
	auto messages = c.messages();
	for (auto it = messages.rbegin(); it != messages.rend(); it++)
	{
		auto &msg = *it;

		double opacity;
		{
			double K = 5.0;
			double F = 3.0;
			auto m = msg.time;
			auto e = c.get_last_end_scroll_time();
			auto n = std::chrono::steady_clock::now();
			double x = std::chrono::duration<double>(n-std::max(m, e)).count();
			opacity = 1.0 - std::min(std::max(0.0, (x-K)/F), 1.0);
		}

		if (opacity == 0)
			break;

		auto text_size = ImGui::CalcTextSize(msg.text.c_str(), nullptr, false, chat_log_size.x);
		message_pos.y -= text_size.y;
		if (message_pos.y < chat_log_pos.y)
			break;

		auto fade_color = [](uint32_t col, double factor) {
			uint32_t alpha = col >> 24;
			uint32_t scaled_alpha = std::round(factor * alpha);
			return (col & 0x00FFFFFF) | (scaled_alpha << 24);
		};

		uint32_t fg = fade_color(msg.fg, opacity*opacity);
		uint32_t bg = fade_color(msg.bg, opacity);

		ImVec2 rect_p_max(message_pos.x + text_size.x, message_pos.y + text_size.y);
		draw_list->AddRectFilled(message_pos, rect_p_max, bg);
		draw_list->AddText(nullptr, 0.0f, message_pos, fg, msg.text.c_str(), msg.text.c_str() + msg.text.size(), chat_log_size.x, nullptr);
	}

	static ImVec4 input_bg = ImVec4(0, 0, 0, 0);

	static std::array<char, 1024> buf;
	ImGui::SetNextItemWidth(chat_input_size.x);
	ImGui::SetCursorPos(ImVec2(0, 260));
	ImGui::PushStyleColor(ImGuiCol_FrameBg, input_bg);
	if (ImGui::InputText("", buf.data(), 1024, ImGuiInputTextFlags_EnterReturnsTrue))
	{
		json j;
		j["type"] = "user_input";
		j["text"] = buf.data();
		std::cout << j << std::endl;
		std::fill(buf.begin(), buf.end(), 0);
		focus_chat = false;
		input_bg = ImVec4(0, 0, 0, 0);
	}
	ImGui::PopStyleColor();
	if (focus_chat)
	{
		ImGui::SetKeyboardFocusHere(-1);
		focus_chat = false;
		input_bg = ImVec4(0.5, 0.5, 0.5, 0.5);
	}

	ImGui::End();
	ImGui::PopStyleVar(2);
}

void send_control(int64_t pos, double time, bool paused)
{
	json res;
	res["type"] = "control";
	res["playlist_position"] = pos;
	res["time"] = time;
	res["paused"] = paused;
	std::cout << res << std::endl;
}

void read_input(std::mutex &m, std::queue<json> &q)
{
	std::string l;
	while (std::getline(std::cin, l))
	{
		std::lock_guard<std::mutex> g(m);
		try {
			q.push(json::parse(l));
		} catch (std::exception &e) {
			std::cerr << e.what() << std::endl;	
		}
	}
}

uint32_t decode_color(std::string_view string)
{
	if (!(string.length() == 7 || string.length() == 9) || string[0] != '#')
		return 0;

	uint8_t channels[4];
	channels[3] = 0xFF;

	for (int i = 1; i+1 < string.length(); i += 2)
	{
		std::from_chars(&string[i], &string[i]+2, channels[(i-1)/2], 16);
	}

	return *(uint32_t *)channels;
}

void handle_instruction(Player &p, Chat &c, json &j)
{
	auto type_it = j.find("type");
	if (type_it == j.end())
		return;

	auto &type = *type_it;
	if (type == "pause")
	{
		bool paused = j.at("paused");
		p.pause(paused);
	}
	else if (type == "seek")
	{
		double time = j.at("time");
		p.set_time(time);
	}
	else if (type == "message")
	{
		std::string bg_str = j.at("bg_color");
		std::string fg_str = j.at("fg_color");
		std::string msg = j.at("message");
		c.add_message({
			msg,
			std::chrono::steady_clock::now(),
			decode_color(fg_str),
			decode_color(bg_str)
		});
	}
	else if (type == "add_file")
	{
		std::string path = j.at("file_path");
		p.add_file(path.c_str());
	}
	else if (type == "set_playlist_position")
	{
		int64_t pos = j.at("position");
		p.set_pl_pos(pos);
	}
	else if (type == "request_status")
	{
		int request_id = j.at("request_id");
		auto info = p.get_info();
		json res;
		res["type"] = "status";
		res["request_id"] = request_id;
		res["playlist_position"] = info.pl_pos;
		res["playlist_count"] = info.pl_count;
		res["time"] = info.c_time;
		res["paused"] = info.c_paused;
		std::cout << res << std::endl;
	}
	else if (type == "close")
	{
		die("closed by ipc");
	}
}

bool button(ImRect rect, ImVec2 padding, ImFont *font, const char *label)
{
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, padding);
	ImGui::PushFont(font);
	ImGui::SetCursorPos(rect.pos);
	bool clicked = ImGui::Button((const char *)label, rect.size);
	ImGui::PopFont();
	ImGui::PopStyleVar(1);
	return clicked;
}

void text(ImRect rect, ImVec2 padding, ImFont *font, const char *text)
{
	ImGui::PushFont(font);
	ImGui::SetCursorPos(rect.pos + padding);
	ImGui::BeginChild(text, rect.size - 2 * padding);
	ImGui::Text("%s", text);
	ImGui::EndChild();
	ImGui::PopFont();
}

void ui(SDL_Window *sdl_win, Player &p, Layout &l)
{
	auto info = p.get_info();

	ImGui::SetNextWindowPos(l.master_win.pos);
	ImGui::SetNextWindowSize(l.master_win.size);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
	ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0);

	bool display = true;
	ImGui::Begin("Master", &display,
		ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground |
			ImGuiWindowFlags_NoSavedSettings);

	ImDrawList *draw_list = ImGui::GetWindowDrawList();

	draw_list->AddRectFilled(l.infobar.pos, l.infobar.pos + l.infobar.size,
		0xbb000000, 0.0, ImDrawCornerFlags_None);

	auto pp_but_str = info.c_paused ? PLAY_ICON : PAUSE_ICON;
	if (button(l.pp_but, l.major_padding, icon_font, pp_but_str)) {
		p.pause(!info.c_paused);
		info = p.get_info();
		send_control(info.pl_pos, info.c_time, info.c_paused);
	}

	text(l.time, l.major_padding, text_font, sec_to_timestr(info.c_time).c_str());

	if (button(l.prev_but, l.major_padding, icon_font, PLAYLIST_PREVIOUS_ICON)) {
		p.set_pl_pos(info.pl_pos - 1);
		info = p.get_info();
		send_control(info.pl_pos, info.c_time, info.c_paused);
	}

	char pl_status_str_buf[10];
	snprintf(pl_status_str_buf, 10, "%d/%d", info.pl_pos + 1, info.pl_count);
	text(l.pl_status, l.major_padding, text_font, pl_status_str_buf);

	if (button(l.next_but, l.major_padding, icon_font, PLAYLIST_NEXT_ICON)) {
		p.set_pl_pos(info.pl_pos + 1);
		info = p.get_info();
		send_control(info.pl_pos, info.c_time, info.c_paused);
	}

	text(l.title, l.major_padding, text_font, info.title.c_str());

	if (button(l.sub_prev_but, l.minor_padding, icon_font, LEFT_ICON))
		p.set_sub(info.sub_pos - 1);
	text(l.sub_icon, l.minor_padding, icon_font, SUBTITLE_ICON);
	char sub_pos_str_buf[10];
	snprintf(sub_pos_str_buf, 10, " %d/%d", info.sub_pos, info.sub_count);
	text(l.sub_status, l.minor_padding, text_font, sub_pos_str_buf);
	if (button(l.sub_next_but, l.minor_padding, icon_font, RIGHT_ICON))
		p.set_sub(info.sub_pos - 1);

	if (button(l.audio_prev_but, l.minor_padding, icon_font, LEFT_ICON))
		p.set_audio(info.audio_pos - 1);
	text(l.audio_icon, l.minor_padding, icon_font, AUDIO_ICON);
	char audio_pos_str_buf[10];
	snprintf(audio_pos_str_buf, 10, " %d/%d", info.audio_pos, info.audio_count);
	text(l.audio_status, l.minor_padding, text_font, audio_pos_str_buf);
	if (button(l.audio_next_but, l.minor_padding, icon_font, RIGHT_ICON))
		p.set_audio(info.audio_pos + 1);

	auto mute_str = info.muted ? MUTED_ICON : UNMUTED_ICON;
	if (button(l.mute_but, l.major_padding, icon_font, mute_str))
		p.toggle_mute();

	auto fullscr_str = is_fullscreen(sdl_win) ? UNFULLSCREEN_ICON : FULLSCREEN_ICON;
	if (button(l.fullscr_but, l.major_padding, icon_font, fullscr_str))
		toggle_fullscreen(sdl_win);

	ImGui::End();
	ImGui::PopStyleVar(3);
}

void inputwin()
{
	bool display = true;
	ImGui::Begin("Input", &display, 0);

	static char buf[1000] = { 0 };
	if (ImGui::InputText(
			"Input: ", buf, 1000, ImGuiInputTextFlags_EnterReturnsTrue)) {
		json j;
		j["type"] = "user_input";
		j["message"] = buf;
		std::cout << j << std::endl;
	}
	ImGui::End();
}

void explorewin(Player &mpvh)
{
	PlayerInfo i = mpvh.get_info();

	static bool display = true;
	ImGui::Begin("Explore", &display, 0);

	if (ImGui::Button("Play/Pause"))
		mpvh.toggle_explore_paused();

	float progress = i.e_time / i.duration;
	ImGui::ProgressBar(progress, ImVec2(380, 20));
	if (ImGui::IsItemClicked() && i.duration) {
		auto min = ImGui::GetItemRectMin();
		auto max = ImGui::GetItemRectMax();
		auto mouse = ImGui::GetMousePos();

		float fraction = (mouse.x - min.x) / (max.x - min.x);
		double time = fraction * i.duration;

		mpvh.set_explore_time(time);
	}

	ImGui::Text(
		"%s", statestr(i.e_time, i.e_paused, i.pl_pos, i.pl_count).c_str());

	if (ImGui::Button("Accept"))
		mpvh.explore_accept();
	ImGui::SameLine();
	if (ImGui::Button("Cancel"))
		mpvh.explore_cancel();

	ImGui::End();
}

void dbgwin(SDL_Window *win, Player &mpvh)
{
	PlayerInfo i = mpvh.get_info();

	static bool display = true;
	ImGui::Begin("Debug", &display, 0);

	if (ImGui::Button("<")) {
		auto info = mpvh.get_info();
		mpvh.set_pl_pos(info.pl_pos - 1);
		info = mpvh.get_info();
		send_control(info.pl_pos, info.c_time, info.c_paused);
	}
	ImGui::SameLine();
	ImGui::Text("T: %ld/%ld", i.pl_pos + 1, i.pl_count);
	ImGui::SameLine();
	if (ImGui::Button(">")) {
		auto info = mpvh.get_info();
		mpvh.set_pl_pos(info.pl_pos + 1);
		info = mpvh.get_info();
		send_control(info.pl_pos, info.c_time, info.c_paused);
	}

	ImGui::Text("%s", i.title.c_str());

	if (ImGui::Button("Play")) {
		mpvh.pause(false);
		auto info = mpvh.get_info();
		send_control(info.pl_pos, info.c_time, info.c_paused);
	}
	ImGui::SameLine();
	if (ImGui::Button("Pause")) {
		mpvh.pause(true);
		auto info = mpvh.get_info();
		send_control(info.pl_pos, info.c_time, info.c_paused);
	}

	ImGui::Text(
		"%s", statestr(i.c_time, i.c_paused, i.pl_pos, i.pl_count).c_str());

	if (!i.exploring)
		ImGui::Text("Delay: %.f", i.delay);

	if (ImGui::Button("<"))
		mpvh.set_sub(i.sub_pos - 1);
	ImGui::SameLine();
	ImGui::Text("S: %ld/%ld", i.sub_pos, i.sub_count);
	ImGui::SameLine();
	if (ImGui::Button(">"))
		mpvh.set_sub(i.sub_pos + 1);

	if (ImGui::Button("<"))
		mpvh.set_audio(i.audio_pos - 1);
	ImGui::SameLine();
	ImGui::Text("A: %ld/%ld", i.audio_pos, i.audio_count);
	ImGui::SameLine();
	if (ImGui::Button(">"))
		mpvh.set_audio(i.audio_pos + 1);

	if (ImGui::Button("Explore"))
		mpvh.explore();
	ImGui::SameLine();
	ImGui::Text("Exploring: %d", i.exploring);
	if (i.exploring)
		explorewin(mpvh);

	if (ImGui::Button("Mute"))
		mpvh.toggle_mute();
	ImGui::SameLine();
	ImGui::Text("Muted: %d", i.muted);

	if (ImGui::Button("Fullscreen"))
		toggle_fullscreen(win);

	ImGui::End();
}

bool handle_sdl_events(SDL_Window *win, Chat &c, int &mouse_x, int &mouse_y, bool &click)
{
	bool redraw = false;
	click = false;

	SDL_GetMouseState(&mouse_x, &mouse_y);

	SDL_Event e;
	while (SDL_PollEvent(&e)) {
		switch (e.type) {
		case SDL_QUIT:
			exit(EXIT_SUCCESS);
		case SDL_MOUSEBUTTONDOWN:
			if (e.button.button == SDL_BUTTON_LEFT)
				click = true;
			ImGui_ImplSDL2_ProcessEvent(&e);
			break;
		case SDL_KEYDOWN:
			switch (e.key.keysym.sym) {
			case SDLK_F11:
				toggle_fullscreen(win);
				break;
			case SDLK_RETURN:
				focus_chat = true;
				ImGui_ImplSDL2_ProcessEvent(&e);
				break;
			default:
				ImGui_ImplSDL2_ProcessEvent(&e);
			}
			break;
		case SDL_MOUSEWHEEL:
			if (e.wheel.y < 0)
				c.scroll_down();
			else if (e.wheel.y > 0)
				c.scroll_up();
		case SDL_WINDOWEVENT:
			if (e.window.event == SDL_WINDOWEVENT_EXPOSED)
				redraw = true;
			break;
		default:
			ImGui_ImplSDL2_ProcessEvent(&e);
		}
	}

	return redraw;
}

SDL_Window *init_window(float font_size)
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
	SDL_Window *window = SDL_CreateWindow("Moov", SDL_WINDOWPOS_CENTERED,
		SDL_WINDOWPOS_CENTERED, 1280, 720,
		SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
	SDL_GL_CreateContext(window);
	SDL_GLContext gl_context = SDL_GL_CreateContext(window);
	glewInit();

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsClassic();
	ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
	ImGui_ImplOpenGL3_Init("#version 150");

	ImGuiIO &io = ImGui::GetIO();
	text_font = io.Fonts->AddFontFromFileTTF(
		"Roboto-Medium.ttf",
		font_size);

	static const ImWchar icons_ranges[] = { 0xe000, 0xeb4c, 0 };
	ImFontConfig icons_config;
	icons_config.PixelSnapH = true;
	icon_font = io.Fonts->AddFontFromFileTTF(
		"MaterialIcons-Regular.ttf", font_size, &icons_config, icons_ranges);

	return window;
}

int main(int argc, char **argv)
{
	float font_size = 30;
	SDL_Window *window = init_window(font_size);
	auto mpvh = Player();

	mpv_render_context *mpv_ctx;
	mpv_opengl_init_params gl_init_params{ get_proc_address_mpv, nullptr, nullptr };
	mpv_render_param render_params[] = {
		{ MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL) },
		{ MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl_init_params },
		{ MPV_RENDER_PARAM_INVALID, nullptr }
	};
	mpvh.create_render_context(&mpv_ctx, render_params);
	mpv_render_context_set_update_callback(mpv_ctx, on_mpv_redraw, nullptr);

	bool mpv_redraw = false;

	Chat chat;
	std::queue<json> input_queue;
	std::mutex input_lock;

	auto input_thread = std::thread(read_input, std::ref(input_lock), std::ref(input_queue));
	input_thread.detach();

	int64_t t_last = 0, t_now = 0;
	while (1) {
		SDL_Delay(4);
		t_now = SDL_GetPerformanceCounter();
		double delta = (t_now - t_last) / (double)SDL_GetPerformanceFrequency();
		if (delta <= 1 / 165.0)
			continue;
		t_last = t_now;

		bool queue_empty = false;
		while (!queue_empty)
		{
			json j;
			{
				std::lock_guard<std::mutex> guard(input_lock);
				queue_empty = input_queue.empty();
				if (!queue_empty)
				{
					j = input_queue.front();
					input_queue.pop();
				}
			}
			if (!queue_empty)
			{
				handle_instruction(mpvh, chat, j);
			}
		}

		bool redraw = false;
		if (mpv_redraw) {
			redraw = true;
			mpv_redraw = false;
		}
		if (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS)
			redraw = true;

		int mouse_x;
		int mouse_y;
		bool click;
		if (handle_sdl_events(window, chat, mouse_x, mouse_y, click))
			redraw = true;
		mpvh.update();

		//if (!redraw)
		//	continue;

		int w, h;
		SDL_GetWindowSize(window, &w, &h);
		glClear(GL_COLOR_BUFFER_BIT);

		mpv_opengl_fbo mpfbo{
			static_cast<int>(MPV_RENDER_PARAM_OPENGL_FBO),
			w, h, 0
		};
		int flip_y = 1;
		mpv_render_param params[] = {
			{ MPV_RENDER_PARAM_OPENGL_FBO, &mpfbo },
			{ MPV_RENDER_PARAM_FLIP_Y, &flip_y },
			{ MPV_RENDER_PARAM_INVALID, nullptr }
		};
		mpv_render_context_render(mpv_ctx, params);

		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplSDL2_NewFrame(window);
		ImGui::NewFrame();
		//inputwin();
		chatbox(chat);

		Layout l = calculate_layout(font_size, w, h, text_font, icon_font);

		ui(window, mpvh, l, mouse_x, mouse_y, click);
		//dbgwin(window, mpvh);
		glViewport(0, 0, w, h);
		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		SDL_GL_SwapWindow(window);
	}

	return 0;
}
