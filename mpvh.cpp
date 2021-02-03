#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#include "moov.h"

void mpv_get_track_counts(mpv_handle *m, int64_t *audio, int64_t *sub)
{
	*audio = *sub = 0;
	int64_t count;
	mpv_get_property(m, "track-list/count", MPV_FORMAT_INT64, &count);
	for (int i = 0; i < count; i++) {
		char buf[100];
		snprintf(buf, 99, "track-list/%d/type", i);
		char *type;
		mpv_get_property(m, buf, MPV_FORMAT_STRING, &type);
		if (strcmp(type, "audio") == 0)
			(*audio)++;
		if (strcmp(type, "sub") == 0)
			(*sub)++;
		mpv_free(type);
	}
}

Player::Player()
{
	mpv = mpv_create();
	mpv_initialize(mpv);
	mpv_set_option_string(mpv, "ytdl", "yes");
#ifdef __linux__ 
	mpv_set_option_string(mpv, "input-ipc-server", "/tmp/mpvsocket");
#endif

	last_time = mpv_get_time_us(mpv);
	c_pos = 0;
	c_time = 0;
	c_paused = true;
	exploring = false;
	speed = 1.0;

	//syncmpv();
}

void Player::set_ytdl_format(const char *format)
{
		mpv_set_option_string(mpv, "ytdl-raw-options", (std::string("format=") + format).c_str());
}

void Player::add_file(const char *file)
{
	const char *cmd[] = { "loadfile", file, "append", NULL };
	mpv_command(mpv, cmd);
	syncmpv();
}

void Player::create_render_context(mpv_render_context **ctx, mpv_render_param render_params[])
{
	mpv_render_context_create(ctx, mpv, render_params);
}

void Player::syncmpv(bool force)
{
	int64_t mpv_pos;
	mpv_get_property(mpv, "playlist-pos", MPV_FORMAT_INT64, &mpv_pos);
	if (mpv_pos != c_pos) {
		mpv_set_property(mpv, "playlist-pos", MPV_FORMAT_INT64, &c_pos);
		exploring = false;
	}

	if (exploring)
		return;

	int paused;
	mpv_get_property(mpv, "pause", MPV_FORMAT_FLAG, &paused);
	if (paused != c_paused)
		mpv_set_property(mpv, "pause", MPV_FORMAT_FLAG, &c_paused);

	double mpv_time;
	mpv_get_property(mpv, "time-pos", MPV_FORMAT_DOUBLE, &mpv_time);
	if (force || abs(mpv_time - c_time) > 5)
		mpv_set_property(mpv, "time-pos", MPV_FORMAT_DOUBLE, &c_time);
}

PlayerInfo Player::get_info()
{
	PlayerInfo i = { 0 };

	mpv_get_property(mpv, "playlist-pos", MPV_FORMAT_INT64, &i.pl_pos);
	mpv_get_property(mpv, "playlist-count", MPV_FORMAT_INT64, &i.pl_count);
	mpv_get_property(mpv, "ao-mute", MPV_FORMAT_FLAG, &i.muted);

	i.title = title;

	mpv_get_property(mpv, "duration", MPV_FORMAT_DOUBLE, &i.duration);

	i.audio_count = audio_count;
	i.sub_count = sub_count;

	mpv_get_property(mpv, "audio", MPV_FORMAT_INT64, &i.audio_pos);
	mpv_get_property(mpv, "sub", MPV_FORMAT_INT64, &i.sub_pos);

	i.exploring = exploring;
	i.c_time = c_time;
	i.c_paused = c_paused;
	double mpv_time;
	mpv_get_property(mpv, "time-pos", MPV_FORMAT_DOUBLE, &mpv_time);
	if (!exploring) {
		i.delay = i.c_time - mpv_time;
	} else {
		i.e_time = mpv_time;
		mpv_get_property(mpv, "pause", MPV_FORMAT_FLAG, &i.e_paused);
	}

	return i;
}

void Player::update()
{
	int64_t current_time = mpv_get_time_us(mpv);
	double dt = (double)(current_time - last_time) / 1000000;
	last_time = current_time;
	if (!c_paused)
		c_time += dt;

	auto info = get_info();

	auto clamp = [](double lo, double x, double hi) { return std::min(std::max(lo, x), hi); };

	if ((speed != 1.0 && info.delay < -0.3) || info.delay < -0.5)
		speed = 1.0 - 0.3*clamp(0, -info.delay/10, 1);
	else if ((speed != 1.0 && info.delay >= 0.3) || info.delay >= 0.5)
		speed = 1.0 + 0.3*clamp(0, info.delay/10, 1);
	else
		speed = 1.0;
	mpv_set_property(mpv, "speed", MPV_FORMAT_DOUBLE, &speed);

	mpv_event *e;
	while (e = mpv_wait_event(mpv, 0), e->event_id != MPV_EVENT_NONE) {
		switch (e->event_id) {
		case MPV_EVENT_SHUTDOWN:
			break;
		case MPV_EVENT_LOG_MESSAGE:
			break;
		case MPV_EVENT_GET_PROPERTY_REPLY:
			break;
		case MPV_EVENT_SET_PROPERTY_REPLY:
			break;
		case MPV_EVENT_COMMAND_REPLY:
			break;
		case MPV_EVENT_START_FILE:
			break;
		case MPV_EVENT_END_FILE:
			break;
		case MPV_EVENT_FILE_LOADED: {
			char *mpv_title;
			mpv_get_property(mpv, "media-title", MPV_FORMAT_STRING, &mpv_title);
			title = std::string(mpv_title);
			mpv_free(mpv_title);

			mpv_get_track_counts(mpv, &audio_count, &sub_count);

			syncmpv();
			break;
		}
		case MPV_EVENT_IDLE:
			break;
		case MPV_EVENT_TICK:
			break;
		case MPV_EVENT_CLIENT_MESSAGE:
			break;
		case MPV_EVENT_VIDEO_RECONFIG:
			break;
		case MPV_EVENT_SEEK:
			break;
		case MPV_EVENT_PLAYBACK_RESTART:
			syncmpv();
			break;
		case MPV_EVENT_PROPERTY_CHANGE:
			break;
		case MPV_EVENT_QUEUE_OVERFLOW:
			break;
		default:
			break;
		}
	}
}

std::string statestr(double time, int paused, int64_t pl_pos, int64_t pl_count)
{
	char buf[50];
	std::string ts = sec_to_timestr(round(time));
	snprintf(buf, 50, "%ld/%ld %s %s", pl_pos + 1, pl_count,
		paused ? "paused" : "playing", ts.c_str());
	return std::string(buf);
}

void Player::explore()
{
	exploring = true;
}

void Player::explore_accept()
{
	exploring = false;
	mpv_get_property(mpv, "time-pos", MPV_FORMAT_DOUBLE, &c_time);
	mpv_get_property(mpv, "pause", MPV_FORMAT_FLAG, &c_paused);
	send_control(c_pos, c_time, c_paused);
}

void Player::explore_cancel()
{
	exploring = false;
	syncmpv();
}

void Player::toggle_mute()
{
	int muted;
	mpv_get_property(mpv, "ao-mute", MPV_FORMAT_FLAG, &muted);
	muted = !muted;
	mpv_set_property(mpv, "ao-mute", MPV_FORMAT_FLAG, &muted);
}

void Player::set_audio(int64_t track)
{
	mpv_set_property(mpv, "audio", MPV_FORMAT_INT64, &track);
}

void Player::set_sub(int64_t track)
{
	mpv_set_property(mpv, "sub", MPV_FORMAT_INT64, &track);
}

void Player::pause(int paused)
{
	c_paused = paused;
	syncmpv();
}

void Player::set_time(double time)
{
	c_time = time;
	syncmpv();
}

void Player::set_pl_pos(int64_t pl_pos)
{
	c_pos = pl_pos;
	c_paused = true, c_time = 0;
	syncmpv();
}

void Player::set_canonical(int64_t pl_pos, bool paused, double time)
{
	c_pos = pl_pos;
	c_paused = paused;
	c_time = time;
	syncmpv();
}

void Player::toggle_explore_paused()
{
	assert(exploring);
	int paused;
	mpv_get_property(mpv, "pause", MPV_FORMAT_FLAG, &paused);
	paused = !paused;
	mpv_set_property(mpv, "pause", MPV_FORMAT_FLAG, &paused);
}

void Player::set_explore_time(double time)
{
	assert(exploring);
	mpv_set_property(mpv, "time-pos", MPV_FORMAT_DOUBLE, &time);
}

void Player::force_sync()
{
	syncmpv(true);
}