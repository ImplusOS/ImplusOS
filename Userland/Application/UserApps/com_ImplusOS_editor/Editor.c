#include "../../../API/File.h"
#include "../../../API/Graphics.h"
#include "../../../API/Input.h"
#include "../../../API/Process.h"
#include "../../../API/Window.h"
#include "../../../../libc/I_libc/include/stdio.h"
#include "../../../../libc/I_libc/include/stdlib.h"
#include "../../../../libc/I_libc/include/string.h"

#define IED_MAX_PATH 512
#define IED_INITIAL_LINE_CAPACITY 32u

typedef struct {
    char *text;
    size_t length;
    size_t capacity;
} editor_line_t;

typedef struct {
    editor_line_t *lines;
    size_t count;
    size_t capacity;
} editor_document_t;

static window_id_t g_win;
static int g_win_w = 640;
static int g_win_h = 400;
static int g_char_w = 8;
static int g_char_h = 16;
static editor_document_t g_document;
static editor_document_t g_undo;
static int g_undo_valid;
static size_t g_cursor_row;
static size_t g_cursor_col;
static size_t g_scroll_top;
static int g_visible_rows;
static size_t g_previous_cursor_row;
static size_t g_previous_scroll_top;
static size_t g_dirty_first;
static size_t g_dirty_last;
static int g_full_redraw = 1;
static char g_filepath[IED_MAX_PATH];
static int g_modified;
static char g_status[128] =
    "Ctrl+S save  Ctrl+L reload  Ctrl+Z undo  Ctrl+Q quit";

static void editor_set_status(const char *status)
{
    snprintf(g_status, sizeof(g_status), "%s", status ? status : "");
}

static void line_destroy(editor_line_t *line)
{
    if (!line) return;
    free(line->text);
    memset(line, 0, sizeof(*line));
}

static int line_reserve(editor_line_t *line, size_t required)
{
    if (required <= line->capacity) return 0;
    size_t capacity = line->capacity ? line->capacity : 32u;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2u) return -1;
        capacity *= 2u;
    }
    char *replacement = (char *)malloc(capacity);
    if (!replacement) return -1;
    if (line->text && line->length != 0u)
        memcpy(replacement, line->text, line->length);
    replacement[line->length] = '\0';
    free(line->text);
    line->text = replacement;
    line->capacity = capacity;
    return 0;
}

static int line_set(editor_line_t *line, const char *text, size_t length)
{
    if (line_reserve(line, length + 1u) < 0) return -1;
    if (length != 0u) memcpy(line->text, text, length);
    line->text[length] = '\0';
    line->length = length;
    return 0;
}

static void document_destroy(editor_document_t *document)
{
    if (!document) return;
    for (size_t i = 0; i < document->count; ++i)
        line_destroy(&document->lines[i]);
    free(document->lines);
    memset(document, 0, sizeof(*document));
}

static int document_reserve(editor_document_t *document, size_t required)
{
    if (required <= document->capacity) return 0;
    size_t capacity = document->capacity ?
        document->capacity : IED_INITIAL_LINE_CAPACITY;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2u) return -1;
        capacity *= 2u;
    }
    editor_line_t *replacement =
        (editor_line_t *)calloc(capacity, sizeof(*replacement));
    if (!replacement) return -1;
    if (document->count != 0u)
        memcpy(replacement, document->lines,
               document->count * sizeof(*replacement));
    free(document->lines);
    document->lines = replacement;
    document->capacity = capacity;
    return 0;
}

static int document_insert_line(editor_document_t *document, size_t index,
                                const char *text, size_t length)
{
    if (index > document->count ||
        document_reserve(document, document->count + 1u) < 0) {
        return -1;
    }
    if (index < document->count) {
        memmove(&document->lines[index + 1u], &document->lines[index],
                (document->count - index) * sizeof(document->lines[0]));
    }
    memset(&document->lines[index], 0, sizeof(document->lines[index]));
    if (line_set(&document->lines[index], text ? text : "", length) < 0) {
        if (index < document->count) {
            memmove(&document->lines[index], &document->lines[index + 1u],
                    (document->count - index) * sizeof(document->lines[0]));
        }
        return -1;
    }
    ++document->count;
    return 0;
}

static void document_remove_line(editor_document_t *document, size_t index)
{
    if (!document || index >= document->count) return;
    line_destroy(&document->lines[index]);
    if (index + 1u < document->count) {
        memmove(&document->lines[index], &document->lines[index + 1u],
                (document->count - index - 1u) *
                    sizeof(document->lines[0]));
    }
    --document->count;
    memset(&document->lines[document->count], 0,
           sizeof(document->lines[0]));
}

static int document_clone(editor_document_t *destination,
                          const editor_document_t *source)
{
    editor_document_t copy = {0};
    if (document_reserve(&copy, source->count) < 0) return -1;
    for (size_t i = 0; i < source->count; ++i) {
        if (document_insert_line(&copy, copy.count, source->lines[i].text,
                                 source->lines[i].length) < 0) {
            document_destroy(&copy);
            return -1;
        }
    }
    document_destroy(destination);
    *destination = copy;
    return 0;
}

static int document_reset(editor_document_t *document)
{
    document_destroy(document);
    return document_insert_line(document, 0u, "", 0u);
}

static void editor_mark_dirty(size_t first, size_t last)
{
    if (first > last) {
        size_t temporary = first;
        first = last;
        last = temporary;
    }
    if (g_dirty_first == SIZE_MAX || first < g_dirty_first)
        g_dirty_first = first;
    if (last > g_dirty_last) g_dirty_last = last;
}

static void editor_capture_undo(void)
{
    if (document_clone(&g_undo, &g_document) == 0) {
        g_undo_valid = 1;
    } else {
        g_undo_valid = 0;
        editor_set_status("Unable to allocate undo snapshot.");
    }
}

static void editor_undo(void)
{
    if (!g_undo_valid) {
        editor_set_status("Nothing to undo.");
        return;
    }
    editor_document_t current = g_document;
    g_document = g_undo;
    memset(&g_undo, 0, sizeof(g_undo));
    document_destroy(&current);
    g_undo_valid = 0;
    if (g_cursor_row >= g_document.count)
        g_cursor_row = g_document.count - 1u;
    if (g_cursor_col > g_document.lines[g_cursor_row].length)
        g_cursor_col = g_document.lines[g_cursor_row].length;
    g_modified = 1;
    g_full_redraw = 1;
    editor_set_status("Undo complete.");
}

static void editor_draw_status(void)
{
    uint32_t y = (uint32_t)(g_win_h - g_char_h - 4);
    draw_fill_rect(0, y, (uint32_t)g_win_w,
                   (uint32_t)(g_char_h + 4), 0xFF313244);
    char location[128];
    snprintf(location, sizeof(location), "%s%s L%zu:%zu",
             g_filepath[0] ? g_filepath : "[new]",
             g_modified ? "*" : "",
             g_cursor_row + 1u, g_cursor_col + 1u);
    window_draw_text(g_win, 4, y + 2u, location, 0xFFBAC2DE, 14.0f);
    window_draw_text(g_win, 250, y + 2u, g_status, 0xFF89B4FA, 12.0f);
}

static void editor_draw_row(int visible_row)
{
    size_t line_index = g_scroll_top + (size_t)visible_row;
    uint32_t y = (uint32_t)(visible_row * g_char_h);
    uint32_t content_height = (uint32_t)(g_win_h - g_char_h - 4);
    if (y >= content_height) return;

    draw_fill_rect(0, y, (uint32_t)g_win_w, (uint32_t)g_char_h,
                   0xFF1E1E2E);
    draw_fill_rect(0, y, 40u, (uint32_t)g_char_h, 0xFF181825);
    draw_fill_rect(40u, y, 1u, (uint32_t)g_char_h, 0xFF313244);
    if (line_index >= g_document.count) return;

    char number[16];
    snprintf(number, sizeof(number), "%4zu", line_index + 1u);
    window_draw_text(g_win, 2u, y + 2u, number, 0xFF6C7086, 14.0f);
    const editor_line_t *line = &g_document.lines[line_index];
    if (line->length != 0u) {
        window_draw_text(g_win, 40u, y + 2u, line->text,
                         0xFFCDD6F4, 14.0f);
    }
    if (line_index == g_cursor_row) {
        uint32_t cursor_x = 40u + (uint32_t)g_cursor_col *
                            (uint32_t)g_char_w;
        if (cursor_x < (uint32_t)g_win_w) {
            draw_fill_rect(cursor_x, y + 2u, 2u,
                           (uint32_t)g_char_h, 0xFFF5C2E7);
        }
    }
}

static void editor_render(void)
{
    if (g_scroll_top != g_previous_scroll_top) g_full_redraw = 1;
    if (g_full_redraw) {
        window_clear(g_win);
        draw_fill_rect(0, 0, (uint32_t)g_win_w, (uint32_t)g_win_h,
                       0xFF1E1E2E);
        g_dirty_first = 0u;
        g_dirty_last = g_document.count + (size_t)g_visible_rows;
    } else {
        editor_mark_dirty(g_previous_cursor_row, g_cursor_row);
    }

    for (int row = 0; row < g_visible_rows; ++row) {
        size_t line_index = g_scroll_top + (size_t)row;
        if (g_full_redraw ||
            (g_dirty_first != SIZE_MAX &&
             line_index >= g_dirty_first && line_index <= g_dirty_last)) {
            editor_draw_row(row);
        }
    }
    editor_draw_status();
    g_previous_cursor_row = g_cursor_row;
    g_previous_scroll_top = g_scroll_top;
    g_dirty_first = SIZE_MAX;
    g_dirty_last = 0u;
    g_full_redraw = 0;
}

static void editor_load(const char *path)
{
    int32_t fd = file_open(path, 0);
    if (fd < 0) {
        editor_set_status("Could not open file.");
        if (g_document.count == 0u) (void)document_reset(&g_document);
        return;
    }

    editor_document_t loaded = {0};
    if (document_insert_line(&loaded, 0u, "", 0u) < 0) {
        file_close(fd);
        editor_set_status("Out of memory.");
        return;
    }
    char buffer[4096];
    int64_t count;
    while ((count = file_read(fd, buffer, sizeof(buffer))) > 0) {
        for (int64_t i = 0; i < count; ++i) {
            if (buffer[i] == '\r') continue;
            if (buffer[i] == '\n') {
                if (document_insert_line(&loaded, loaded.count, "", 0u) < 0)
                    goto load_failed;
                continue;
            }
            editor_line_t *line = &loaded.lines[loaded.count - 1u];
            if (line_reserve(line, line->length + 2u) < 0)
                goto load_failed;
            line->text[line->length++] = buffer[i];
            line->text[line->length] = '\0';
        }
    }
    file_close(fd);
    document_destroy(&g_document);
    g_document = loaded;
    document_destroy(&g_undo);
    g_undo_valid = 0;
    snprintf(g_filepath, sizeof(g_filepath), "%s", path);
    g_cursor_row = 0u;
    g_cursor_col = 0u;
    g_scroll_top = 0u;
    g_modified = 0;
    g_full_redraw = 1;
    editor_set_status("Loaded file.");
    return;

load_failed:
    file_close(fd);
    document_destroy(&loaded);
    editor_set_status("File is too large for available memory.");
}

static void editor_save(void)
{
    if (g_filepath[0] == '\0')
        snprintf(g_filepath, sizeof(g_filepath), "/Userland/editor.txt");
    int32_t fd = file_creat(g_filepath);
    if (fd < 0) {
        editor_set_status("Save failed.");
        return;
    }

    int failed = 0;
    for (size_t i = 0; i < g_document.count; ++i) {
        editor_line_t *line = &g_document.lines[i];
        if (line->length != 0u &&
            file_write(fd, line->text, line->length) !=
                (int64_t)line->length) {
            failed = 1;
            break;
        }
        if (i + 1u < g_document.count &&
            file_write(fd, "\n", 1u) != 1) {
            failed = 1;
            break;
        }
    }
    file_close(fd);
    if (failed) {
        editor_set_status("Save failed.");
        return;
    }
    g_modified = 0;
    editor_set_status("Saved.");
}

static int editor_insert_char(char character)
{
    editor_line_t *line = &g_document.lines[g_cursor_row];
    if (line_reserve(line, line->length + 2u) < 0) return -1;
    memmove(&line->text[g_cursor_col + 1u], &line->text[g_cursor_col],
            line->length - g_cursor_col + 1u);
    line->text[g_cursor_col++] = character;
    ++line->length;
    editor_mark_dirty(g_cursor_row, g_cursor_row);
    g_modified = 1;
    return 0;
}

static int editor_newline(void)
{
    editor_line_t *line = &g_document.lines[g_cursor_row];
    size_t tail_length = line->length - g_cursor_col;
    if (document_insert_line(&g_document, g_cursor_row + 1u,
                             &line->text[g_cursor_col], tail_length) < 0) {
        return -1;
    }
    line = &g_document.lines[g_cursor_row];
    line->length = g_cursor_col;
    line->text[line->length] = '\0';
    ++g_cursor_row;
    g_cursor_col = 0u;
    editor_mark_dirty(g_cursor_row - 1u, g_document.count);
    g_modified = 1;
    return 0;
}

static int editor_backspace(void)
{
    editor_line_t *line = &g_document.lines[g_cursor_row];
    if (g_cursor_col > 0u) {
        memmove(&line->text[g_cursor_col - 1u],
                &line->text[g_cursor_col],
                line->length - g_cursor_col + 1u);
        --line->length;
        --g_cursor_col;
        editor_mark_dirty(g_cursor_row, g_cursor_row);
        g_modified = 1;
        return 0;
    }
    if (g_cursor_row == 0u) return 0;

    editor_line_t *previous = &g_document.lines[g_cursor_row - 1u];
    size_t previous_length = previous->length;
    if (line_reserve(previous, previous->length + line->length + 1u) < 0)
        return -1;
    memcpy(previous->text + previous->length, line->text, line->length + 1u);
    previous->length += line->length;
    document_remove_line(&g_document, g_cursor_row);
    --g_cursor_row;
    g_cursor_col = previous_length;
    editor_mark_dirty(g_cursor_row, g_document.count);
    g_modified = 1;
    return 0;
}

static void editor_ensure_visible(void)
{
    if (g_cursor_row < g_scroll_top) g_scroll_top = g_cursor_row;
    if (g_cursor_row >= g_scroll_top + (size_t)g_visible_rows)
        g_scroll_top = g_cursor_row - (size_t)g_visible_rows + 1u;
}

void _start(void)
{
    g_win = window_create_ex(100, 80, 640, 400, 0xFF1E1E2E,
                             "ImplusOS Editor");
    if (g_win == 0) {
        for (;;) process_yield();
    }
    window_subscribe_keyboard(g_win);
    graphics_init(g_win);

    uint32_t wx, wy, width, height;
    if (window_get_rect(g_win, &wx, &wy, &width, &height) == 0) {
        g_win_w = (int)width;
        g_win_h = (int)height;
    }
    g_visible_rows = (g_win_h - g_char_h - 4) / g_char_h;
    g_dirty_first = SIZE_MAX;
    (void)document_reset(&g_document);
    char launch_path[512];
    int32_t launch_length =
        process_get_launch_argument(launch_path, (uint32_t)sizeof(launch_path));
    editor_load(launch_length > 0 ? launch_path : "/Userland/editor.txt");
    editor_render();

    for (;;) {
        input_keyboard_event_t keyboard;
        if (window_input_keyboard_poll(&keyboard) <= 0) {
            process_yield();
            continue;
        }
        if (!keyboard.pressed) continue;

        char character = (char)keyboard.ascii;
        uint8_t modifiers = keyboard.modifiers;
        if ((modifiers & INPUT_KBD_MOD_CTRL) &&
            (character == 's' || character == 'S' ||
             keyboard.keycode == 0x1Fu)) {
            editor_save();
            editor_render();
            continue;
        }
        if ((modifiers & INPUT_KBD_MOD_CTRL) &&
            (character == 'l' || character == 'L' ||
             keyboard.keycode == 0x26u)) {
            editor_load(g_filepath[0] ? g_filepath :
                        "/Userland/editor.txt");
            editor_render();
            continue;
        }
        if ((modifiers & INPUT_KBD_MOD_CTRL) &&
            (character == 'z' || character == 'Z' ||
             keyboard.keycode == 0x2Cu)) {
            editor_undo();
            editor_ensure_visible();
            editor_render();
            continue;
        }
        if ((modifiers & INPUT_KBD_MOD_CTRL) &&
            (character == 'q' || character == 'Q' ||
             keyboard.keycode == 0x10u)) {
            process_exit(0);
        }

        int edited = 0;
        if (character == '\n' || keyboard.keycode == 0x1Cu) {
            editor_capture_undo();
            edited = editor_newline() == 0;
        } else if (character == '\b' || keyboard.keycode == 0x0Eu) {
            editor_capture_undo();
            edited = editor_backspace() == 0;
        } else if (keyboard.keycode == 0x48u) {
            if (g_cursor_row > 0u) {
                --g_cursor_row;
                if (g_cursor_col >
                    g_document.lines[g_cursor_row].length) {
                    g_cursor_col = g_document.lines[g_cursor_row].length;
                }
            }
        } else if (keyboard.keycode == 0x50u) {
            if (g_cursor_row + 1u < g_document.count) {
                ++g_cursor_row;
                if (g_cursor_col >
                    g_document.lines[g_cursor_row].length) {
                    g_cursor_col = g_document.lines[g_cursor_row].length;
                }
            }
        } else if (keyboard.keycode == 0x4Bu) {
            if (g_cursor_col > 0u) --g_cursor_col;
        } else if (keyboard.keycode == 0x4Du) {
            if (g_cursor_col <
                g_document.lines[g_cursor_row].length) {
                ++g_cursor_col;
            }
        } else if (character == '\t') {
            editor_capture_undo();
            edited = 1;
            for (int i = 0; i < 4; ++i) {
                if (editor_insert_char(' ') < 0) {
                    edited = 0;
                    break;
                }
            }
        } else if (character >= 0x20 && character <= 0x7E) {
            editor_capture_undo();
            edited = editor_insert_char(character) == 0;
        }
        if (!edited &&
            (character == '\n' || character == '\b' ||
             character == '\t' ||
             (character >= 0x20 && character <= 0x7E))) {
            editor_set_status("Edit failed: out of memory.");
        }
        editor_ensure_visible();
        editor_render();
    }
}
