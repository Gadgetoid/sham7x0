#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include "imgui.h"
#include "console.h"

extern "C" bool mp_repl_continue_with_input(const char *input);

namespace {

enum LineKind { OUTPUT, ECHO, NOTICE };

struct Line {
    std::string text;
    LineKind kind;
};

const size_t MAX_LINES = 5000;

std::vector<Line> lines;
bool line_open = false;
std::deque<std::string> pending;
std::string block;
std::vector<std::string> history;
int history_pos = -1;
char input[1024] = "";
bool focus_requested = false;
bool scroll_requested = false;

void trim() {
    if (lines.size() > MAX_LINES) {
        lines.erase(lines.begin(), lines.begin() + (lines.size() - MAX_LINES));
    }
}

void add_line(const std::string &text, LineKind kind) {
    lines.push_back({ text, kind });
    line_open = false;
    scroll_requested = true;
    trim();
}

const char *prompt() {
    return block.empty() ? ">>> " : "... ";
}

int input_callback(ImGuiInputTextCallbackData *data) {
    if (data->EventFlag != ImGuiInputTextFlags_CallbackHistory || history.empty()) return 0;
    int previous = history_pos;
    if (data->EventKey == ImGuiKey_UpArrow) {
        if (history_pos == -1) history_pos = (int)history.size() - 1;
        else if (history_pos > 0) history_pos--;
    } else if (data->EventKey == ImGuiKey_DownArrow) {
        if (history_pos != -1 && ++history_pos >= (int)history.size()) history_pos = -1;
    }
    if (previous != history_pos) {
        data->DeleteChars(0, data->BufTextLen);
        if (history_pos >= 0) data->InsertChars(0, history[history_pos].c_str());
    }
    return 0;
}

ImVec4 colour_for(LineKind kind) {
    switch (kind) {
        case ECHO:   return ImVec4(0.45f, 0.85f, 0.78f, 1.0f);
        case NOTICE: return ImVec4(0.95f, 0.75f, 0.35f, 1.0f);
        default:     return ImVec4(0.86f, 0.88f, 0.86f, 1.0f);
    }
}

}

extern "C" void console_write(const char *text, size_t len) {
    for (size_t i = 0; i < len; i++) {
        char c = text[i];
        if (c == '\r') continue;
        if (c == '\n') {
            if (!line_open) lines.push_back({ "", OUTPUT });
            line_open = false;
            continue;
        }
        if (!line_open) {
            lines.push_back({ "", OUTPUT });
            line_open = true;
        }
        lines.back().text.push_back(c);
    }
    scroll_requested = true;
    trim();
}

extern "C" void console_notice(const char *message) {
    add_line(std::string("-- ") + message, NOTICE);
    printf("-- %s\n", message);
}

extern "C" void console_submit(const char *line) {
    add_line(std::string(prompt()) + line, ECHO);
    if (line[0]) {
        if (history.empty() || history.back() != line) history.push_back(line);
    }
    history_pos = -1;
    if (!block.empty()) block += "\n";
    block += line;
    if (mp_repl_continue_with_input(block.c_str())) return;
    bool blank = block.find_first_not_of(" \t\n") == std::string::npos;
    if (!blank) pending.push_back(block);
    block.clear();
}

extern "C" char *console_take_input(void) {
    if (pending.empty()) return nullptr;
    char *source = strdup(pending.front().c_str());
    pending.pop_front();
    return source;
}

void console_focus(void) {
    focus_requested = true;
}

void console_cancel(void) {
    add_line(std::string(prompt()) + input + "^C", ECHO);
    input[0] = '\0';
    block.clear();
}

void console_draw(void) {
    float footer = ImGui::GetFrameHeightWithSpacing();
    ImGui::BeginChild("scrollback", ImVec2(0, -footer), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    ImGuiListClipper clipper;
    clipper.Begin((int)lines.size());
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
            ImGui::PushStyleColor(ImGuiCol_Text, colour_for(lines[i].kind));
            ImGui::TextUnformatted(lines[i].text.c_str());
            ImGui::PopStyleColor();
        }
    }
    if (scroll_requested && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - ImGui::GetTextLineHeight() * 2) {
        ImGui::SetScrollHereY(1.0f);
    }
    scroll_requested = false;
    ImGui::EndChild();

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(prompt());
    ImGui::SameLine(0, 0);
    ImGui::SetNextItemWidth(-1);
    if (focus_requested) {
        ImGui::SetKeyboardFocusHere();
        focus_requested = false;
    }
    ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_EscapeClearsAll |
                                ImGuiInputTextFlags_CallbackHistory;
    if (ImGui::InputText("##repl", input, sizeof input, flags, input_callback)) {
        console_submit(input);
        input[0] = '\0';
        ImGui::SetKeyboardFocusHere(-1);
    }
}
