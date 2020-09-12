#include "imgui/imgui.h"
#include "ui.h"

ImVec2 operator+(ImVec2 a, ImVec2 b)
{
	return ImVec2(a.x + b.x, a.y + b.y);
}

ImVec2 operator-(ImVec2 a, ImVec2 b)
{
	return ImVec2(a.x - b.x, a.y - b.y);
}

ImVec2 operator*(int a, ImVec2 b)
{
	return ImVec2(a * b.x, a * b.y);
}

ImVec2 calc_text_size(ImFont *font, ImVec2 padding, const char *text)
{
	ImGui::PushFont(font);
	auto size = ImGui::CalcTextSize(text);
	ImGui::PopFont();
	return size + 2 * padding;
}

ImRect calc_text_l(float &lcurs, float y, ImVec2 padding, ImFont *font, const char *label)
{
	ImRect r;
	r.size = calc_text_size(font, padding, label);
	r.pos = ImVec2(lcurs, y);
	lcurs = lcurs + r.size.x;
	return r;
}

ImRect calc_text_r(float &rcurs, float y, ImVec2 padding, ImFont *font, const char *label)
{
	ImRect r;
	r.size = calc_text_size(font, padding, label);
	r.pos = ImVec2(rcurs - r.size.x, y);
	rcurs = r.pos.x;
	return r;
}

Layout calculate_layout(
	int text_height, int win_w, int win_h, ImFont *text_font, ImFont *icon_font)
{
	Layout l = {};

	l.master_win.pos = ImVec2(-1, 0);
	l.master_win.size = ImVec2(win_w + 2, win_h + 1);
	l.major_padding = ImVec2(text_height / 4, text_height / 15);
	l.minor_padding = ImVec2(0, l.major_padding.y);
	l.infobar.size = ImVec2(l.master_win.size.x, text_height + 2 * l.major_padding.y);
	l.infobar.pos = ImVec2(l.master_win.pos.x, l.master_win.size.y - l.infobar.size.y);
	float separator = text_height / 2;

	float lcurs = l.infobar.pos.x, rcurs = l.master_win.pos.x + l.master_win.size.x;
	float y = l.infobar.pos.y;

	l.pp_but = calc_text_l(lcurs, y, l.major_padding, icon_font, PLAY_ICON);
	lcurs += separator;
	l.time = calc_text_l(lcurs, y, l.major_padding, text_font, "00:00:00");
	lcurs += separator;
	l.prev_but = calc_text_l(lcurs, y, l.major_padding, icon_font, PLAYLIST_PREVIOUS_ICON);
	l.pl_status = calc_text_l(lcurs, y, l.major_padding, text_font, "1/1");
	l.next_but = calc_text_l(lcurs, y, l.major_padding, icon_font, PLAYLIST_NEXT_ICON);
	lcurs += separator;

	l.fullscr_but = calc_text_r(rcurs, y, l.major_padding, icon_font, FULLSCREEN_ICON);
	rcurs -= separator;
	l.mute_but = calc_text_r(rcurs, y, l.major_padding, icon_font, MUTED_ICON);
	rcurs -= separator;
	l.sub_next_but = calc_text_r(rcurs, y, l.minor_padding, icon_font, RIGHT_ICON);
	rcurs -= l.major_padding.x;
	l.sub_status = calc_text_r(rcurs, y, l.minor_padding, text_font, " 0/0");
	l.sub_icon = calc_text_r(rcurs, y, l.minor_padding, icon_font, SUBTITLE_ICON);
	rcurs -= l.major_padding.x;
	l.sub_prev_but = calc_text_r(rcurs, y, l.minor_padding, icon_font, LEFT_ICON);
	rcurs -= separator;
	l.audio_next_but = calc_text_r(rcurs, y, l.minor_padding, icon_font, RIGHT_ICON);
	rcurs -= l.major_padding.x;
	l.audio_status = calc_text_r(rcurs, y, l.minor_padding, text_font, " 1/1");
	l.audio_icon = calc_text_r(rcurs, y, l.minor_padding, icon_font, AUDIO_ICON);
	rcurs -= l.major_padding.x;
	l.audio_prev_but = calc_text_r(rcurs, y, l.minor_padding, icon_font, LEFT_ICON);
	rcurs -= separator;

	l.title.pos = ImVec2(lcurs, y);
	l.title.size = ImVec2(rcurs - lcurs, l.infobar.size.y);

	return l;
}
