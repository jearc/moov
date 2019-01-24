#include <mpv/client.h>
#include <mpv/opengl_cb.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "mpvhandler.h"
#include "chat.h"

enum { playing, paused };
struct state {
	int pstatus;
	double time;
};

struct mpvhandler {
	mpv_handle *mpv;
	char *file;
	state s_canon;
	int64_t last_time;
};

mpvhandler *mpvh_create(char *uri)
{
	mpvhandler *h = (mpvhandler *)malloc(sizeof *h);

	h->mpv = mpv_create();
	mpv_initialize(h->mpv);
	mpv_set_option_string(h->mpv, "vo", "opengl-cb");
	mpv_set_option_string(h->mpv, "ytdl", "yes");
	
	h->file = strdup(uri);
	const char *cmd[] = { "loadfile", h->file, NULL };
	mpv_command(h->mpv, cmd);
	
	h->s_canon.pstatus = paused;
	h->s_canon.time = 152.0;
	
	h->last_time = mpv_get_time_us(h->mpv);

	return h;
}

mpv_opengl_cb_context *mpvh_get_opengl_cb_api(mpvhandler *h)
{
	return (mpv_opengl_cb_context *)mpv_get_sub_api(h->mpv, MPV_SUB_API_OPENGL_CB);
}

void mpvh_syncmpv(mpvhandler *h)
{
	state *s = &h->s_canon;
	mpv_set_property(h->mpv, "pause", MPV_FORMAT_FLAG, &s->pstatus);
	mpv_set_property(h->mpv, "time-pos", MPV_FORMAT_DOUBLE, &s->time);
}

void mpvh_update(mpvhandler *h)
{
	int64_t current_time = mpv_get_time_us(h->mpv);
	double dt = (double)(current_time - h->last_time) / 1000000;
	if (h->s_canon.pstatus == playing)
		h->s_canon.time += dt;
	h->last_time = current_time;
	
	mpv_event *e;
	while (e = mpv_wait_event(h->mpv, 0), e->event_id != MPV_EVENT_NONE) {
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
		case MPV_EVENT_FILE_LOADED:
			mpvh_syncmpv(h);
			break;
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

void mpvh_pp(mpvhandler *h)
{
	h->s_canon.pstatus = h->s_canon.pstatus == playing ? paused : playing;
	mpvh_syncmpv(h);
}

void mpvh_seek(mpvhandler *h, double time)
{
	h->s_canon.time = time;
	mpvh_syncmpv(h);
}

void mpvh_seekrel(mpvhandler *h, double offset)
{
	h->s_canon.time += offset;
	mpvh_syncmpv(h);
}

state mpvh_mpvstate(mpvhandler *h)
{
	state st;
	mpv_get_property(h->mpv, "playback-time", MPV_FORMAT_DOUBLE, &st.time);
	mpv_get_property(h->mpv, "pause", MPV_FORMAT_FLAG, &st.pstatus);
	return st;
}

statusstr statestr(state st)
{
	statusstr s;
	
	int64_t t, hh, mm, ss;
	t = round(st.time);
	hh = t / 3600;
	mm = (t % 3600) / 60;
	ss = t % 60;
	
	snprintf(s.str, 50, "%s %ld:%02ld:%02ld",
	         st.pstatus == paused ? "paused" : "playing", hh, mm, ss);

	return s;
}

statusstr mpvh_statusstr(mpvhandler *h)
{
	return statestr(h->s_canon);
}

statusstr mpvh_mpvstatusstr(mpvhandler *h)
{
	return statestr(mpvh_mpvstate(h));
}