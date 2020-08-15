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

void mpvhandler::init(int filec, char **filev, int track, double time)
{
	mpv = mpv_create();
	mpv_initialize(mpv);
	mpv_set_option_string(mpv, "vo", "opengl-cb");
	mpv_set_option_string(mpv, "ytdl", "yes");

	for (int i = 0; i < filec; i++) {
		const char *cmd[] = { "loadfile", filev[i], "append", NULL };
		mpv_command(mpv, cmd);
	}

	last_time = mpv_get_time_us(mpv);
	c_pos = track;
	c_time = time;
	c_paused = true;
	exploring = false;

	syncmpv();
	sendstatus();
}

mpv_opengl_cb_context *mpvhandler::get_opengl_cb_api()
{
	return (mpv_opengl_cb_context *)mpv_get_sub_api(mpv, MPV_SUB_API_OPENGL_CB);
}

void mpvhandler::syncmpv()
{
	int64_t mpv_pos;
	mpv_get_property(mpv, "playlist-pos", MPV_FORMAT_INT64, &mpv_pos);
	if (mpv_pos != c_pos) {
		mpv_set_property(mpv, "playlist-pos", MPV_FORMAT_INT64, &c_pos);
		exploring = false;
	}

	if (exploring)
		return;

	bool paused;
	mpv_get_property(mpv, "pause", MPV_FORMAT_FLAG, &paused);
	if (paused != c_paused)
		mpv_set_property(mpv, "pause", MPV_FORMAT_FLAG, &c_paused);

	double mpv_time;
	mpv_get_property(mpv, "time-pos", MPV_FORMAT_DOUBLE, &mpv_time);
	if (abs(mpv_time - c_time) > 5)
		mpv_set_property(mpv, "time-pos", MPV_FORMAT_DOUBLE, &c_time);
}

PlayerInfo mpvhandler::get_info()
{
	PlayerInfo i = { 0 };

	mpv_get_property(mpv, "playlist-pos", MPV_FORMAT_INT64, &i.pl_pos);
	mpv_get_property(mpv, "playlist-count", MPV_FORMAT_INT64, &i.pl_count);
	mpv_get_property(mpv, "muted", MPV_FORMAT_INT64, &i.muted);

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

void mpvhandler::update()
{
	int64_t current_time = mpv_get_time_us(mpv);
	double dt = (double)(current_time - last_time) / 1000000;
	last_time = current_time;
	if (!c_paused)
		c_time += dt;

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
			strncpy(title.str, mpv_title, TITLE_STRING_LEN - 1);
			title.str[TITLE_STRING_LEN - 1] = '\0';
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

statusstr statestr(double time, int paused, int64_t pl_pos, int64_t pl_count)
{
	statusstr s;
	timestr ts = sec_to_timestr(round(time));
	snprintf(s.str, 50, "%ld/%ld %s %s", pl_pos + 1, pl_count,
		paused ? "paused" : "playing", ts.str);
	return s;
}

void mpvhandler::sendstatus()
{
	PlayerInfo i = get_info();
	sendmsg("%s", statestr(i.c_time, i.c_paused, i.pl_pos, i.pl_count).str);
}

void mpvhandler::explore()
{
	exploring = true;
}

void mpvhandler::explore_accept()
{
	exploring = false;
	mpv_get_property(mpv, "time-pos", MPV_FORMAT_DOUBLE, &c_time);
	mpv_get_property(mpv, "pause", MPV_FORMAT_FLAG, &c_paused);
	timestr ts = sec_to_timestr(c_time);
	sendmsg("SET %ld %s %d", c_pos + 1, ts.str, c_paused);
}

void mpvhandler::explore_cancel()
{
	exploring = false;
	syncmpv();
}

void mpvhandler::toggle_mute()
{
	int muted;
	mpv_get_property(mpv, "ao-mute", MPV_FORMAT_FLAG, &muted);
	muted = !muted;
	mpv_set_property(mpv, "ao-mute", MPV_FORMAT_FLAG, &muted);
}

void mpvhandler::set_audio(int64_t track)
{
	mpv_set_property(mpv, "audio", MPV_FORMAT_INT64, &track);
}

void mpvhandler::set_sub(int64_t track)
{
	mpv_set_property(mpv, "sub", MPV_FORMAT_INT64, &track);
}

void mpvhandler::pause(int paused)
{
	c_paused = paused;
	syncmpv();
}

void mpvhandler::set_time(double time)
{
	c_time = time;
	syncmpv();
}

void mpvhandler::set_pl_pos(int64_t pl_pos)
{
	c_pos = pl_pos;
	c_paused = true, c_time = 0;
	syncmpv();
}

void mpvhandler::toggle_explore_paused()
{
	assert(exploring);
	int paused;
	mpv_get_property(mpv, "pause", MPV_FORMAT_FLAG, &paused);
	paused = !paused;
	mpv_set_property(mpv, "pause", MPV_FORMAT_FLAG, &paused);
}

void mpvhandler::set_explore_time(double time)
{
	assert(exploring);
	mpv_set_property(mpv, "time-pos", MPV_FORMAT_DOUBLE, &time);
}
