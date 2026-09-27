#include <SDL3/SDL.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "imgui.h"
#include "menu_layout.h"
#include "serial.h"

namespace {

const int MENU_QUEUE = 32;
const int SERIAL_DEVICE_LIMIT = MENU_SERIAL_DEVICE_END - MENU_SERIAL_DEVICE_FIRST;

int queue[MENU_QUEUE];
int queued = 0;
bool checked[MENU_COUNT];
char serial_devices[SERIAL_DEVICE_LIMIT][64];
int serial_device_count = 0;
char serial_current[512] = "";
std::vector<std::string> firmware_titles;
int firmware_current = -1;

void push(int item) {
    if (queued < MENU_QUEUE) queue[queued++] = item;
}

ImGuiKeyChord key_chord(const menu_entry_t &entry) {
    if (entry.key < 'a' || entry.key > 'z') return ImGuiKey_None;
    ImGuiKeyChord chord = (ImGuiKey)(ImGuiKey_A + (entry.key - 'a'));
    if (entry.modifiers & MENU_KEY_PRIMARY) chord |= ImGuiMod_Alt;
    if (entry.modifiers & MENU_KEY_SHIFT) chord |= ImGuiMod_Shift;
    if (entry.modifiers & MENU_KEY_CONTROL) chord |= ImGuiMod_Ctrl;
    return chord;
}

std::string shortcut_label(const menu_entry_t &entry) {
    if (!entry.key) return "";
    std::string label;
    if (entry.modifiers & MENU_KEY_CONTROL) label += "Ctrl+";
    if (entry.modifiers & MENU_KEY_PRIMARY) label += "Alt+";
    if (entry.modifiers & MENU_KEY_SHIFT) label += "Shift+";
    label += (char)(entry.key - 'a' + 'A');
    return label;
}

std::string plain_title(const char *title) {
    std::string text = title;
    for (size_t at = text.find("\xe2\x80\xa6"); at != std::string::npos; at = text.find("\xe2\x80\xa6", at)) text.replace(at, 3, "...");
    return text;
}

int skip_menu(int index) {
    for (int depth = 1; ++index < MENU_ENTRY_COUNT;) {
        menu_entry_kind_t kind = MENU_ENTRIES[index].kind;
        if (kind == MENU_ENTRY_MENU || kind == MENU_ENTRY_SUBMENU) depth++;
        if (kind == MENU_ENTRY_END && --depth == 0) break;
    }
    return index;
}

void draw_serial() {
    bool on_device = false;
    for (int i = 0; i < serial_device_count; i++) on_device |= strcmp(serial_current, serial_devices[i]) == 0;
    if (ImGui::MenuItem("Off", nullptr, serial_current[0] == 0)) push(MENU_SERIAL_OFF);
#ifndef _WIN32
    std::string virtual_title = "Virtual Port (pty)";
    if (serial_current[0] && !on_device && strcmp(serial_current, "pty") != 0) virtual_title = std::string("Virtual Port at ") + serial_current;
    if (ImGui::MenuItem(virtual_title.c_str(), nullptr, serial_current[0] && !on_device)) push(MENU_SERIAL_PTY);
#endif
    if (serial_device_count) ImGui::Separator();
    for (int i = 0; i < serial_device_count; i++) {
        const char *name = strrchr(serial_devices[i], '/');
        if (ImGui::MenuItem(name ? name + 1 : serial_devices[i], nullptr, strcmp(serial_current, serial_devices[i]) == 0)) {
            push(MENU_SERIAL_DEVICE_FIRST + i);
        }
    }
}

void draw_firmware() {
    for (size_t i = 0; i < firmware_titles.size(); i++) {
        if (ImGui::MenuItem(firmware_titles[i].c_str(), nullptr, (int)i == firmware_current)) push(MENU_FIRMWARE_FIRST + (int)i);
    }
}

int draw_entries(int index) {
    for (; index < MENU_ENTRY_COUNT; index++) {
        const menu_entry_t &entry = MENU_ENTRIES[index];
        switch (entry.kind) {
            case MENU_ENTRY_END: return index;
            case MENU_ENTRY_SEPARATOR: ImGui::Separator(); break;
            case MENU_ENTRY_MENU:
            case MENU_ENTRY_SUBMENU:
                if (ImGui::BeginMenu(plain_title(entry.title).c_str())) {
                    index = draw_entries(index + 1);
                    ImGui::EndMenu();
                } else {
                    index = skip_menu(index);
                }
                break;
            case MENU_ENTRY_ITEM:
                if (ImGui::MenuItem(plain_title(entry.title).c_str(), shortcut_label(entry).c_str(), checked[entry.tag])) push(entry.tag);
                break;
            case MENU_ENTRY_FIRMWARE:
                if (ImGui::BeginMenu(entry.title, !firmware_titles.empty())) {
                    draw_firmware();
                    ImGui::EndMenu();
                }
                break;
            case MENU_ENTRY_SERIAL:
                if (ImGui::BeginMenu(entry.title)) {
                    draw_serial();
                    ImGui::EndMenu();
                }
                break;
        }
    }
    return index;
}

}

void menu_install(void) {
}

void menu_ensure(void) {
}

void menu_draw(void) {
    for (int i = 0; i < MENU_ENTRY_COUNT; i++) {
        const menu_entry_t &entry = MENU_ENTRIES[i];
        if (entry.kind == MENU_ENTRY_ITEM && entry.key && ImGui::IsKeyChordPressed(key_chord(entry))) push(entry.tag);
    }
    bool open = ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsKeyPressed(ImGuiKey_Menu, false);
    if (open && !ImGui::IsPopupOpen("menu")) {
        serial_device_count = serial_list_devices(serial_devices, SERIAL_DEVICE_LIMIT);
        ImGui::OpenPopup("menu");
    }
    if (!ImGui::BeginPopup("menu")) return;
    draw_entries(0);
    ImGui::Separator();
    if (ImGui::MenuItem("Quit")) {
        SDL_Event quit = {};
        quit.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&quit);
    }
    ImGui::EndPopup();
}

void menu_perform(int item) {
    if (item >= 0 && item < MENU_COUNT) push(item);
}

int menu_poll(void) {
    if (queued == 0) return -1;
    int item = queue[0];
    for (int i = 1; i < queued; i++) queue[i - 1] = queue[i];
    queued--;
    return item;
}

void menu_set_checked(int item, bool state) {
    if (item >= 0 && item < MENU_COUNT) checked[item] = state;
}

void menu_set_serial(const char *current) {
    snprintf(serial_current, sizeof serial_current, "%s", current ? current : "");
}

const char *menu_serial_device(int item) {
    int index = item - MENU_SERIAL_DEVICE_FIRST;
    return index >= 0 && index < serial_device_count ? serial_devices[index] : nullptr;
}

void menu_set_firmware(const char *const *titles, int count, int current) {
    firmware_titles.assign(titles, titles + count);
    firmware_current = current;
}

void window_set_transparent(void *nswindow, void *layer, bool transparent) {
    (void)nswindow;
    (void)layer;
    (void)transparent;
}

void window_set_aspect(void *nswindow, float width, float height) {
    (void)nswindow;
    (void)width;
    (void)height;
}
