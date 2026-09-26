#include <SDL3/SDL.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <mutex>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <utility>
#include <vector>

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

#include "beeper.h"
#include "browser.h"
#include "console.h"
#include "device.h"
#include "host.h"
#include "keys.h"
#include "lcd.h"
#include "menu.h"
#include "touch.h"
#include "runtime.h"

static const int IDLE_WAIT_MS = 16;
static const int REDRAW_TAIL_MS = 500;
static const int STARTUP_FRAMES = 150;
static const int TOUCH_RETRY_MS = 2000;

static uint64_t start_ticks = 0;
static std::mutex install_lock;
static std::vector<std::string> pending_installs;

static void SDLCALL install_chosen(void *userdata, const char *const *files, int filter) {
    (void)userdata;
    (void)filter;
    if (!files) return;
    std::lock_guard<std::mutex> guard(install_lock);
    for (int i = 0; files[i]; i++) pending_installs.push_back(files[i]);
}
static SDL_WindowID main_window_id = 0;

extern "C" uint32_t host_ticks_ms(void) {
    return (uint32_t)(SDL_GetTicks() - start_ticks);
}

struct Options {
    std::string rom = "rom/r162.da1";
    std::string model = "OZ-750";
    std::string data = "data";
    std::string apps = "apps";
    std::vector<std::string> install;
    std::string serial;
    std::string screenshot;
    std::string keys;
    std::vector<std::string> exec;
    int frames = 120;
    int width = 1400;
    int height = 900;
    bool dead_columns = false;
    bool fresh = false;
    bool show_console = true;
    bool show_keys = true;
    bool show_keyboard = true;
    int layout = -1;
    int fps = 0;
    float response = 1.0f;
    bool backlight = true;
    bool scratches = true;
    bool wear = false;
    bool touchscreen = false;
    std::string touch_display = "TETRA";
    std::vector<int> menu_items;
};

struct Settings {
    bool show_console;
    int layout;
    bool backlight;
    bool dead_columns;
    bool scratches;
    bool wear;
    bool touchscreen;
    int fps;
    float response;
    int width;
    int height;

    bool operator==(const Settings &other) const {
        return show_console == other.show_console && layout == other.layout && backlight == other.backlight &&
               dead_columns == other.dead_columns && scratches == other.scratches && wear == other.wear && touchscreen == other.touchscreen && fps == other.fps && response == other.response &&
               width == other.width && height == other.height;
    }
};

static std::string settings_path(const std::string &data) {
    return data + "/emu.ini";
}

static void load_settings(const std::string &data, Options &options) {
    FILE *file = fopen(settings_path(data).c_str(), "r");
    if (!file) return;
    char key[64];
    char value[64];
    while (fscanf(file, " %63[^=]=%63s", key, value) == 2) {
        std::string name = key;
        if (name == "show_console" || name == "show_console") options.show_console = atoi(value) != 0;
        else if (name == "show_keys") options.show_keys = atoi(value) != 0;
        else if (name == "show_keyboard") options.show_keyboard = atoi(value) != 0;
        else if (name == "layout") options.layout = atoi(value);
        else if (name == "backlight") options.backlight = atoi(value) != 0;
        else if (name == "dead_columns") options.dead_columns = atoi(value) != 0;
        else if (name == "scratches") options.scratches = atoi(value) != 0;
        else if (name == "wear") options.wear = atoi(value) != 0;
        else if (name == "touchscreen") options.touchscreen = atoi(value) != 0;
        else if (name == "fps") options.fps = atoi(value);
        else if (name == "response") options.response = (float)atof(value);
        else if (name == "width") options.width = atoi(value);
        else if (name == "height") options.height = atoi(value);
    }
    fclose(file);
}

static void save_settings(const std::string &data, const Settings &settings) {
    std::string path = settings_path(data);
    std::string temporary = path + ".tmp";
    FILE *file = fopen(temporary.c_str(), "w");
    if (!file) return;
    fprintf(file, "show_console=%d\nlayout=%d\nbacklight=%d\ndead_columns=%d\nscratches=%d\nwear=%d\ntouchscreen=%d\nfps=%d\nresponse=%g\nwidth=%d\nheight=%d\n",
            settings.show_console, settings.layout, settings.backlight, settings.dead_columns, settings.scratches, settings.wear, settings.touchscreen, settings.fps,
            settings.response, settings.width, settings.height);
    fclose(file);
    rename(temporary.c_str(), path.c_str());
}

static std::string data_argument(int argc, char **argv) {
    std::string data = "data";
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--data=", 7) == 0) data = argv[i] + 7;
    }
    return data;
}

static int menu_item_named(const std::string &name) {
    static const std::pair<const char *, int> names[] = {
        { "reload", MENU_RELOAD }, { "interrupt", MENU_INTERRUPT }, { "initialize", MENU_INITIALIZE }, { "test-mode", MENU_TEST_MODE }, { "show-console", MENU_SHOW_CONSOLE },
        { "focus-console", MENU_FOCUS_CONSOLE }, { "backlight", MENU_BACKLIGHT }, { "dead-columns", MENU_DEAD_COLUMNS },
        { "period", MENU_FPS_FIRST + 5 }, { "sound", MENU_SOUND },
        { "next-layout", MENU_LAYOUT_NEXT }, { "apps", MENU_APP_BROWSER }, { "scratches", MENU_SCRATCHES }, { "wear", MENU_WEAR }, { "touchscreen", MENU_TOUCHSCREEN },
    };
    for (auto &entry : names) {
        if (name == entry.first) return entry.second;
    }
    return -1;
}

static void usage() {
    printf(
        "usage: sham7x0 [options]\n"
        "  --rom=FILE          firmware image (default rom/r162.da1)\n"
        "  --data=DIR          settings and saved machine state (default data)\n"
        "  --fresh             ignore and do not write the saved machine state\n"
        "  --apps=DIR          App Browser catalogue and where Install .wzd starts looking (default apps)\n"
        "  --model=NAME        model printed on the case: OZ-750 (default) or ZQ-770\n"
        "  --install=FILE      install a .wzd program after boot, repeatable\n"
        "  --serial[=TARGET]   connect the UART: a serial device such as /dev/cu.usbmodem1101, or a pty linked at TARGET\n"
        "  --size=WxH          window size (default 1400x900)\n"
        "  --dead-columns      simulate failed LCD column drivers\n"
        "  --no-console        start with the console hidden\n"
        "  --no-keys           start without the device keys around the screen\n"
        "  --no-keyboard       start without the keyboard\n"
        "  --layout=N          0 screen only, 1 screen & frame, 2 screen & buttons, 3 screen & keyboard\n"
        "  --touchscreen[=NAME]  take over the named touch display (default TETRA)\n"
        "  --no-touchscreen    stay in a normal window\n"
        "  --period            run the device at a period accurate 10 fps\n"
        "  --fps=N             device frame rate, 0 for unlimited (default 0)\n"
        "  --response=N        LCD response time scale, 0 instant, 1 normal, 4 very slow\n"
        "  --menu=ITEMS        trigger menu items after boot: reload, interrupt, initialize, test-mode, show-console,\n"
        "                      focus-console, backlight, dead-columns, sound, period, apps,\n"
        "                      show-keys\n"
        "  --keys=SEQUENCE     type into the device after boot, {DOWN} {ENTER} {F1}, {+LEFT} holds, {-LEFT} releases\n"
        "  --exec=COMMAND      run a console command after boot, repeatable (type help in the console)\n"
        "  --screenshot=FILE   save the window as BMP after --frames and exit\n"
        "  --frames=N          frames before the screenshot (default 120)\n");
}

static std::string absolute(const std::string &path) {
    if (path.empty() || path[0] == '/') return path;
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof cwd)) return path;
    return std::string(cwd) + "/" + path;
}

static bool parse_options(int argc, char **argv, Options &options) {
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        auto value = [&](const char *prefix) -> const char * {
            size_t len = strlen(prefix);
            return arg.compare(0, len, prefix) == 0 ? argv[i] + len : nullptr;
        };
        if (const char *v = value("--rom=")) options.rom = v;
        else if (const char *v = value("--data=")) options.data = v;
        else if (const char *v = value("--apps=")) options.apps = v;
        else if (const char *v = value("--model=")) options.model = v;
        else if (const char *v = value("--install=")) options.install.push_back(v);
        else if (arg == "--serial") options.serial = "pty";
        else if (const char *v = value("--serial=")) options.serial = v;
        else if (const char *v = value("--screenshot=")) options.screenshot = v;
        else if (const char *v = value("--frames=")) options.frames = atoi(v);
        else if (const char *v = value("--keys=")) options.keys = v;
        else if (const char *v = value("--exec=")) options.exec.push_back(v);
        else if (const char *v = value("--size=")) sscanf(v, "%dx%d", &options.width, &options.height);
        else if (arg == "--dead-columns") options.dead_columns = true;
        else if (arg == "--fresh") options.fresh = true;
        else if (arg == "--no-console" || arg == "--no-repl") options.show_console = false;
        else if (arg == "--no-keys") options.layout = 1;
        else if (arg == "--no-keyboard") options.layout = 2;
        else if (arg == "--touchscreen") options.touchscreen = true;
        else if (arg == "--no-touchscreen") options.touchscreen = false;
        else if (const char *v = value("--touchscreen=")) {
            options.touchscreen = true;
            options.touch_display = v;
        }
        else if (const char *v = value("--layout=")) options.layout = atoi(v);
        else if (arg == "--period") options.fps = 10;
        else if (const char *v = value("--fps=")) options.fps = atoi(v);
        else if (const char *v = value("--response=")) options.response = (float)atof(v);
        else if (const char *v = value("--menu=")) {
            std::string list = v;
            size_t start = 0;
            while (start <= list.size()) {
                size_t comma = list.find(',', start);
                std::string name = list.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
                int item = menu_item_named(name);
                if (item < 0) {
                    usage();
                    return false;
                }
                options.menu_items.push_back(item);
                if (comma == std::string::npos) break;
                start = comma + 1;
            }
        }
        else {
            usage();
            return false;
        }
    }
    return true;
}

static uint32_t special_key(SDL_Keycode key) {
    switch (key) {
        case SDLK_UP:        return HOST_KEY_UP;
        case SDLK_DOWN:      return HOST_KEY_DOWN;
        case SDLK_LEFT:      return HOST_KEY_LEFT;
        case SDLK_RIGHT:     return HOST_KEY_RIGHT;
        case SDLK_HOME:      return HOST_KEY_HOME;
        case SDLK_END:       return HOST_KEY_END;
        case SDLK_PAGEUP:    return HOST_KEY_PGUP;
        case SDLK_PAGEDOWN:  return HOST_KEY_PGDN;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:  return HOST_KEY_ENTER;
        case SDLK_ESCAPE:    return HOST_KEY_ESC;
        case SDLK_BACKSPACE: return HOST_KEY_BACKSPACE;
        case SDLK_DELETE:    return HOST_KEY_DELETE;
        case SDLK_TAB:       return HOST_KEY_TAB;
        default: break;
    }
    if (key >= SDLK_F1 && key <= SDLK_F12) return HOST_KEY_F1 + (key - SDLK_F1);
    return 0;
}

static SDL_Keycode sdl_key(uint32_t code) {
    static const std::pair<uint32_t, SDL_Keycode> table[] = {
        { HOST_KEY_UP, SDLK_UP }, { HOST_KEY_DOWN, SDLK_DOWN }, { HOST_KEY_LEFT, SDLK_LEFT }, { HOST_KEY_RIGHT, SDLK_RIGHT },
        { HOST_KEY_HOME, SDLK_HOME }, { HOST_KEY_END, SDLK_END }, { HOST_KEY_PGUP, SDLK_PAGEUP },
        { HOST_KEY_PGDN, SDLK_PAGEDOWN }, { HOST_KEY_ENTER, SDLK_RETURN }, { HOST_KEY_ESC, SDLK_ESCAPE },
        { HOST_KEY_BACKSPACE, SDLK_BACKSPACE }, { HOST_KEY_DELETE, SDLK_DELETE }, { HOST_KEY_TAB, SDLK_TAB },
    };
    for (auto &entry : table) {
        if (entry.first == code) return entry.second;
    }
    if (code >= HOST_KEY_F1 && code < HOST_KEY_F1 + 12) return SDLK_F1 + (code - HOST_KEY_F1);
    return 0;
}

static void push_sdl_key(SDL_Keycode key, bool down) {
    SDL_Event event = {};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.key = key;
    event.key.scancode = SDL_GetScancodeFromKey(key, nullptr);
    event.key.down = down;
    event.key.windowID = main_window_id;
    SDL_PushEvent(&event);
}

static void push_mouse(uint8_t kind, float x, float y) {
    SDL_Event event = {};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.x = x;
    event.motion.y = y;
    event.motion.windowID = main_window_id;
    event.motion.state = kind == TOUCH_UP ? 0 : SDL_BUTTON_LMASK;
    SDL_PushEvent(&event);
    if (kind == TOUCH_MOVE) return;
    event = {};
    event.type = kind == TOUCH_DOWN ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.down = kind == TOUCH_DOWN;
    event.button.clicks = 1;
    event.button.x = x;
    event.button.y = y;
    event.button.windowID = main_window_id;
    SDL_PushEvent(&event);
}

struct Touchscreen {
    bool active = false;
    bool reported_missing = false;
    SDL_Rect windowed = {};
    SDL_Rect panel = {};
};

static SDL_DisplayID find_display(const std::string &name) {
    int count = 0;
    SDL_DisplayID *ids = SDL_GetDisplays(&count);
    SDL_DisplayID found = 0;
    for (int i = 0; i < count && !found; i++) {
        const char *display = SDL_GetDisplayName(ids[i]);
        if (display && strstr(display, name.c_str())) found = ids[i];
    }
    SDL_free(ids);
    return found;
}

static bool set_touchscreen(SDL_Window *window, Touchscreen &touch, bool enable, const std::string &name) {
    void *native = SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
    if (enable == touch.active) return true;
    if (enable) {
        SDL_DisplayID display = find_display(name);
        if (!display) {
            if (!touch.reported_missing) SDL_Log("touchscreen: no display matching %s, waiting for it", name.c_str());
            touch.reported_missing = true;
            return false;
        }
        touch.reported_missing = false;
        SDL_Rect bounds;
        SDL_GetDisplayBounds(display, &bounds);
        SDL_GetWindowPosition(window, &touch.windowed.x, &touch.windowed.y);
        SDL_GetWindowSize(window, &touch.windowed.w, &touch.windowed.h);
        SDL_SetWindowBordered(window, false);
        SDL_SetWindowResizable(window, false);
        SDL_SetWindowPosition(window, bounds.x, bounds.y);
        SDL_SetWindowSize(window, bounds.w, bounds.h);
        window_cover_display(native, true);
        touch_set_panel(bounds.w, bounds.h);
        touch.panel = bounds;
        touch_start();
        SDL_RaiseWindow(window);
        SDL_Log("touchscreen: covering %s (%dx%d at %d,%d)", SDL_GetDisplayName(display), bounds.w, bounds.h, bounds.x, bounds.y);
    } else {
        window_cover_display(native, false);
        SDL_SetWindowBordered(window, true);
        SDL_SetWindowResizable(window, true);
        SDL_SetWindowSize(window, touch.windowed.w, touch.windowed.h);
        SDL_SetWindowPosition(window, touch.windowed.x, touch.windowed.y);
    }
    touch.active = enable;
    return true;
}

static void push_click(float x, float y) {
    SDL_Event event = {};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.x = x;
    event.motion.y = y;
    event.motion.windowID = main_window_id;
    SDL_PushEvent(&event);
    event = {};
    event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.down = true;
    event.button.clicks = 1;
    event.button.x = x;
    event.button.y = y;
    event.button.windowID = main_window_id;
    SDL_PushEvent(&event);
    event.type = SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.down = false;
    SDL_PushEvent(&event);
}

static uint32_t held_code(SDL_Keycode key) {
    if (uint32_t code = special_key(key)) return code;
    if (key >= SDLK_SPACE && key <= SDLK_Z) return (uint32_t)key;
    return 0;
}

static uint8_t modifiers(SDL_Keymod mod) {
    uint8_t mods = 0;
    if (mod & SDL_KMOD_SHIFT) mods |= HOST_MOD_SHIFT;
    if (mod & SDL_KMOD_CTRL)  mods |= HOST_MOD_CTRL;
    if (mod & SDL_KMOD_ALT)   mods |= HOST_MOD_ALT;
    if (mod & SDL_KMOD_GUI)   mods |= HOST_MOD_CMD;
    return mods;
}

static void push_text(const char *text) {
    const unsigned char *p = (const unsigned char *)text;
    while (*p) {
        uint32_t codepoint = *p++;
        int extra = codepoint >= 0xf0 ? 3 : codepoint >= 0xe0 ? 2 : codepoint >= 0xc0 ? 1 : 0;
        if (extra) codepoint &= 0x3f >> extra;
        while (extra-- > 0 && *p) codepoint = (codepoint << 6) | (*p++ & 0x3f);
        keys_push(codepoint, 0);
    }
}

enum KeyAction { PRESS, HOLD, RELEASE, CLICK };

struct KeyScript {
    struct Step {
        uint32_t code;
        KeyAction action;
        float x = -1;
        float y = -1;
    };
    std::vector<Step> steps;
    size_t next = 0;
    int start_frame = 60;
    int interval = 4;
    float click_x = 0;
    float click_y = 0;
    float window_w = 0;
    float window_h = 0;

    static uint32_t named(const std::string &name) {
        static const std::pair<const char *, uint32_t> names[] = {
            { "UP", HOST_KEY_UP }, { "DOWN", HOST_KEY_DOWN }, { "LEFT", HOST_KEY_LEFT }, { "RIGHT", HOST_KEY_RIGHT },
            { "ENTER", HOST_KEY_ENTER }, { "ESC", HOST_KEY_ESC }, { "BS", HOST_KEY_BACKSPACE },
            { "DEL", HOST_KEY_DELETE }, { "TAB", HOST_KEY_TAB }, { "HOME", HOST_KEY_HOME }, { "END", HOST_KEY_END },
            { "PGUP", HOST_KEY_PGUP }, { "PGDN", HOST_KEY_PGDN }, { "SPACE", ' ' },
            { "NEW", HOST_KEY_NEW }, { "SMBL", HOST_KEY_SMBL }, { "SEARCH", HOST_KEY_SEARCH }, { "CASE", HOST_KEY_CASE },
            { "CUT", HOST_KEY_CUT }, { "COPY", HOST_KEY_COPY }, { "PASTE", HOST_KEY_PASTE }, { "EDIT", HOST_KEY_EDIT },
            { "SYNC", HOST_KEY_SYNC }, { "PICK", HOST_KEY_PICK },
        };
        if (name.size() > 1 && name[0] == 'F' && isdigit((unsigned char)name[1])) {
            return HOST_KEY_F1 + atoi(name.c_str() + 1) - 1;
        }
        for (auto &entry : names) {
            if (name == entry.first) return entry.second;
        }
        return name.size() == 1 ? (uint32_t)(unsigned char)name[0] : 0;
    }

    void parse(const std::string &script) {
        for (size_t i = 0; i < script.size(); i++) {
            if (script[i] != '{') {
                steps.push_back({ (uint8_t)script[i], PRESS });
                continue;
            }
            size_t close = script.find('}', i);
            if (close == std::string::npos) break;
            std::string name = script.substr(i + 1, close - i - 1);
            i = close;
            KeyAction action = PRESS;
            if (!name.empty() && (name[0] == '+' || name[0] == '-')) {
                action = name[0] == '+' ? HOLD : RELEASE;
                name = name.substr(1);
            }
            if (name == "CLICK") {
                steps.push_back({ 1, CLICK });
                continue;
            }
            if (name.compare(0, 6, "CLICK:") == 0) {
                Step step = { 1, CLICK };
                sscanf(name.c_str() + 6, "%f,%f", &step.x, &step.y);
                steps.push_back(step);
                continue;
            }
            steps.push_back({ name == "WAIT" ? 0 : named(name), action });
        }
    }

    void step(int frame) {
        if (next >= steps.size() || frame < start_frame || (frame - start_frame) % interval) return;
        Step current = steps[next++];
        if (!current.code) return;
        if (current.action == CLICK) {
            if (current.x >= 0) push_click(current.x * window_w, current.y * window_h);
            else push_click(click_x, click_y);
            return;
        }
        SDL_Keycode key = sdl_key(current.code);
        if (key && current.action == PRESS) {
            push_sdl_key(key, true);
            push_sdl_key(key, false);
            return;
        }
        if (current.action == PRESS) keys_push(current.code, 0);
        else if (current.action == HOLD) { keys_set_held(current.code, true); keys_push(current.code, 0); }
        else keys_set_held(current.code, false);
    }
};

static void set_console_visible(SDL_Window *window, bool visible, const DeviceState &device, int &restore_height) {
    int width = 0, height = 0;
    SDL_GetWindowSize(window, &width, &height);
    int device_height = (int)device_fit_height((float)width, device);
    if (visible) {
        SDL_SetWindowSize(window, width, std::max(restore_height, device_height + 200));
    } else {
        restore_height = height;
        SDL_SetWindowSize(window, width, device_height);
    }
}

static void draw_transfer_progress(float width) {
    float fraction = 0;
    const char *description = nullptr;
    int waiting = 0;
    if (!runtime_transfer_progress(&fraction, &description, &waiting)) return;
    char label[160];
    if (waiting) snprintf(label, sizeof label, "sending %s  %.0f%%  (%d more waiting)", description, fraction * 100, waiting);
    else snprintf(label, sizeof label, "sending %s  %.0f%%", description, fraction * 100);
    ImGui::ProgressBar(fraction, ImVec2(width, 0), label);
}

static void save_screenshot(SDL_Renderer *renderer, const std::string &path) {
    SDL_Surface *surface = SDL_RenderReadPixels(renderer, nullptr);
    if (!surface) {
        SDL_Log("screenshot failed: %s", SDL_GetError());
        return;
    }
    if (SDL_SaveBMP(surface, path.c_str())) SDL_Log("screenshot: %s", path.c_str());
    else SDL_Log("screenshot failed: %s", SDL_GetError());
    SDL_DestroySurface(surface);
}

int main(int argc, char **argv) {
    Options options;
    load_settings(absolute(data_argument(argc, argv)), options);
    if (!parse_options(argc, argv, options)) return 1;
    if (options.model != "OZ-750" && options.model != "ZQ-770") {
        usage();
        return 1;
    }
    device_set_model(options.model.c_str());

    options.rom = absolute(options.rom);
    options.apps = absolute(options.apps);
    options.data = absolute(options.data);
    options.screenshot = absolute(options.screenshot);
    mkdir(options.data.c_str(), 0755);
    setvbuf(stdout, nullptr, _IONBF, 0);

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    start_ticks = SDL_GetTicks();

    SDL_Window *window = SDL_CreateWindow(("SHAM " + options.model).c_str(), options.width, options.height,
                                          SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
                                          (options.screenshot.empty() ? 0 : SDL_WINDOW_HIDDEN));
    SDL_Renderer *renderer = window ? SDL_CreateRenderer(window, nullptr) : nullptr;
    if (!renderer) {
        SDL_Log("window/renderer failed: %s", SDL_GetError());
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);
    main_window_id = SDL_GetWindowID(window);
    beeper_init();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    io.Fonts->AddFontDefault();
    const char *label_font = "/System/Library/Fonts/Supplemental/Arial Bold.ttf";
    if (access(label_font, R_OK) == 0) device_set_label_font(io.Fonts->AddFontFromFileTTF(label_font, 16.0f));
    const char *keyboard_font = "/System/Library/Fonts/HelveticaNeue.ttc";
    if (access(keyboard_font, R_OK) == 0) {
        ImFontConfig medium;
        medium.FontNo = 10;
        ImFontConfig regular;
        regular.FontNo = 0;
        device_set_keyboard_fonts(io.Fonts->AddFontFromFileTTF(keyboard_font, 16.0f, &medium),
                                  io.Fonts->AddFontFromFileTTF(keyboard_font, 16.0f, &regular));
    }
    std::string icon_font = std::string(SDL_GetBasePath() ? SDL_GetBasePath() : "") + "assets/MaterialSymbolsKeys.ttf";
    if (access(icon_font.c_str(), R_OK) == 0) device_set_icon_font(io.Fonts->AddFontFromFileTTF(icon_font.c_str(), 24.0f));
    ImGuiStyle &style = ImGui::GetStyle();
    style.FontSizeBase = 14.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.11f, 0.12f, 1.0f);
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    srand((unsigned)SDL_GetTicks() ^ (unsigned)time(nullptr));
    if (options.dead_columns) lcd_set_dead_columns(true);
    lcd_set_backlight(options.backlight);

    bool persist = !options.fresh && (options.screenshot.empty() || getenv("POCKET_PERSIST"));
    host_config_t config = { options.rom.c_str(), options.data.c_str(), persist };
    if (!runtime_init(&config)) return 1;
    for (auto &path : options.install) runtime_install_wzd(absolute(path).c_str());
    if (!options.serial.empty()) runtime_set_serial(options.serial == "pty" ? "pty" : absolute(options.serial).c_str());

    for (auto &line : options.exec) console_submit(line.c_str());
    KeyScript script;
    script.parse(options.keys);

    browser_set_directory(options.apps.c_str());
    menu_install();

    bool running = true;
    DeviceState device;
    int layout = options.layout >= 0 ? options.layout : !options.show_keys ? 1 : !options.show_keyboard ? 2 : 3;
    layout = std::max(0, std::min(3, layout));
    auto apply_layout = [&]() {
        device.screen_only = layout == 0;
        device.show_keys = layout >= 2;
        device.show_keyboard = layout == 3;
    };
    apply_layout();
    device.scratches = options.scratches;
    device.wear = options.wear;
    bool &device_focused = device.focused;
    bool show_console = options.show_console;
    Touchscreen touch;
    bool want_touchscreen = options.touchscreen;
    int restore_height = options.height;
    if (!show_console) set_console_visible(window, false, device, restore_height);
    int fps = options.fps;
    float response = options.response;
    lcd_set_response(response);
    uint64_t last_device_ms = 0;
    int frame = 0;

    bool idle = false;
    uint64_t redraw_until_ms = 0;
    uint64_t touch_retry_ms = 0;
    while (running) {
        if (idle) SDL_WaitEventTimeout(nullptr, IDLE_WAIT_MS);
        bool had_event = false;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            had_event = true;
            bool device_tab = device_focused && (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) &&
                              event.key.key == SDLK_TAB;
            if (!device_tab) ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) running = false;
            if (event.type == SDL_EVENT_KEY_DOWN) {
                SDL_Keycode key = event.key.key;
                SDL_Keymod mod = event.key.mod;
                if ((mod & SDL_KMOD_CTRL) && key == SDLK_C) continue;
                if (!device_focused || !device.powered || (mod & SDL_KMOD_GUI)) continue;
                if (uint32_t code = held_code(key)) keys_set_held(code, true);
                if (uint32_t code = special_key(key)) {
                    keys_push(code, modifiers(mod));
                } else if ((mod & SDL_KMOD_CTRL) && key >= SDLK_A && key <= SDLK_Z) {
                    keys_push('a' + (key - SDLK_A), modifiers(mod));
                }
            }
            if (event.type == SDL_EVENT_KEY_UP) {
                if (uint32_t code = held_code(event.key.key)) keys_set_held(code, false);
            }
            if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) keys_release_all();
            if (event.type == SDL_EVENT_TEXT_INPUT && device_focused && device.powered) {
                if (!(SDL_GetModState() & (SDL_KMOD_CTRL | SDL_KMOD_GUI))) push_text(event.text.text);
            }
        }

        for (int item = menu_poll(); item >= 0; item = menu_poll()) {
            had_event = true;
            if (item >= MENU_FPS_FIRST && item < MENU_FPS_END) fps = MENU_FPS_VALUES[item - MENU_FPS_FIRST];
            if (item >= MENU_RESPONSE_FIRST && item < MENU_RESPONSE_END) {
                response = MENU_RESPONSE_VALUES[item - MENU_RESPONSE_FIRST];
                lcd_set_response(response);
            }
            if (item >= MENU_SERIAL_DEVICE_FIRST && item < MENU_SERIAL_DEVICE_END) {
                if (const char *path = menu_serial_device(item)) runtime_set_serial(path);
            }
            if (item >= MENU_LAYOUT_FIRST && item < MENU_LAYOUT_END) {
                layout = item - MENU_LAYOUT_FIRST;
                apply_layout();
                if (!show_console) set_console_visible(window, false, device, restore_height);
            }
            switch (item) {
                case MENU_RELOAD:       runtime_request_reload(); break;
                case MENU_INTERRUPT:
                    if (device_focused || runtime_console_busy()) runtime_interrupt();
                    else console_cancel();
                    break;
                case MENU_INITIALIZE:   runtime_initialize_memory(); break;
                case MENU_TEST_MODE:    runtime_enter_test_mode(); break;
                case MENU_INSTALL_WZD: {
                    static const SDL_DialogFileFilter filters[] = { { "Sharp organizer programs", "wzd" } };
                    SDL_ShowOpenFileDialog(install_chosen, nullptr, window, filters, 1, options.apps.c_str(), true);
                    break;
                }
                case MENU_APP_BROWSER:  browser_toggle(); break;
                case MENU_SERIAL_OFF:   runtime_set_serial(nullptr); break;
                case MENU_SERIAL_PTY:   runtime_set_serial("pty"); break;
                case MENU_SHOW_CONSOLE:
                    show_console = !show_console;
                    set_console_visible(window, show_console, device, restore_height);
                    break;
                case MENU_FOCUS_CONSOLE:
                    if (!show_console) set_console_visible(window, true, device, restore_height);
                    show_console = true;
                    console_focus();
                    break;
                case MENU_BACKLIGHT:    lcd_set_backlight(!lcd_get_backlight()); break;
                case MENU_DEAD_COLUMNS: lcd_set_dead_columns(!lcd_get_dead_columns()); break;
                case MENU_SOUND:        beeper_set_sound(!beeper_sound()); break;
                case MENU_SCRATCHES:    device.scratches = !device.scratches; break;
                case MENU_WEAR:         device.wear = !device.wear; break;
                case MENU_TOUCHSCREEN:
                    want_touchscreen = !touch.active;
                    touch.reported_missing = false;
                    set_touchscreen(window, touch, want_touchscreen, options.touch_display);
                    break;
                case MENU_LAYOUT_NEXT:
                    layout = (layout + 1) % 4;
                    apply_layout();
                    if (!show_console) set_console_visible(window, false, device, restore_height);
                    break;
                default: break;
            }
        }
        if (options.screenshot.empty() || getenv("POCKET_PERSIST")) {
            static Settings saved = {};
            static bool have_saved = false;
            int window_w = 0, window_h = 0;
            SDL_GetWindowSize(window, &window_w, &window_h);
            Settings current = { show_console, layout, lcd_get_backlight(), lcd_get_dead_columns(), device.scratches, device.wear, want_touchscreen, fps, response,
                                 touch.active ? touch.windowed.w : window_w,
                                 touch.active ? touch.windowed.h : show_console ? window_h : restore_height };
            if (!have_saved) {
                saved = current;
                have_saved = true;
            } else if (!(current == saved)) {
                save_settings(options.data, current);
                saved = current;
            }
        }
        {
            std::lock_guard<std::mutex> guard(install_lock);
            for (auto &path : pending_installs) runtime_install_wzd(path.c_str());
            pending_installs.clear();
        }
        menu_ensure();
        menu_set_checked(MENU_SHOW_CONSOLE, show_console);
        menu_set_checked(MENU_BACKLIGHT, lcd_get_backlight());
        menu_set_checked(MENU_DEAD_COLUMNS, lcd_get_dead_columns());
        for (int i = 0; i < MENU_FPS_END - MENU_FPS_FIRST; i++) menu_set_checked(MENU_FPS_FIRST + i, fps == MENU_FPS_VALUES[i]);
        for (int i = 0; i < MENU_RESPONSE_END - MENU_RESPONSE_FIRST; i++) {
            menu_set_checked(MENU_RESPONSE_FIRST + i, response == MENU_RESPONSE_VALUES[i]);
        }
        menu_set_checked(MENU_SOUND, beeper_sound());
        for (int i = 0; i < MENU_LAYOUT_END - MENU_LAYOUT_FIRST; i++) menu_set_checked(MENU_LAYOUT_FIRST + i, layout == i);
        menu_set_checked(MENU_SCRATCHES, device.scratches);
        menu_set_checked(MENU_WEAR, device.wear);
        menu_set_checked(MENU_TOUCHSCREEN, touch.active);
        menu_set_serial(runtime_serial_target());

        script.click_x = io.DisplaySize.x * 0.5f;
        script.click_y = io.DisplaySize.y * 0.25f;
        script.window_w = io.DisplaySize.x;
        script.window_h = io.DisplaySize.y;
        script.step(frame);
        if (want_touchscreen && !touch.active && SDL_GetTicks() >= touch_retry_ms) {
            touch_retry_ms = SDL_GetTicks() + TOUCH_RETRY_MS;
            set_touchscreen(window, touch, true, options.touch_display);
        }
        if (const char *probe = getenv("POCKET_TOUCH_PROBE"); probe && touch.active && (frame == 80 || frame == 82)) {
            float x = 0, y = 0;
            sscanf(probe, "%f,%f", &x, &y);
            push_mouse(frame == 80 ? TOUCH_DOWN : TOUCH_UP, x, y);
        }
        for (touch_event_t event; touch_pop(&event);) {
            if (!touch.active) continue;
            int window_x = 0, window_y = 0;
            SDL_GetWindowPosition(window, &window_x, &window_y);
            push_mouse(event.kind, event.x + touch.panel.x - window_x, event.y + touch.panel.y - window_y);
        }
        device.touch = touch.active;
        if (frame == 45) {
            for (int item : options.menu_items) menu_perform(item);
        }
        uint64_t now_ms = SDL_GetTicks();
        float compose_seconds = 0;
        if (fps <= 0 || now_ms - last_device_ms >= (uint64_t)(1000 / fps)) {
            compose_seconds = last_device_ms ? (now_ms - last_device_ms) / 1000.0f : 1.0f / 60.0f;
            last_device_ms = now_ms;
            if (device.powered) runtime_step();
        }
        float transfer_fraction = 0;
        const char *transfer_description = nullptr;
        int transfer_waiting = 0;
        bool busy = had_event || frame < STARTUP_FRAMES || !options.screenshot.empty() || script.next < script.steps.size() ||
                    lcd_needs_compose() || console_take_changed() || SDL_GetMouseState(nullptr, nullptr) != 0 ||
                    runtime_transfer_progress(&transfer_fraction, &transfer_description, &transfer_waiting);
        if (busy) redraw_until_ms = now_ms + REDRAW_TAIL_MS;
        idle = now_ms >= redraw_until_ms;
        if (idle) continue;

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("root", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

        float total_height = ImGui::GetContentRegionAvail().y;
        float device_share = device.show_keys && device.show_keyboard ? 0.72f : 0.52f;
        bool console_visible = show_console && !touch.active;
        float device_height = console_visible ? std::max(220.0f, total_height * device_share) : total_height;
        device_draw(renderer, io.DisplayFramebufferScale.x, device_height, compose_seconds, device);

        if (console_visible) {
            ImGui::TextDisabled("%s", device_focused ? "keys -> device  (Cmd-L: console)" : "keys -> console  (Esc: device)");
            ImGui::SameLine();
            draw_transfer_progress(320);
            ImGui::SameLine(ImGui::GetContentRegionMax().x - 200);
            if (fps > 0) {
                ImGui::TextDisabled("%s  %.0f fps  LCD %d fps", runtime_idle() ? "idle" : "running", io.Framerate, fps);
            } else {
                ImGui::TextDisabled("%s  %.0f fps", runtime_idle() ? "idle" : "running", io.Framerate);
            }
            ImGui::Separator();
            console_draw();
        }
        ImGui::End();
        if (!console_visible) {
            float fraction = 0;
            const char *description = nullptr;
            int waiting = 0;
            if (runtime_transfer_progress(&fraction, &description, &waiting)) {
                ImGui::SetNextWindowPos(ImVec2(12, io.DisplaySize.y - 12), ImGuiCond_Always, ImVec2(0, 1));
                ImGui::SetNextWindowBgAlpha(0.85f);
                ImGui::Begin("transfer", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                                  ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs);
                draw_transfer_progress(360);
                ImGui::End();
            }
        }
        browser_draw(renderer);

        ImGui::Render();
        bool device_keys = !io.WantTextInput && !browser_focused();
        if (device_focused && !device_keys) keys_release_all();
        device_focused = device_keys;
        if (device_focused && !SDL_TextInputActive(window)) SDL_StartTextInput(window);

        SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
        SDL_SetRenderDrawColor(renderer, 26, 28, 31, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        device_flush_bake(renderer);

        frame++;
        if (!options.screenshot.empty() && frame >= options.frames) {
            save_screenshot(renderer, options.screenshot);
            running = false;
        }
        SDL_RenderPresent(renderer);
    }

    set_touchscreen(window, touch, false, options.touch_display);
    touch_stop();
    runtime_deinit();
    beeper_deinit();
    browser_shutdown();
    device_shutdown();
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
