#pragma once

#include "moov.h"

#define PLAY_ICON ((const char *)u8"\ue037")
#define PAUSE_ICON ((const char *)u8"\ue034")
#define PLAYLIST_PREVIOUS_ICON ((const char *)u8"\ue045")
#define PLAYLIST_NEXT_ICON ((const char *)u8"\ue044")
#define LEFT_ICON ((const char *)u8"\ue408")
#define RIGHT_ICON ((const char *)u8"\ue409")
#define AUDIO_ICON ((const char *)u8"\ue0ca")
#define SUBTITLE_ICON ((const char *)u8"\ue048")
#define MUTED_ICON ((const char *)u8"\ue04e")
#define UNMUTED_ICON ((const char *)u8"\ue050")
#define FULLSCREEN_ICON ((const char *)u8"\ue5d0")
#define UNFULLSCREEN_ICON ((const char *)u8"\ue5d1")

struct Layout {
	ImVec2 major_padding;
	ImVec2 minor_padding;

	ImRect master_win;

	ImRect ui_bg;

	ImRect infobar;

	ImRect pp_but;
	ImRect time;
	ImRect prev_but;
	ImRect pl_status;
	ImRect next_but;
	ImRect sub_but;
	ImRect sub_icon;
	ImRect sub_status;
	ImRect audio_but;
	ImRect audio_icon;
	ImRect audio_status;
	ImRect mute_but;
	ImRect fullscr_but;

	ImRect explore_bar;

	ImRect explore_status;
	ImRect seek_bar;
	ImRect cancel_but;
	ImRect accept_but;

	ImRect chat_area;
	ImRect chat_log;
	ImRect chat_input;
};

Layout calculate_layout(int text_height, int win_w, int win_h,
	ImFont *text_font, ImFont *icon_font);
ImVec2 operator+(ImVec2 a, ImVec2 b);
ImVec2 operator-(ImVec2 a, ImVec2 b);
ImVec2 operator*(int a, ImVec2 b);
bool operator==(const ImVec2 &a, const ImVec2 &b);
ImVec2 calc_text_size(ImFont *font, ImVec2 padding, const char *string);
