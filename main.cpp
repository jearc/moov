#include <algorithm>
#include <ctime>
#include <iostream>
#include <mutex>
#include <stddef.h>
#include <stdlib.h>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

#include <GL/gl3w.h>
#include <SDL.h>

#include <mpv/client.h>
#include <mpv/opengl_cb.h>

#include "imgui/imgui.h"
#include "imgui/imgui_impl_sdl_gl3.h"

#include "libs/json.hpp"
using json = nlohmann::json;

static Uint32 wakeup_on_mpv_events;

struct Color {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

struct Message {
    time_t time;
    std::string from;
    std::string text;
    Color color = {255, 255, 255};
};

const uint16_t BUFFER_SIZE = 512;
const char *PIPE = "/tmp/mpvpipe";

const uint16_t MARGIN_SIZE = 8;
const float DEFAULT_OPACITY = 0.75;
const Color USER_COLOR = {173, 216, 230};
const Color SYSTEM_COLOR = {180, 180, 180};

void on_msg(const char *s, bool self);

SDL_Window *window;

mpv_handle *mpv;

char buffer[BUFFER_SIZE];
std::vector<Message> chat_log;
std::mutex chat_log_mutex;

ImVec4 clear_color = ImColor(114, 144, 154);
char chat_input_buf[256] = {0};
bool scroll_to_bottom = true;
bool is_fullscreen = false;
int mouse_x = 0, mouse_y = 0;
int last_mouse_move = 0;
int64_t current_tick = 0;
int64_t last_tick = 0;
bool mouse_over_controls = false;
ImVec2 chatsize = ImVec2(400, 200);
ImVec2 chatpos = ImVec2(MARGIN_SIZE, -chatsize.y - MARGIN_SIZE);
ImVec2 osdsize = ImVec2(400, 78);
ImVec2 osdpos = ImVec2(MARGIN_SIZE, chatpos.y - MARGIN_SIZE - osdsize.y);
float chat_opacity = DEFAULT_OPACITY;
float osd_opacity = DEFAULT_OPACITY;
std::string username = "User";

static void die(const char *msg) {
    std::cerr << "error: " << msg << std::endl;
    exit(1);
}

static void *get_proc_address_mpv(void *fn_ctx, const char *name) {
    return SDL_GL_GetProcAddress(name);
}

static void on_mpv_events(void *ctx) {
    SDL_Event event = {.type = wakeup_on_mpv_events};
    SDL_PushEvent(&event);
}

char *seconds_to_time_string(float t) {
    unsigned int h = floor(t / 3600);
    unsigned int m = floor((t - 3600 * h) / 60);
    unsigned int s = floor(t - 3600 * h - 60 * m);

    char *ts = (char *)malloc(9);
    snprintf(ts, 9, "%02d:%02d:%02d", h, m, s);

    return ts;
}

int get_num_audio_sub_tracks(mpv_handle *mpv, int *naudio, int *nsubs) {
    int64_t ntracks;
    if (mpv_get_property(mpv, "track-list/count", MPV_FORMAT_INT64, &ntracks))
        return -1;
    int naudio_ = 0, nsubs_ = 0;
    for (int i = 0; i < ntracks; i++) {
        std::string prop;
        prop += "track-list/";
        prop += std::to_string(i);
        prop += "/type";
        char *type = NULL;
        if (mpv_get_property(mpv, prop.c_str(), MPV_FORMAT_STRING, &type))
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

void sendmsg(const char *msg) {
    json j;
    j["message"] = msg;
    std::cout << j << std::endl;
}

std::string getstatus() {
    char *playback_time = mpv_get_property_osd_string(mpv, "playback-time");
    char *duration = mpv_get_property_osd_string(mpv, "duration");
    char *playlist_pos = mpv_get_property_osd_string(mpv, "playlist-pos-1");
    char *playlist_count = mpv_get_property_osd_string(mpv, "playlist-count");
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

std::vector<std::string> splitstring(const char *s, char delim) {
    std::vector<std::string> words;

    words.push_back("");
    for (int w = 0; *s; s++) {
        if (*s == delim) {
            w += 1;
            words.push_back("");
        } else {
            words[w] += *s;
        }
    }

    return words;
}

int parse_time(const char *timestr) {
    auto selems = splitstring(timestr, ':');
    std::vector<int> elems;
    for (int i = selems.size() - 1; i >= 0; i--)
        elems.push_back(atoi(selems[i].c_str()));
    auto nelems = elems.size();

    int t = 0;

    if (nelems > 0)
        t += elems[0];
    if (nelems > 1)
        t += elems[1] * 60;
    if (nelems > 2)
        t += elems[2] * 3600;

    return t;
}

void writechat(const char *text, const char *from, Color color) {
    Message msg;
    time_t t = time(0);
    msg.time = t;
    msg.from = from ? from : username;
    msg.text = text;
    msg.color = color;

    std::lock_guard<std::mutex> guard(chat_log_mutex);
    chat_log.push_back(msg);
    scroll_to_bottom = true;
}

void msg(const char *text, const char *from, Color color) {
    if (!text)
        return;

    int len = strlen(text);

    writechat(text, from, color);

    if (!(from || (len > 2 && text[0] == ':')))
        sendmsg(text);

    on_msg(text, from ? false : true);
}

void on_msg(const char *s, bool self) {
    auto words = splitstring(s, ' ');
    std::string com;
    if (words.size() > 0)
        com = words[0];
    else
        return;

    if (com == "pp") {
        mpv_command_string(mpv, "cycle pause");
        auto status = getstatus();
        msg(status.c_str(), NULL, SYSTEM_COLOR);
    } else if (com == "STATUS") {
        auto status = getstatus();
        msg(status.c_str(), NULL, SYSTEM_COLOR);
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
        if (words[1] == "chat_opacity") {
            if (words.size() > 2)
                chat_opacity = atof(words[2].c_str());
            else
                chat_opacity = DEFAULT_OPACITY;
        }
        if (words[1] == "osd_opacity") {
            if (words.size() > 2)
                osd_opacity = atof(words[2].c_str());
            else
                osd_opacity = DEFAULT_OPACITY;
        }
    }
}

void osd() {
    if (!(mouse_over_controls || current_tick - last_mouse_move < 1000))
        return;

    ImVec2 size = osdsize;
    ImVec2 pos;
    pos.x =
        osdpos.x >= 0 ? osdpos.x : (int)ImGui::GetIO().DisplaySize.x + osdpos.x;
    pos.y =
        osdpos.y >= 0 ? osdpos.y : (int)ImGui::GetIO().DisplaySize.y + osdpos.y;
    bool display = true;
    ImGui::SetNextWindowPos(pos);
    ImGui::Begin("StatusDisplay", &display, size, osd_opacity,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoSavedSettings);
    auto filename = mpv_get_property_string(mpv, "filename");
    ImGui::Text(filename);
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
    char *playlist_count = mpv_get_property_osd_string(mpv, "playlist-count");
    if (playback_time && duration) {
        std::string status;
        status += playback_time;
        status += " / ";
        status += duration;
        ImGui::Text(status.c_str());
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
        ImGui::Text(status.c_str());
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
        SDL_SetWindowFullscreen(window, !is_fullscreen);
        is_fullscreen = !is_fullscreen;
    }
    mouse_over_controls = ImGui::IsWindowHovered();
    static bool mouse_down = false;
    static int start_pos_x = 0, start_pos_y = 0;
    static int last_pos_x = mouse_x, last_pos_y = mouse_y;
    bool mouse_over_window = (mouse_x >= pos.x && mouse_x < pos.x + size.x) &&
                             (mouse_y >= pos.y && mouse_y < pos.y + size.y);
    if (ImGui::IsMouseClicked(0) && mouse_over_window) {
        mouse_down = true;
        start_pos_x = mouse_x;
        start_pos_y = mouse_y;
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

void chatbox() {
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
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoSavedSettings);
    ImGui::BeginChild("LogRegion",
                      ImVec2(0, -ImGui::GetItemsLineHeightWithSpacing()), false,
                      ImGuiWindowFlags_NoScrollbar);
    {
        std::lock_guard<std::mutex> guard(chat_log_mutex);
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
            ImGui::PushStyleColor(
                0, ImColor(msg.color.r, msg.color.g, msg.color.b));
            ImGui::TextWrapped(formatted.c_str());
            ImGui::PopStyleColor(1);
        }
    }
    if (scroll_to_bottom) {
        ImGui::SetScrollHere();
        scroll_to_bottom = false;
    }
    ImGui::EndChild();
    ImGui::PushItemWidth(size.x - 8 * 2);
    if (ImGui::InputText("", chat_input_buf, 256,
                         ImGuiInputTextFlags_EnterReturnsTrue, NULL, NULL)) {
        if (strlen(chat_input_buf)) {
            msg(chat_input_buf, NULL, USER_COLOR);
            strcpy(chat_input_buf, "");
        }
    }
    static bool mouse_down = false;
    static int start_pos_x = 0, start_pos_y = 0;
    static int last_pos_x = mouse_x, last_pos_y = mouse_y;
    bool mouse_over_window = (mouse_x >= pos.x && mouse_x < pos.x + size.x) &&
                             (mouse_y >= pos.y && mouse_y < pos.y + size.y);
    if (ImGui::IsMouseClicked(0) && mouse_over_window) {
        mouse_down = true;
        start_pos_x = mouse_x;
        start_pos_y = mouse_y;
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
    if (ImGui::IsItemHovered() ||
        (ImGui::IsRootWindowOrAnyChildFocused() && !ImGui::IsAnyItemActive() &&
         !ImGui::IsMouseClicked(0)))
        ImGui::SetKeyboardFocusHere(-1);
    ImGui::End();
}

void ui() {
    ImGui_ImplSdlGL3_NewFrame(window);

    osd();
    chatbox();

    glViewport(0, 0, (int)ImGui::GetIO().DisplaySize.x,
               (int)ImGui::GetIO().DisplaySize.y);
    ImGui::Render();
}

int main(int argc, char *argv[]) {
    if (argc < 3)
        die("pass a username and media file(s) or URL(s) as arguments");

    username = argv[1];

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

    mpv_opengl_cb_context *mpv_gl =
        (mpv_opengl_cb_context *)mpv_get_sub_api(mpv, MPV_SUB_API_OPENGL_CB);
    if (!mpv_gl)
        die("failed to create mpv GL API handle");

    SDL_GLContext glcontext = SDL_GL_CreateContext(window);
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
    mpv_set_option_string(mpv, "input-ipc-server", PIPE);

    wakeup_on_mpv_events = SDL_RegisterEvents(1);
    if (wakeup_on_mpv_events == (Uint32)-1)
        die("could not register events");
    mpv_set_wakeup_callback(mpv, on_mpv_events, NULL);

    const char *cmd[] = {"loadfile", argv[2], NULL};
    mpv_command(mpv, cmd);
    for (int i = 3; i < argc; i++) {
        const char *cmd2[] = {"loadfile", argv[i], "append", NULL};
        mpv_command(mpv, cmd2);
    }

    auto t = std::thread([]() {
        for (std::string line; std::getline(std::cin, line);) {
            try {
                auto j = json::parse(line);
                std::string user = j["user"];
                std::string message = j["message"];
                int colori = 0xffffff;
                if (j.count("color") && j["color"].is_number()) {
                    colori = (int)j["color"];
                }
                Color color;
                color.r = (0xff0000 & colori) >> 16;
                color.g = (0x00ff00 & colori) >> 8;
                color.b = 0x0000ff & colori;
                msg(message.c_str(), user.c_str(), color);
            } catch (...) {
                std::cerr << "exception: failed to parse input" << std::endl;
            }
        }
    });
    t.detach();

    while (1) {
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
                    SDL_SetWindowFullscreen(window, !is_fullscreen);
                    is_fullscreen = !is_fullscreen;
                } else if (event.key.keysym.mod & KMOD_CTRL &&
                           event.key.keysym.sym == SDLK_SPACE) {
                    mpv_command_string(mpv, "cycle pause");
                } else if (event.key.keysym.sym == SDLK_LEFT) {
                    mpv_command_string(mpv, "playlist-prev");
                } else if (event.key.keysym.sym == SDLK_RIGHT) {
                    mpv_command_string(mpv, "playlist-next");
                } else
                    ImGui_ImplSdlGL3_ProcessEvent(&event);
                break;
            default:
                if (event.type == wakeup_on_mpv_events) {
                    while (1) {
                        mpv_event *mp_event = mpv_wait_event(mpv, 0);
                        if (mp_event->event_id == MPV_EVENT_NONE)
                            break;
                        if (mp_event->event_id == MPV_EVENT_FILE_LOADED) {
                            mpv_command_string(mpv, "set pause yes");
                        }
                        if (mp_event->event_id == MPV_EVENT_PLAYBACK_RESTART) {
                            auto status = getstatus();
                            msg(status.c_str(), NULL, SYSTEM_COLOR);
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
    }

done:
    mpv_opengl_cb_uninit_gl(mpv_gl);
    mpv_terminate_destroy(mpv);
    SDL_GL_DeleteContext(glcontext);
    SDL_DestroyWindow(window);

    return 0;
}
