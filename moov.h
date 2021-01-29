#pragma once

#include <string>
#include <vector>
#include <span>
#include <chrono>
#include <optional>
#include <mpv/client.h>
#include <mpv/render.h>
#include <mpv/render_gl.h>
#include "imgui/imgui.h"

using time_point = std::chrono::time_point<std::chrono::steady_clock>;

enum Command {
	OUT_CONTROL = 10,
	IN_PAUSE = 1,
	IN_SEEK = 2,
	IN_MESSAGE = 3,
	OUT_STATUS = 4,
	OUT_USER_INPUT = 5,
	IN_ADD_FILE = 6,
	IN_SET_PLAYLIST_POSITION = 7,
	IN_STATUS_REQUEST = 8,
	IN_CLOSE = 9,
};

struct Message {
	std::string text;
	time_point time;
	unsigned fg, bg;
};

struct Chat {

	void add_message(const Message &m);
	std::span<Message> messages();
	void scroll_up();
	void scroll_down();
	time_point get_last_end_scroll_time();

private:
	std::vector<Message> log;
	size_t cursor = 0;
	time_point last_end_scroll_time;
};

struct PlayerInfo {
	int64_t pl_pos, pl_count;
	int muted;

	std::string title;
	double duration;
	int64_t audio_pos, audio_count;
	int64_t sub_pos, sub_count;

	double c_time, delay;
	int c_paused;

	int exploring;
	double e_time;
	int e_paused;
};

class Player {
public:
	Player();
	void add_file(const char *file);
	void pause(int paused);
	void toggle_explore_paused();
	PlayerInfo get_info();
	void set_canonical(int64_t pl_pos, bool paused, double time);
	void set_time(double time);
	void set_pl_pos(int64_t pl_pos);
	void set_explore_time(double time);
	void toggle_mute();
	void explore_cancel();
	void explore_accept();
	void explore();
	void update();
	void create_render_context(mpv_render_context **ctx, mpv_render_param render_params[]);
	void set_audio(int64_t track);
	void set_sub(int64_t track);
	void force_sync();

private:
	void syncmpv(bool force = false);

	mpv_handle *mpv;
	int64_t last_time;
	int64_t c_pos;
	double c_time;
	int c_paused;
	int exploring;
	double speed;

	int64_t audio_count, sub_count;
	std::string title;
};

struct ImRect {
	ImVec2 pos, size;
};

struct Mouse_State {
	ImVec2 pos, global_pos;
	bool in_window;
};

struct Frame_Input {
	Mouse_State mouse_state;
	bool left_click = false;
	bool redraw = false;
	bool ret = false;
	bool scroll_up = false;
	bool scroll_down = false;
	bool fullscreen = false;
	bool left_up = false;
};

struct UI_State {
	bool fullscreen = false;
	ImRect window_geometry;
	std::optional<time_point> initial_fullscreen_click;
	std::optional<Mouse_State> left_down_on_nothing;
	bool left_down_on_something = false;
	time_point last_activity;
	ImVec2 last_mouse_pos;
	bool delay_indicator_sign = false;
	double seek_bar_scale = 40 * 60;
	std::optional<Mouse_State> initial_left_down;
};

std::string sec_to_timestr(uint32_t seconds);
void die(std::string_view str);
void send_control(int64_t pos, double time, bool paused);
std::string statestr(double time, int paused, int64_t pl_pos, int64_t pl_count);
