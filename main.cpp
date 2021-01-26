#include <time.h>
#include <errno.h>
#include <GL/glew.h>
#include <SDL2/SDL.h>
#include <mpv/render.h>
#include <mpv/render_gl.h>
#include <vector>
#include <string>
#include <iostream>
#include <sstream>
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

void *get_proc_address_mpv(void *fn_ctx, const char *name)
{
	return SDL_GL_GetProcAddress(name);
}

void on_mpv_redraw(void *mpv_redraw)
{
	//*(bool *)mpv_redraw = true;
}

void toggle_fullscreen(SDL_Window *win, UI_State &ui)
{
	int mx, my;
	SDL_GetGlobalMouseState(&mx, &my);
	SDL_SetWindowFullscreen(win, SDL_GetWindowFlags(win) ^ SDL_WINDOW_FULLSCREEN_DESKTOP);
	SDL_WarpMouseGlobal(mx, my);
	ui.fullscreen = SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP;
}

void move_window(SDL_Window *win, int dx, int dy)
{
	int x, y;
	SDL_GetWindowPosition(win, &x, &y);
	SDL_SetWindowPosition(win, x + dx, y + dy);
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

bool button(ImRect rect, ImVec2 padding)
{
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, padding);
	ImGui::SetCursorPos(rect.pos);
	bool clicked = ImGui::Button("", rect.size);
	ImGui::PopStyleVar(1);
	return clicked;
}

void text(ImRect rect, ImVec2 padding, ImFont *font, const char *text)
{
	ImGui::PushFont(font);
	ImGui::GetWindowDrawList()->AddText(rect.pos + padding, decode_color("#ffffff"), text);
	ImGui::PopFont();
}

void rect(ImRect rect, uint32_t color)
{
	ImGui::GetWindowDrawList()->AddRectFilled(rect.pos, rect.pos + rect.size, color);
}

void chatbox(Chat &c, UI_State &ui, Layout &l)
{
	auto draw_list = ImGui::GetWindowDrawList();

	ImVec2 message_pos = l.chat_log.pos;
	message_pos.y = l.chat_log.pos.y + l.chat_log.size.y;
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

		auto text_size = ImGui::CalcTextSize(msg.text.c_str(), nullptr, false, l.chat_log.size.x);
		message_pos.y -= text_size.y;
		if (message_pos.y < l.chat_log.pos.y)
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
		draw_list->AddText(nullptr, 0.0f, message_pos, fg, msg.text.c_str(), msg.text.c_str() + msg.text.size(), l.chat_log.size.x, nullptr);
	}

	static ImVec4 input_bg = ImVec4(0, 0, 0, 0);

	static std::array<char, 1024> buf;
	ImGui::SetNextItemWidth(l.chat_input.size.x);
	ImGui::SetCursorPos(l.chat_input.pos);
	ImGui::PushStyleColor(ImGuiCol_FrameBg, input_bg);
	if (ImGui::InputText("", buf.data(), 1024, ImGuiInputTextFlags_EnterReturnsTrue))
	{
		json j;
		j["type"] = "user_input";
		j["text"] = buf.data();
		std::cout << j << std::endl;
		std::fill(buf.begin(), buf.end(), 0);
		ui.focus_chat = false;
		input_bg = ImVec4(0, 0, 0, 0);
	}
	ImGui::PopStyleColor();
	if (ui.focus_chat)
	{
		ImGui::SetKeyboardFocusHere(-1);
		ui.focus_chat = false;
		input_bg = ImVec4(0.5, 0.5, 0.5, 0.5);
	}
}

void create_ui(SDL_Window *sdl_win, UI_State &ui, Frame_Input &in, Player &p, Layout &l, Chat &c)
{
	auto info = p.get_info();

	auto intersects_rect = [](ImRect &r, ImVec2 &v) {
		return r.pos.x <= v.x && v.x <= r.pos.x + r.size.x &&
			r.pos.y <= v.y && v.y <= r.pos.y + r.size.y;
	};

	if (ui.last_mouse_pos != in.mouse_state.pos)
		ui.last_activity = std::chrono::steady_clock::now();

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

	bool display_ui = intersects_rect(l.ui_bg, in.mouse_state.pos)
		|| std::chrono::steady_clock::now() - ui.last_activity < std::chrono::seconds(2);

	if (ui.fullscreen && !display_ui)
		ImGui::SetMouseCursor(ImGuiMouseCursor_None);

	if (display_ui)
	{
		rect(l.ui_bg, 0xbb000000);

		if (button(l.prev_but, l.minor_padding, icon_font, PLAYLIST_PREVIOUS_ICON)) {
			p.set_pl_pos(info.pl_pos - 1);
			info = p.get_info();
			send_control(info.pl_pos, info.c_time, info.c_paused);
		}

		auto pl_status = std::stringstream{} << (info.pl_pos + 1) << "/" << info.pl_count;
		text(l.pl_status, l.major_padding, text_font, pl_status.str().c_str());

		if (button(l.next_but, l.minor_padding, icon_font, PLAYLIST_NEXT_ICON)) {
			p.set_pl_pos(info.pl_pos + 1);
			info = p.get_info();
			send_control(info.pl_pos, info.c_time, info.c_paused);
		}

		auto pp_but_str = info.c_paused ? PLAY_ICON : PAUSE_ICON;
		if (button(l.pp_but, l.major_padding, icon_font, pp_but_str)) {
			p.pause(!info.c_paused);
			info = p.get_info();
			send_control(info.pl_pos, info.c_time, info.c_paused);
		}

		auto time_str = std::stringstream{} << sec_to_timestr(info.c_time);
		if (!info.exploring)
		{
			uint32_t delay = std::round(abs(info.delay));
			const char *unit = "s";
			if (delay >= 60) {
				unit = "m";
				delay /= 60;
			}
			if (delay >= 60) {
				unit = "h";
				delay /= 60;
				delay = std::max(delay, 99u);
			}
			if (ui.delay_indicator_sign && info.delay < -0.1)
				ui.delay_indicator_sign = false;
			else if (ui.delay_indicator_sign && info.delay > 0.1)
				ui.delay_indicator_sign = true;
			time_str << (ui.delay_indicator_sign ? "+" : "-") << delay << unit;
		}
		text(l.time, l.major_padding, text_font, time_str.str().c_str());

		if (button(l.sync_but, l.major_padding, text_font, "S"))
		{
			p.force_sync();
		}

		if (button(l.canonize_but, l.major_padding, text_font, "C"))
		{
			p.set_time(info.c_time - info.delay);		
		}

		if (button(l.audio_but, l.major_padding))
			p.set_audio(info.audio_pos + 1);
		text(l.audio_icon, l.minor_padding, icon_font, AUDIO_ICON);
		auto audio_status = std::stringstream{} << " " << info.audio_pos << "/" << info.audio_count;
		text(l.audio_status, l.minor_padding, text_font, audio_status.str().c_str());

		if (button(l.sub_but, l.major_padding))
			p.set_audio(info.sub_pos + 1);
		text(l.sub_icon, l.minor_padding, icon_font, SUBTITLE_ICON);
		auto sub_status = std::stringstream{} << " " << info.sub_pos << "/" << info.sub_count;
		text(l.sub_status, l.minor_padding, text_font, sub_status.str().c_str());

		auto mute_str = info.muted ? MUTED_ICON : UNMUTED_ICON;
		if (button(l.mute_but, l.major_padding, icon_font, mute_str))
			p.toggle_mute();

		auto fullscr_str = ui.fullscreen ? UNFULLSCREEN_ICON : FULLSCREEN_ICON;
		if (button(l.fullscr_but, l.major_padding, icon_font, fullscr_str))
			toggle_fullscreen(sdl_win, ui);

		rect(l.seek_bar, decode_color("#88888888"));
		float seek_fill_bar_w = l.seek_bar.size.x / 4;
		if (info.exploring) {
			float time_delta = info.e_time - info.c_time;
			seek_fill_bar_w += (time_delta / 40.0 / 60.0) * l.seek_bar.size.x;
		}
		rect(
			{l.seek_bar.pos, {seek_fill_bar_w, l.seek_bar.size.y}},
			info.exploring ? decode_color("#ffaa00") : decode_color("#ffaa0088")
		);

		auto mouse_rel_seek = in.mouse_state.pos - l.seek_bar.pos;
		if (0 <= mouse_rel_seek.x && mouse_rel_seek.x <= l.seek_bar.size.x &&
				0 <= mouse_rel_seek.y && mouse_rel_seek.y <= l.seek_bar.size.y)
		{
			int zero_point = l.seek_bar.size.x / 4;
			int point = mouse_rel_seek.x - zero_point;

			float time = point / l.seek_bar.size.x * 40 * 60;
			auto indicator_text = std::stringstream{};
			if (time < 0) {
				indicator_text << "-" << sec_to_timestr(-std::round(time));
			} else {
				indicator_text << "+" << sec_to_timestr(std::round(time));
			}
			ImVec2 indicator_size = calc_text_size(text_font, ImVec2(0, 0), indicator_text.str().c_str());

			auto indicator_pos = ImVec2(in.mouse_state.pos.x, l.seek_bar.pos.y);

			int right_space = l.seek_bar.size.x - mouse_rel_seek.x;
			if (point > 0)
				indicator_pos.x = in.mouse_state.pos.x - indicator_size.x;			

			text({indicator_pos, indicator_size}, l.minor_padding, text_font, indicator_text.str().c_str());

			if (in.left_click) {
				if (!info.exploring)
					p.explore();
				p.set_explore_time(info.c_time + time);
			}
		}

		if (info.exploring)
		{
			text(l.explore_status, l.major_padding, text_font, sec_to_timestr(info.e_time).c_str());

			if (button(l.cancel_but, l.major_padding, text_font, "Cancel"))
				p.explore_cancel();

			if (button(l.accept_but, l.major_padding, text_font, "Accept"))
				p.explore_accept();
		}
	}

	if (ui.fullscreen) {
		if (in.ret)
			ui.focus_chat = true;
		chatbox(c, ui, l);
	}

	bool mouse_on_nothing = !(intersects_rect(l.ui_bg, in.mouse_state.pos) || intersects_rect(l.chat_input, in.mouse_state.pos));

	bool left_clicked = ui.left_down_on_nothing.has_value() && !in.left_click && ui.left_down_on_nothing->pos == in.mouse_state.pos
		&& ui.left_down_on_nothing->global_pos == in.mouse_state.global_pos;

	if (left_clicked) {
		auto fullscreen_click = std::chrono::steady_clock::now();
		auto doubleclick_timeout = std::chrono::duration<double>(0.5);
		if (ui.initial_fullscreen_click.has_value() && fullscreen_click - ui.initial_fullscreen_click.value() < doubleclick_timeout) {
			toggle_fullscreen(sdl_win, ui);
			ui.initial_fullscreen_click.reset();
		} else {
			ui.initial_fullscreen_click = fullscreen_click;
		}
	}

	if (ui.left_down_on_nothing.has_value() && ui.left_down_on_nothing->global_pos != in.mouse_state.global_pos) {
		ImVec2 delta = in.mouse_state.global_pos - ui.left_down_on_nothing->global_pos;
		move_window(sdl_win, delta.x, delta.y);
	}

	if (mouse_on_nothing && in.left_click && !ui.left_down_on_something)
		ui.left_down_on_nothing = in.mouse_state;
	else
		ui.left_down_on_nothing.reset();

	ui.left_down_on_something = in.left_click && (!mouse_on_nothing || ui.left_down_on_something);

	if (in.scroll_down)
		c.scroll_down();
	if (in.scroll_up)
		c.scroll_up();

	ImGui::End();
	ImGui::PopStyleVar(3);

	ui.last_mouse_pos = in.mouse_state.pos;
}

Frame_Input get_sdl_input(SDL_Window *win)
{
	Frame_Input in;

	int mouse_x, mouse_y;
	uint32_t button_state = SDL_GetMouseState(&mouse_x, &mouse_y);
	in.mouse_state.pos = ImVec2(mouse_x, mouse_y);
	in.left_click = button_state & SDL_BUTTON(SDL_BUTTON_LEFT);
	SDL_GetGlobalMouseState(&mouse_x, &mouse_y);
	in.mouse_state.global_pos = ImVec2(mouse_x, mouse_y);

	SDL_Event e;
	while (SDL_PollEvent(&e)) {
		switch (e.type) {
		case SDL_QUIT:
			exit(EXIT_SUCCESS);
			break;
		case SDL_KEYDOWN:
			switch (e.key.keysym.sym) {
			case SDLK_F11:
				in.fullscreen = true;
				break;
			case SDLK_RETURN:
				in.ret = true;
				ImGui_ImplSDL2_ProcessEvent(&e);
				break;
			default:
				ImGui_ImplSDL2_ProcessEvent(&e);
			}
			break;
		case SDL_MOUSEWHEEL:
			if (e.wheel.y < 0)
				in.scroll_down = true;
			else if (e.wheel.y > 0)
				in.scroll_up = true;
		case SDL_WINDOWEVENT:
			if (e.window.event == SDL_WINDOWEVENT_EXPOSED)
				in.redraw = true;
			break;
		default:
			ImGui_ImplSDL2_ProcessEvent(&e);
		}
	}

	return in;
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

	UI_State ui;
	ui.last_activity = std::chrono::steady_clock::now();

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

		Frame_Input input = get_sdl_input(window);
		mpvh.update();

		auto info = mpvh.get_info();
		std::string window_title = info.title == "" ? "Moov" : info.title + " - Moov";
		SDL_SetWindowTitle(window, window_title.c_str());

		if (input.fullscreen)
			toggle_fullscreen(window, ui);

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

		Layout l = calculate_layout(font_size, w, h, text_font, icon_font);

		create_ui(window, ui, input, mpvh, l, chat);
		glViewport(0, 0, w, h);
		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		SDL_GL_SwapWindow(window);
	}

	return 0;
}
