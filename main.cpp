#include <time.h>
#include <stddef.h>
#include <stdlib.h>
#include <string>
#include <unistd.h>
#include <vector>
#include <stdio.h>
#include <pthread.h>

#include <GL/gl3w.h>
#include <SDL.h>

#include <mpv/client.h>
#include <mpv/opengl_cb.h>

#include "imgui/imgui.h"
#include "imgui/imgui_impl_sdl_gl3.h"

static Uint32 wakeup_on_mpv_events;

const char *PIPE = "/tmp/mpvpipe";

const uint16_t MARGIN_SIZE = 8;
const float DEFAULT_OPACITY = 1.0;

struct Message {
	time_t time;
	std::string from;
	std::string text;
};

void on_msg(const char *s, bool self);

SDL_Window *window;

mpv_handle *mpv;
SDL_GLContext glcontext;
mpv_opengl_cb_context *mpv_gl;
std::vector<Message> chat_log;
pthread_mutex_t chatmutex = PTHREAD_MUTEX_INITIALIZER;

ImVec4 clear_color = ImColor(114, 144, 154);
char chat_input_buf[256] = {0};
bool scroll_to_bottom = true;
int mouse_x = 0, mouse_y = 0;
int last_mouse_move = 0;
int64_t current_tick = 0;
int64_t last_tick = 0;
bool mouse_over_controls = false;
ImVec2 chatsize = ImVec2(400, 200);
ImVec2 chatpos = ImVec2(-chatsize.x - MARGIN_SIZE, -chatsize.y - MARGIN_SIZE);
ImVec2 osdsize = ImVec2(400, 78);
ImVec2 osdpos = ImVec2(MARGIN_SIZE, chatpos.y - MARGIN_SIZE - osdsize.y);
float chat_opacity = DEFAULT_OPACITY;
float osd_opacity = DEFAULT_OPACITY;
std::string username = "User";
char *home_dir = NULL;

static void die(const char *msg)
{
	fprintf(stderr, "error: %s\n", msg);
	exit(1);
}

static void *get_proc_address_mpv(void *fn_ctx, const char *name)
{
	return SDL_GL_GetProcAddress(name);
}

static void on_mpv_events(void *ctx)
{
	SDL_Event event = {.type = wakeup_on_mpv_events};
	SDL_PushEvent(&event);
}

char *seconds_to_time_string(float t)
{
	unsigned int h = floor(t / 3600);
	unsigned int m = floor((t - 3600 * h) / 60);
	unsigned int s = floor(t - 3600 * h - 60 * m);

	char *ts = (char *)malloc(9);
	snprintf(ts, 9, "%02d:%02d:%02d", h, m, s);

	return ts;
}

int get_num_audio_sub_tracks(mpv_handle *mpv, int *naudio, int *nsubs)
{
	int64_t ntracks;
	if (mpv_get_property(mpv, "track-list/count", MPV_FORMAT_INT64,
			     &ntracks))
		return -1;
	int naudio_ = 0, nsubs_ = 0;
	for (int i = 0; i < ntracks; i++) {
		std::string prop;
		prop += "track-list/";
		prop += std::to_string(i);
		prop += "/type";
		char *type = NULL;
		if (mpv_get_property(mpv, prop.c_str(), MPV_FORMAT_STRING,
				     &type))
			return -2;
		if (std::string(type) == "sub")
			nsubs_++;
		if (std::string(type) == "audio")
			naudio_++;
	}

	*naudio = naudio_;
	*nsubs = nsubs_;
	return 0;
}

void sendmsg(const char *msg)
{
	printf("MSG :%s\n", msg);
}

std::string getstatus()
{
	char *playback_time = mpv_get_property_osd_string(mpv, "playback-time");
	char *duration = mpv_get_property_osd_string(mpv, "duration");
	char *playlist_pos = mpv_get_property_osd_string(mpv, "playlist-pos-1");
	char *playlist_count =
	    mpv_get_property_osd_string(mpv, "playlist-count");
	int paused = 0;
	mpv_get_property(mpv, "pause", MPV_FORMAT_FLAG, &paused);

	std::string status;
	if (playback_time && duration) {
		status += "moov: [";
		status += playlist_pos;
		status += "/";
		status += playlist_count;
		status += "] ";
		status += paused ? "paused" : "playing";
		status += " ";
		status += playback_time;
	} else {
		status = "moov: not playing";
	}

	return status;
}

std::vector<std::string> splitstring(const char *s, char delim)
{
	char *dup = strdup(s);
	char *delimstr = (char *)malloc(2);
	sprintf(delimstr, "%c", delim);
	std::vector<std::string> words;

	char *str = dup, *token, *saveptr;
	while (token = strtok_r(str, delimstr, &saveptr)) {
		words.push_back(token);
		str = NULL;
	}

	free(dup);

	return words;
}

int parse_time(const char *timestr)
{
	int nums[3] = {0}, count = 0;

	char *dup = strdup(timestr);
	char *str = dup, *token, *saveptr;
	while (count < 3 && (token = strtok_r(str, ":", &saveptr))) {
		nums[count++] = atoi(token);
		str = NULL;
	}
	free(dup);

	int seconds = 0;
	int power = 0;
	while (count > 0) {
		seconds += nums[count - 1] * pow(60, power);
		printf("%d^%d\n", nums[count - 1], power);
		count--;
		power++;
	}

	return seconds;
}

void writechat(const char *text, const char *from = NULL)
{
	Message msg;
	time_t t = time(0);
	msg.time = t;
	msg.from = from ? from : username;
	msg.text = text;

	pthread_mutex_lock(&chatmutex);
	chat_log.push_back(msg);
	pthread_mutex_unlock(&chatmutex);
	scroll_to_bottom = true;
}

void msg(const char *text, const char *from = NULL)
{
	if (!text)
		return;

	int len = strlen(text);

	writechat(text, from);

	if (!(from || (len > 2 && text[0] == ':')))
		sendmsg(text);

	on_msg(text, !from);
}

void on_msg(const char *s, bool self)
{
	auto words = splitstring(s, ' ');
	std::string com;
	if (words.size() > 0)
		com = words[0];
	else
		return;

	if (com == "pp") {
		mpv_command_string(mpv, "cycle pause");
		auto status = getstatus();
		msg(status.c_str());
	} else if (com == "STATUS") {
		auto status = getstatus();
		msg(status.c_str());
	} else if (com == "NEXT") {
		mpv_command_string(mpv, "playlist-next");
	} else if (com == "PREV") {
		mpv_command_string(mpv, "playlist-prev");
	} else if (com == "SEEK") {
		if (words.size() < 2)
			return;
		double time = parse_time(words[1].c_str());
		mpv_set_property(mpv, "time-pos", MPV_FORMAT_DOUBLE, &time);
	} else if (com == "INDEX") {
		if (words.size() < 2)
			return;
		int64_t index = atoi(words[1].c_str()) - 1;
		mpv_set_property(mpv, "playlist-pos", MPV_FORMAT_INT64, &index);
	} else if (com == ":set" && self) {
		if (words.size() < 2)
			return;
		if (words[1] == "chat_opacity")
			chat_opacity = words.size() > 2 ? atof(words[2].c_str())
							: DEFAULT_OPACITY;
		if (words[1] == "osd_opacity")
			osd_opacity = words.size() > 2 ? atof(words[2].c_str())
						       : DEFAULT_OPACITY;
	} else if (com == "ALTF4") {
		mpv_opengl_cb_uninit_gl(mpv_gl);
		mpv_terminate_destroy(mpv);
		SDL_GL_DeleteContext(glcontext);
		SDL_DestroyWindow(window);
		exit(0);
	}
}

void toggle_fullscreen()
{
	bool is_fullscreen = SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN;
	SDL_SetWindowFullscreen(
	    window, is_fullscreen ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
}

void osd()
{
	if (!(mouse_over_controls || current_tick - last_mouse_move < 1000))
		return;

	ImVec2 size = osdsize;
	ImVec2 pos;
	pos.x = osdpos.x >= 0 ? osdpos.x
			      : (int)ImGui::GetIO().DisplaySize.x + osdpos.x;
	pos.y = osdpos.y >= 0 ? osdpos.y
			      : (int)ImGui::GetIO().DisplaySize.y + osdpos.y;
	bool display = true;
	ImGui::SetNextWindowPos(pos);
	ImGui::Begin("StatusDisplay", &display, size, osd_opacity,
		     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
			 | ImGuiWindowFlags_NoMove
			 | ImGuiWindowFlags_NoScrollbar
			 | ImGuiWindowFlags_NoSavedSettings);
	auto filename = mpv_get_property_string(mpv, "media-title");
	ImGui::Text("%s", filename);
	char *pos_sec = mpv_get_property_string(mpv, "playback-time");
	char *total_sec = mpv_get_property_string(mpv, "duration");
	float progress = 0;
	if (pos_sec && total_sec)
		progress = std::stof(pos_sec) / std::stof(total_sec);
	ImGui::ProgressBar(progress, ImVec2(380, 20));
	if (ImGui::IsItemClicked() && total_sec) {
		auto min = ImGui::GetItemRectMin();
		auto max = ImGui::GetItemRectMax();
		auto mouse = ImGui::GetMousePos();

		float fraction = (mouse.x - min.x) / (max.x - min.x);
		double time = fraction * std::stof(total_sec);

		mpv_set_property(mpv, "time-pos", MPV_FORMAT_DOUBLE, &time);
	}
	if (ImGui::Button("PP"))
		mpv_command_string(mpv, "cycle pause");
	ImGui::SameLine();
	char *playback_time = mpv_get_property_osd_string(mpv, "playback-time");
	char *duration = mpv_get_property_osd_string(mpv, "duration");
	char *playlist_pos = mpv_get_property_osd_string(mpv, "playlist-pos-1");
	char *playlist_count =
	    mpv_get_property_osd_string(mpv, "playlist-count");
	if (playback_time && duration) {
		std::string status;
		status += playback_time;
		status += " / ";
		status += duration;
		ImGui::Text("%s", status.c_str());
	} else {
		ImGui::Text("Not playing");
	}
	ImGui::SameLine();
	if (ImGui::Button("<"))
		mpv_command_string(mpv, "playlist-prev");
	ImGui::SameLine();
	if (playlist_pos && playlist_count) {
		std::string status;
		status += playlist_pos;
		status += "/";
		status += playlist_count;
		ImGui::Text("%s", status.c_str());
	} else {
		ImGui::Text("0/0");
	}
	ImGui::SameLine();
	if (ImGui::Button(">"))
		mpv_command_string(mpv, "playlist-next");
	ImGui::SameLine();
	int naudio = 0;
	int nsubs = 0;
	get_num_audio_sub_tracks(mpv, &naudio, &nsubs);
	int64_t selsub = 0;
	mpv_get_property(mpv, "sub", MPV_FORMAT_INT64, &selsub);
	std::string sub_button =
	    "Sub: " + std::to_string(selsub) + "/" + std::to_string(nsubs);
	if (ImGui::Button(sub_button.c_str())) {
		mpv_command_string(mpv, "cycle sub");
	}
	ImGui::SameLine();
	int64_t selaudio = 0;
	mpv_get_property(mpv, "audio", MPV_FORMAT_INT64, &selaudio);
	std::string audio_button =
	    "Audio: " + std::to_string(selaudio) + "/" + std::to_string(naudio);
	ImGui::Button(audio_button.c_str());
	ImGui::SameLine();
	if (ImGui::Button("Full")) {
		toggle_fullscreen();
	}
	mouse_over_controls = ImGui::IsWindowHovered();
	static bool mouse_down = false;
	static int last_pos_x = mouse_x, last_pos_y = mouse_y;
	bool mouse_over_window =
	    (mouse_x >= pos.x && mouse_x < pos.x + size.x)
	    && (mouse_y >= pos.y && mouse_y < pos.y + size.y);
	if (ImGui::IsMouseClicked(0) && mouse_over_window) {
		mouse_down = true;
	}
	if (mouse_down) {
		int delta_x = mouse_x - last_pos_x;
		int delta_y = mouse_y - last_pos_y;
		osdpos.x += delta_x;
		osdpos.y += delta_y;
	}
	if (ImGui::IsMouseReleased(0)) {
		mouse_down = false;
	}
	last_pos_x = mouse_x;
	last_pos_y = mouse_y;
	ImGui::End();
}

void chatbox()
{
	ImVec2 size = chatsize;
	ImVec2 pos;
	pos.x = chatpos.x >= 0 ? chatpos.x
			       : (int)ImGui::GetIO().DisplaySize.x + chatpos.x;
	pos.y = chatpos.y >= 0 ? chatpos.y
			       : (int)ImGui::GetIO().DisplaySize.y + chatpos.y;

	bool display = true;
	ImGui::SetNextWindowSize(size, ImGuiSetCond_FirstUseEver);
	ImGui::SetNextWindowPos(pos);
	ImGui::Begin("ChatBox", &display, size, chat_opacity,
		     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
			 | ImGuiWindowFlags_NoMove
			 | ImGuiWindowFlags_NoScrollbar
			 | ImGuiWindowFlags_NoSavedSettings);
	ImGui::BeginChild("LogRegion",
			  ImVec2(0, -ImGui::GetItemsLineHeightWithSpacing()),
			  false, ImGuiWindowFlags_NoScrollbar);
	{
		pthread_mutex_lock(&chatmutex);
		for (auto &msg : chat_log) {
			tm *now = localtime(&msg.time);
			char buf[20] = {0};
			strftime(buf, sizeof(buf), "%X", now);
			std::string formatted;
			formatted += "[";
			formatted += buf;
			formatted += "] ";
			formatted += msg.from;
			formatted += ": ";
			formatted += msg.text;
			ImGui::TextWrapped("%s", formatted.c_str());
		}
		pthread_mutex_unlock(&chatmutex);
	}
	if (scroll_to_bottom) {
		ImGui::SetScrollHere();
		scroll_to_bottom = false;
	}
	ImGui::EndChild();
	ImGui::PushItemWidth(size.x - 8 * 2);
	if (ImGui::InputText("", chat_input_buf, 256,
			     ImGuiInputTextFlags_EnterReturnsTrue, NULL,
			     NULL)) {
		if (strlen(chat_input_buf)) {
			msg(chat_input_buf);
			strcpy(chat_input_buf, "");
		}
	}
	static bool mouse_down = false;
	static int last_pos_x = mouse_x, last_pos_y = mouse_y;
	bool mouse_over_window =
	    (mouse_x >= pos.x && mouse_x < pos.x + size.x)
	    && (mouse_y >= pos.y && mouse_y < pos.y + size.y);
	if (ImGui::IsMouseClicked(0) && mouse_over_window) {
		mouse_down = true;
	}
	if (mouse_down) {
		int delta_x = mouse_x - last_pos_x;
		int delta_y = mouse_y - last_pos_y;
		chatpos.x += delta_x;
		chatpos.y += delta_y;
	}
	if (ImGui::IsMouseReleased(0)) {
		mouse_down = false;
	}
	last_pos_x = mouse_x;
	last_pos_y = mouse_y;
	ImGui::PopItemWidth();
	if (ImGui::IsItemHovered()
	    || (ImGui::IsRootWindowOrAnyChildFocused()
		&& !ImGui::IsAnyItemActive() && !ImGui::IsMouseClicked(0)))
		ImGui::SetKeyboardFocusHere(-1);
	ImGui::End();
}

void ui()
{
	ImGui_ImplSdlGL3_NewFrame(window);

	osd();
	chatbox();

	glViewport(0, 0, (int)ImGui::GetIO().DisplaySize.x,
		   (int)ImGui::GetIO().DisplaySize.y);
	ImGui::Render();
}

int readmsg(const char *l)
{
	char *user = NULL;
	const char *message = NULL;

	if (!l)
		return 1;

	if (strncmp("MSG", l, 3))
		return 2;

	if (strnlen(l, 5) < 5)
		return 3;
	const char *u = l + 4;
	const char *c = strchr(u, ':');
	if (!c)
		return 4;
	user = strndup(u, c - u);

	if (strnlen(c, 2) < 2)
		return 5;
	message = c + 1;

	msg(message, user);

	free(user);

	return 0;
}

void *stdin_loop(void *arg)
{
	char *line = NULL;
	size_t size;
	while (true) {
		getline(&line, &size, stdin);
		int err = readmsg(line);
		if (err)
			fprintf(stderr, "parsing error: %d\n", err);
		free(line);
	}

	return NULL;
}

char *playlistpath()
{
	size_t pathlen = strlen(home_dir) + strlen("/.moov_playlist") + 1;
	char *path = (char *)malloc(pathlen);
	snprintf(path, pathlen, "%s/.moov_playlist", home_dir);

	return path;
}

char *trackpath()
{
	size_t pathlen = strlen(home_dir) + strlen("/.moov_track") + 1;
	char *path = (char *)malloc(pathlen);
	snprintf(path, pathlen, "%s/.moov_track", home_dir);

	return path;
}

void load_playlist(int *last_completed_track)
{
	size_t size, n;
	char *path, *line;
	FILE *fp;

	path = playlistpath();
	fp = fopen(path, "r");
	if (!fp)
		die("could not load playlist file.");
	line = NULL;
	size = 0, n = 0;
	while ((size = getline(&line, &n, fp)) != -1) {
		if (line[size - 1] == '\n')
			line[size - 1] = '\0';
		const char *cmd[] = {"loadfile", line, "append", NULL};
		mpv_command(mpv, cmd);
		free(line);
		line = NULL;
	}
	fclose(fp);
	free(path);

	path = trackpath();
	fp = fopen(path, "r");
	if (!fp)
		die("could not load track file.");
	line = NULL;
	n = 0;
	size = getline(&line, &n, fp);
	errno = 0;
	int64_t track = strtol(line, NULL, 10);
	if (errno)
		die("could not parse track file.");
	*last_completed_track = track;
	track++;
	mpv_set_property(mpv, "playlist-pos", MPV_FORMAT_INT64, &track);
	fclose(fp);
	free(path);
}

void save_track(int n)
{
	char *path = trackpath();
	FILE *fp = fopen(path, "w");
	fprintf(fp, "%d\n", n);
	fclose(fp);
	free(path);
}

void save_playlist(int numfiles, char **files)
{
	char *path = playlistpath();
	FILE *fp = fopen(path, "w");
	for (int i = 0; i < numfiles; i++)
		fprintf(fp, "%s\n", files[i]);
	fclose(fp);
	free(path);
}

int main(int argc, char *argv[])
{
	if (argc < 3)
		die("pass a username and media file(s) or URL(s) as arguments");

	username = argv[1];

	home_dir = getenv("HOME");

	mpv = mpv_create();
	if (!mpv)
		die("context init failed");

	if (mpv_initialize(mpv) < 0)
		die("mpv init failed");

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
	window = SDL_CreateWindow(
	    "Moov", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720,
	    SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
	if (!window)
		die("failed to create SDL window");

	mpv_gl = (mpv_opengl_cb_context *)mpv_get_sub_api(
	    mpv, MPV_SUB_API_OPENGL_CB);
	if (!mpv_gl)
		die("failed to create mpv GL API handle");

	glcontext = SDL_GL_CreateContext(window);
	if (!glcontext)
		die("failed to create SDL GL context");

	gl3wInit();

	ImGui_ImplSdlGL3_Init(window);

	ImGuiIO &io = ImGui::GetIO();
	// io.Fonts->AddFontFromFileTTF("LiberationSans-Regular.ttf", 14.0f);

	if (mpv_opengl_cb_init_gl(mpv_gl, NULL, get_proc_address_mpv, NULL) < 0)
		die("failed to initialize mpv GL context");

	if (mpv_set_option_string(mpv, "vo", "opengl-cb") < 0)
		die("failed to set VO");

	mpv_set_option_string(mpv, "ytdl", "yes");
	mpv_set_option_string(mpv, "ytdl-raw-options",
			      "format=\"bestvideo[height<="
			      "720][ext=mp4]+bestaudio/"
			      "best[height<=720]/best\"");
	mpv_set_option_string(mpv, "input-ipc-server", PIPE);

	wakeup_on_mpv_events = SDL_RegisterEvents(1);
	if (wakeup_on_mpv_events == (Uint32)-1)
		die("could not register events");
	mpv_set_wakeup_callback(mpv, on_mpv_events, NULL);

	bool file_loaded = false;
	bool is_playlist = false;
	int last_completed_track = -1;

	if (argc >= 2 && !strcmp(argv[2], "--resume")) {
		is_playlist = true;
		load_playlist(&last_completed_track);
		file_loaded = true;
	} else {
		const char *cmd[] = {"loadfile", argv[2], NULL};
		mpv_command(mpv, cmd);
		for (int i = 3; i < argc; i++) {
			const char *cmd2[] = {"loadfile", argv[i], "append",
					      NULL};
			mpv_command(mpv, cmd2);
		}
		int64_t playlist_count;
		mpv_get_property(mpv, "playlist-count", MPV_FORMAT_INT64,
				 &playlist_count);
		if (playlist_count > 1) {
			save_playlist(argc - 2, argv + 2);
			save_track(-1);
			is_playlist = true;
		}
	}

	pthread_t stdin_thread;
	pthread_create(&stdin_thread, NULL, stdin_loop, NULL);

	while (1) {
		SDL_Delay(10);
		current_tick = SDL_GetTicks();
		if (current_tick - last_tick <= 16)
			continue;
		last_tick = current_tick;
		int old_mouse_x = mouse_x, old_mouse_y = mouse_y;
		SDL_GetMouseState(&mouse_x, &mouse_y);
		if (old_mouse_x != mouse_x || old_mouse_y != mouse_y)
			last_mouse_move = current_tick;
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			switch (event.type) {
			case SDL_QUIT:
				goto done;
			case SDL_KEYDOWN:
				if (event.key.keysym.sym == SDLK_F11) {
					toggle_fullscreen();
				} else if (event.key.keysym.mod & KMOD_CTRL
					   && event.key.keysym.sym
						  == SDLK_SPACE) {
					mpv_command_string(mpv, "cycle pause");
				} else
					ImGui_ImplSdlGL3_ProcessEvent(&event);
				break;
			default:
				if (event.type == wakeup_on_mpv_events) {
					while (1) {
						mpv_event *mp_event =
						    mpv_wait_event(mpv, 0);
						if (mp_event->event_id
						    == MPV_EVENT_NONE)
							break;
						if (mp_event->event_id
						    == MPV_EVENT_FILE_LOADED) {
							mpv_command_string(
							    mpv,
							    "set pause yes");
							file_loaded = true;
						}
						if (mp_event->event_id
						    == MPV_EVENT_PLAYBACK_RESTART) {
							auto status =
							    getstatus();
							msg(status.c_str());
						}
						if (mp_event->event_id
							== MPV_EVENT_END_FILE
						    && !file_loaded) {
							msg("moov: "
							    "m̛̿̇al͒f̃un̩cͯt̿io̲n̙͌͢");
							goto done;
						}
					}
				}
				ImGui_ImplSdlGL3_ProcessEvent(&event);
			}
		}

		int w, h;
		SDL_GetWindowSize(window, &w, &h);
		glClearColor(clear_color.x, clear_color.y, clear_color.z,
			     clear_color.w);
		glClear(GL_COLOR_BUFFER_BIT);
		mpv_opengl_cb_draw(mpv_gl, 0, w, -h);
		ui();
		SDL_GL_SwapWindow(window);

		int64_t playlist_pos;
		mpv_get_property(mpv, "playlist-pos", MPV_FORMAT_INT64,
				 &playlist_pos);
		if (is_playlist && playlist_pos > last_completed_track) {
			char *pos_sec =
			    mpv_get_property_string(mpv, "playback-time");
			char *total_sec =
			    mpv_get_property_string(mpv, "duration");
			float progress = 0;
			if (pos_sec && total_sec)
				progress =
				    std::stof(pos_sec) / std::stof(total_sec);
			if (progress > 0.8) {
				last_completed_track = playlist_pos;
				save_track(playlist_pos);
			}
		}
	}

done:
	mpv_opengl_cb_uninit_gl(mpv_gl);
	mpv_terminate_destroy(mpv);
	SDL_GL_DeleteContext(glcontext);
	SDL_DestroyWindow(window);

	return 0;
}
