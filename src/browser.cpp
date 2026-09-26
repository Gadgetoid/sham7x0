#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "imgui.h"
#include "browser.h"
#include "runtime.h"

namespace {

typedef std::map<std::string, std::string> Record;

struct App {
    std::string directory;
    std::string file;
    std::string original_file;
    std::string title;
    std::string data_type;
    std::string category;
    std::string description;
    std::string alert;
    std::string source_url;
    std::string screenshot;
    std::string searchable;
    bool program;
};

struct Kind {
    const char *directory;
    const char *label;
    bool program;
};

const Kind KINDS[] = {
    { "programs", "Programs", true },
    { "basic", "BASIC", true },
    { "memo", "Memo", false },
    { "schedule", "Schedule", false },
};
const int KIND_COUNT = sizeof KINDS / sizeof KINDS[0];

std::string apps_directory = "apps";
std::vector<App> apps;
bool loaded = false;
bool visible = false;
bool focused = false;
char search[128] = "";
int kind_filter = 0;
int selected = -1;
bool focus_search = false;
std::string install_status;
int install_status_app = -1;
std::map<std::string, SDL_Texture *> textures;

struct JsonReader {
    const char *position;
    const char *end;

    void skip_space() {
        while (position < end && isspace((unsigned char)*position)) position++;
    }

    bool consume(char expected) {
        skip_space();
        if (position >= end || *position != expected) return false;
        position++;
        return true;
    }

    static void append_utf8(std::string &text, unsigned codepoint) {
        if (codepoint < 0x80) {
            text += (char)codepoint;
        } else if (codepoint < 0x800) {
            text += (char)(0xc0 | (codepoint >> 6));
            text += (char)(0x80 | (codepoint & 0x3f));
        } else {
            text += (char)(0xe0 | (codepoint >> 12));
            text += (char)(0x80 | ((codepoint >> 6) & 0x3f));
            text += (char)(0x80 | (codepoint & 0x3f));
        }
    }

    bool read_string(std::string &text) {
        if (!consume('"')) return false;
        text.clear();
        while (position < end && *position != '"') {
            char character = *position++;
            if (character != '\\') {
                text += character;
                continue;
            }
            if (position >= end) return false;
            char escaped = *position++;
            switch (escaped) {
                case 'n': text += '\n'; break;
                case 't': text += '\t'; break;
                case 'r': text += '\r'; break;
                case 'b': text += '\b'; break;
                case 'f': text += '\f'; break;
                case 'u': {
                    if (end - position < 4) return false;
                    unsigned codepoint = 0;
                    sscanf(std::string(position, 4).c_str(), "%x", &codepoint);
                    position += 4;
                    append_utf8(text, codepoint);
                    break;
                }
                default: text += escaped; break;
            }
        }
        return consume('"');
    }

    bool read_value(std::string &text) {
        skip_space();
        if (position < end && *position == '"') return read_string(text);
        const char *start = position;
        while (position < end && *position != ',' && *position != '}' && *position != ']' && !isspace((unsigned char)*position)) position++;
        std::string literal(start, position);
        text = literal == "null" ? "" : literal;
        return position > start;
    }

    bool read_record(Record &record) {
        if (!consume('{')) return false;
        if (consume('}')) return true;
        do {
            std::string key, value;
            if (!read_string(key) || !consume(':') || !read_value(value)) return false;
            record[key] = value;
        } while (consume(','));
        return consume('}');
    }

    bool read_records(std::vector<Record> &records) {
        if (!consume('[')) return false;
        if (consume(']')) return true;
        do {
            Record record;
            if (!read_record(record)) return false;
            records.push_back(record);
        } while (consume(','));
        return consume(']');
    }
};

std::string lowercase(std::string text) {
    for (char &character : text) character = (char)tolower((unsigned char)character);
    return text;
}

std::string read_text(const std::string &path) {
    FILE *file = fopen(path.c_str(), "rb");
    if (!file) return "";
    std::string text;
    char buffer[65536];
    size_t count;
    while ((count = fread(buffer, 1, sizeof buffer, file)) > 0) text.append(buffer, count);
    fclose(file);
    return text;
}

void load() {
    loaded = true;
    apps.clear();
    for (const Kind &kind : KINDS) {
        std::string text = read_text(apps_directory + "/" + kind.directory + "/index.json");
        if (text.empty()) continue;
        JsonReader reader = { text.data(), text.data() + text.size() };
        std::vector<Record> records;
        if (!reader.read_records(records)) {
            fprintf(stderr, "sham7x0: cannot parse %s/%s/index.json\n", apps_directory.c_str(), kind.directory);
            continue;
        }
        for (Record &record : records) {
            App app;
            app.directory = kind.directory;
            app.file = record["file"];
            app.original_file = record["original_file"];
            app.title = record["title"];
            app.data_type = record["data_type"];
            app.category = record["category"];
            app.description = record["description"];
            app.alert = record["alert"];
            app.source_url = record["source_url"];
            app.screenshot = record["screenshot"];
            app.program = kind.program;
            app.searchable = lowercase(app.title + "\n" + app.file + "\n" + app.original_file + "\n" + app.category + "\n" + app.description);
            apps.push_back(app);
        }
    }
    selected = apps.empty() ? -1 : 0;
}

bool matches(const App &app, const std::vector<std::string> &words) {
    if (kind_filter > 0 && app.directory != KINDS[kind_filter - 1].directory) return false;
    for (const std::string &word : words) {
        if (app.searchable.find(word) == std::string::npos) return false;
    }
    return true;
}

std::vector<std::string> search_words() {
    std::vector<std::string> words;
    std::string word;
    for (const char *character = search;; character++) {
        if (*character && !isspace((unsigned char)*character)) {
            word += (char)tolower((unsigned char)*character);
            continue;
        }
        if (!word.empty()) words.push_back(word);
        word.clear();
        if (!*character) break;
    }
    return words;
}

const char *list_tag(const App &app) {
    if (!app.program) return app.category.c_str();
    return app.directory == "basic" ? "BASIC" : "";
}

std::string app_path(const App &app, const std::string &file) {
    return apps_directory + "/" + app.directory + "/" + file;
}

SDL_Texture *screenshot_texture(SDL_Renderer *renderer, const App &app) {
    if (app.screenshot.empty()) return nullptr;
    std::string path = app_path(app, app.screenshot);
    auto found = textures.find(path);
    if (found != textures.end()) return found->second;
    SDL_Texture *texture = nullptr;
    if (SDL_Surface *surface = SDL_LoadPNG(path.c_str())) {
        texture = SDL_CreateTextureFromSurface(renderer, surface);
        if (texture) SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
        SDL_DestroySurface(surface);
    }
    textures[path] = texture;
    return texture;
}

void install(int index) {
    const App &app = apps[index];
    install_status_app = index;
    bool accepted = runtime_install_wzd(app_path(app, app.file).c_str());
    if (!accepted) install_status = "Failed, see console";
    else install_status = app.program ? "Installed" : "Transfer finished, see console";
}

void draw_details(SDL_Renderer *renderer, int index) {
    const App &app = apps[index];
    float width = ImGui::GetContentRegionAvail().x;
    if (SDL_Texture *texture = screenshot_texture(renderer, app)) {
        float texture_width = 0, texture_height = 0;
        SDL_GetTextureSize(texture, &texture_width, &texture_height);
        float image_width = std::min(width, texture_width);
        ImGui::Image((ImTextureID)(intptr_t)texture, ImVec2(image_width, image_width * texture_height / texture_width));
    } else {
        ImGui::TextDisabled("No screenshot");
    }
    ImGui::Spacing();
    ImGui::TextUnformatted(app.title.c_str());
    ImGui::TextDisabled("%s  |  %s  |  %s", app.data_type.c_str(), app.category.c_str(), app.original_file.c_str());
    ImGui::Spacing();
    if (ImGui::Button("Install")) install(index);
    ImGui::SameLine();
    float fraction = 0;
    const char *description = nullptr;
    int waiting = 0;
    if (runtime_transfer_progress(&fraction, &description, &waiting)) {
        char label[160];
        snprintf(label, sizeof label, "sending %s  %.0f%%%s", description, fraction * 100, waiting ? "  (more waiting)" : "");
        ImGui::ProgressBar(fraction, ImVec2(-1, 0), label);
    } else if (install_status_app == index) {
        ImGui::TextDisabled("%s", install_status.c_str());
    }
    ImGui::Separator();
    if (!app.alert.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.75f, 0.35f, 1.0f));
        ImGui::TextWrapped("%s", app.alert.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::TextWrapped("%s", app.description.c_str());
    if (!app.source_url.empty()) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", app.source_url.c_str());
        ImGui::PopStyleColor();
    }
}

}

void browser_set_directory(const char *apps) {
    apps_directory = apps;
    loaded = false;
}

void browser_toggle(void) {
    visible = !visible;
    if (visible) focus_search = true;
}

bool browser_visible(void) {
    return visible;
}

bool browser_focused(void) {
    return visible && focused;
}

void browser_draw(SDL_Renderer *renderer) {
    focused = false;
    if (!visible) return;
    if (!loaded) load();
    ImGui::SetNextWindowSize(ImVec2(900, 560), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin("Apps", &visible)) {
        ImGui::End();
        return;
    }
    focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    std::vector<std::string> words = search_words();
    std::vector<int> shown;
    for (int i = 0; i < (int)apps.size(); i++) {
        if (matches(apps[i], words)) shown.push_back(i);
    }

    if (focus_search) {
        ImGui::SetKeyboardFocusHere();
        focus_search = false;
    }
    ImGui::SetNextItemWidth(-260);
    ImGui::InputTextWithHint("##search", "Search titles, descriptions, categories", search, sizeof search);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    const char *kind_labels[KIND_COUNT + 1] = { "All" };
    for (int i = 0; i < KIND_COUNT; i++) kind_labels[i + 1] = KINDS[i].label;
    ImGui::Combo("##kind", &kind_filter, kind_labels, KIND_COUNT + 1);
    ImGui::SameLine();
    ImGui::TextDisabled("%zu of %zu", shown.size(), apps.size());

    int selected_row = (int)(std::find(shown.begin(), shown.end(), selected) - shown.begin());
    if (selected_row == (int)shown.size()) {
        selected_row = 0;
        selected = shown.empty() ? -1 : shown[0];
    }
    bool scroll_to_selected = false;
    if (focused && !shown.empty()) {
        int page = 10;
        int target = selected_row;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) target--;
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) target++;
        if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) target -= page;
        if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) target += page;
        target = std::max(0, std::min((int)shown.size() - 1, target));
        if (target != selected_row) {
            selected_row = target;
            selected = shown[target];
            scroll_to_selected = true;
        }
        bool enter = ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
        if (enter) install(selected);
    }

    if (apps.empty()) {
        ImGui::TextWrapped("No apps found. Expected %s/{programs,basic,memo,schedule}/index.json.", apps_directory.c_str());
        ImGui::End();
        return;
    }

    ImGui::BeginChild("list", ImVec2(300, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
    ImGuiListClipper clipper;
    clipper.Begin((int)shown.size());
    if (scroll_to_selected) clipper.IncludeItemByIndex(selected_row);
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
            int index = shown[row];
            const App &app = apps[index];
            ImGui::PushID(index);
            if (ImGui::Selectable(app.title.c_str(), selected == index, ImGuiSelectableFlags_AllowDoubleClick)) {
                selected = index;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) install(index);
            }
            if (scroll_to_selected && row == selected_row) {
                float top = ImGui::GetWindowPos().y;
                float bottom = top + ImGui::GetWindowHeight();
                if (ImGui::GetItemRectMin().y < top) ImGui::SetScrollHereY(0.0f);
                else if (ImGui::GetItemRectMax().y > bottom) ImGui::SetScrollHereY(1.0f);
            }
            const char *tag = list_tag(app);
            if (*tag) {
                ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(tag).x);
                ImGui::TextDisabled("%s", tag);
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("details");
    if (selected >= 0 && selected < (int)apps.size()) draw_details(renderer, selected);
    ImGui::EndChild();
    ImGui::End();
}

void browser_shutdown(void) {
    for (auto &entry : textures) {
        if (entry.second) SDL_DestroyTexture(entry.second);
    }
    textures.clear();
}
