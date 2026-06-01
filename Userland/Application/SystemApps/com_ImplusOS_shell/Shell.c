#include "../../../API/File.h"
#include "../../../API/Serial.h"
#include "../../../API/Process.h"
#include "../../../API/Graphics.h"
#include "../../../API/Window.h"
#include "../../../API/Input.h"
#include "../../../../libc/include/string.h"
#include "../../../../libc/include/stdlib.h"
#include "../../../../libc/include/ctype.h"

#define ISH_MAX_CMD_LEN     512
#define ISH_MAX_ARGS        64
#define ISH_MAX_PATH        512
#define ISH_HISTORY_SIZE    32

static char g_cmd_buf[ISH_MAX_CMD_LEN];
static int  g_cmd_len = 0;
static char g_cwd[ISH_MAX_PATH] = "/";
static char g_history[ISH_HISTORY_SIZE][ISH_MAX_CMD_LEN];
static int  g_history_count = 0;
static int  g_history_pos = 0;
static window_id_t g_win = 0;
static int  g_cursor_x = 0;
static int  g_cursor_y = 0;
static int  g_win_w = 0;
static int  g_win_h = 0;
static int  g_char_w = 8;
static int  g_char_h = 16;
static int  g_cols = 0;
static int  g_rows = 0;

static void shell_scroll_up(void)
{
    g_cursor_y = g_win_h - g_char_h - 4;
    draw_fill_rect(0, 0, (uint32_t)g_win_w, (uint32_t)g_win_h, 0xFF1E1E2E);
    draw_present();
    g_cursor_x = 0;
    g_cursor_y = 4;
}

static void shell_putchar(char c)
{
    if (c == '\n') {
        g_cursor_x = 0;
        g_cursor_y += g_char_h;
        if (g_cursor_y + g_char_h > g_win_h) {
            shell_scroll_up();
        }
        return;
    }
    if (c == '\r') {
        g_cursor_x = 0;
        return;
    }
    if (c == '\b') {
        if (g_cursor_x >= g_char_w) {
            g_cursor_x -= g_char_w;
            draw_fill_rect((uint32_t)g_cursor_x, (uint32_t)g_cursor_y,
                           (uint32_t)g_char_w, (uint32_t)g_char_h, 0xFF1E1E2E);
        }
        return;
    }
    if (c == '\t') {
        int spaces = 4 - ((g_cursor_x / g_char_w) % 4);
        for (int i = 0; i < spaces; i++) shell_putchar(' ');
        return;
    }
    if (g_win != 0) {
        char tmp[2] = {c, 0};
        window_draw_text(g_win, (uint32_t)g_cursor_x, (uint32_t)g_cursor_y,
                         tmp, 0xFFCDD6F4, 14.0f);
    }
    g_cursor_x += g_char_w;
    if (g_cursor_x + g_char_w > g_win_w) {
        g_cursor_x = 0;
        g_cursor_y += g_char_h;
        if (g_cursor_y + g_char_h > g_win_h) {
            shell_scroll_up();
        }
    }
}

static void shell_puts(const char *s)
{
    if (!s) return;
    while (*s) {
        shell_putchar(*s++);
    }
}

static void shell_print_number(int32_t n)
{
    char buf[16];
    int i = 0;
    int neg = 0;
    if (n < 0) { neg = 1; n = -n; }
    if (n == 0) { buf[i++] = '0'; }
    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    if (neg) buf[i++] = '-';
    for (int j = i - 1; j >= 0; j--) {
        shell_putchar(buf[j]);
    }
}

static void shell_print_u32(uint32_t n)
{
    char buf[16];
    int i = 0;
    if (n == 0) { buf[i++] = '0'; }
    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    for (int j = i - 1; j >= 0; j--) {
        shell_putchar(buf[j]);
    }
}

static void shell_prompt(void)
{
    if (g_win != 0) {
        window_draw_text(g_win, (uint32_t)g_cursor_x, (uint32_t)g_cursor_y,
                         "ish", 0xFF89B4FA, 14.0f);
        g_cursor_x += 3 * g_char_w;

        window_draw_text(g_win, (uint32_t)g_cursor_x, (uint32_t)g_cursor_y,
                         ":", 0xFFCDD6F4, 14.0f);
        g_cursor_x += g_char_w;

        window_draw_text(g_win, (uint32_t)g_cursor_x, (uint32_t)g_cursor_y,
                         g_cwd, 0xFFA6E3A1, 14.0f);
        g_cursor_x += (int)strlen(g_cwd) * g_char_w;

        window_draw_text(g_win, (uint32_t)g_cursor_x, (uint32_t)g_cursor_y,
                         "$ ", 0xFFCDD6F4, 14.0f);
        g_cursor_x += 2 * g_char_w;
    } else {
        shell_puts("ish:");
        shell_puts(g_cwd);
        shell_puts("$ ");
    }
    draw_present();
}

static void history_add(const char *cmd)
{
    if (cmd[0] == '\0') return;
    if (g_history_count > 0 && strcmp(g_history[(g_history_count - 1) % ISH_HISTORY_SIZE], cmd) == 0) return;
    strcpy(g_history[g_history_count % ISH_HISTORY_SIZE], cmd);
    g_history_count++;
    g_history_pos = g_history_count;
}

static int parse_args(char *cmd, char *argv[ISH_MAX_ARGS])
{
    int argc = 0;
    char *p = cmd;
    while (*p && argc < ISH_MAX_ARGS - 1) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') break;
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) *p++ = '\0';
    }
    argv[argc] = (char*)0;
    return argc;
}

static void append_path_component(char *out, int out_size, const char *component, int len)
{
    if (!out || out_size <= 0 || !component || len <= 0) return;
    if (len == 1 && component[0] == '.') return;
    if (len == 2 && component[0] == '.' && component[1] == '.') {
        int cur_len = (int)strlen(out);
        if (cur_len <= 1) {
            strcpy(out, "/");
            return;
        }
        if (out[cur_len - 1] == '/') out[--cur_len] = '\0';
        char *last = out;
        for (char *p = out + 1; *p; ++p) {
            if (*p == '/') last = p;
        }
        if (last == out) out[1] = '\0';
        else *last = '\0';
        return;
    }

    int cur_len = (int)strlen(out);
    if (cur_len == 0) {
        strncpy(out, "/", (size_t)out_size);
        cur_len = 1;
    }
    if (cur_len > 1 && out[cur_len - 1] != '/') {
        strncat(out, "/", (size_t)(out_size - cur_len - 1));
        cur_len = (int)strlen(out);
    }
    if (cur_len == 1 && out[0] == '/') {
        cur_len = 1;
    }
    int copy_len = len;
    int room = out_size - cur_len - 1;
    if (copy_len > room) copy_len = room;
    if (copy_len > 0) {
        memcpy(out + cur_len, component, (size_t)copy_len);
        out[cur_len + copy_len] = '\0';
    }
}

static void resolve_path(const char *path, char *out, int out_size)
{
    if (!out || out_size <= 0) return;
    out[0] = '\0';
    const char *p = path ? path : "";
    if (*p == '/') {
        strcpy(out, "/");
        while (*p == '/') p++;
    } else {
        strncpy(out, g_cwd, (size_t)(out_size - 1));
        out[out_size - 1] = '\0';
    }

    while (*p) {
        while (*p == '/') p++;
        const char *start = p;
        while (*p && *p != '/') p++;
        append_path_component(out, out_size, start, (int)(p - start));
    }

    if (out[0] == '\0') {
        strcpy(out, "/");
    }
}

static int cmd_echo(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (i > 1) shell_putchar(' ');
        shell_puts(argv[i]);
    }
    shell_putchar('\n');
    return 0;
}

static int cmd_pwd(int argc, char **argv)
{
    (void)argc; (void)argv;
    shell_puts(g_cwd);
    shell_putchar('\n');
    return 0;
}

static int cmd_cd(int argc, char **argv)
{
    if (argc < 2) {
        strcpy(g_cwd, "/");
        return 0;
    }
    char resolved[ISH_MAX_PATH];
    resolve_path(argv[1], resolved, ISH_MAX_PATH);

    file_stat_t st;
    if (file_stat(resolved, &st) == 0 && st.is_dir) {
        strncpy(g_cwd, resolved, ISH_MAX_PATH - 1);
        g_cwd[ISH_MAX_PATH - 1] = '\0';
    } else {
        shell_puts("cd: ");
        shell_puts(argv[1]);
        shell_puts(": No such directory\n");
        return 1;
    }
    return 0;
}

static int cmd_ls(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : g_cwd;
    char resolved[ISH_MAX_PATH];
    resolve_path(path, resolved, ISH_MAX_PATH);

    int32_t dh = file_opendir(resolved);
    if (dh < 0) {
        shell_puts("ls: cannot access '");
        shell_puts(path);
        shell_puts("'\n");
        return 1;
    }
    file_dirent_t de;
    int count = 0;
    while (file_readdir(dh, &de) == 0) {
        if (de.name[0] == '\0') continue;
        int is_dir = (de.attributes & 0x10) != 0;
        if (is_dir) {
            if (g_win != 0) {
                window_draw_text(g_win, (uint32_t)g_cursor_x, (uint32_t)g_cursor_y,
                                 de.name, 0xFF89B4FA, 14.0f);
                g_cursor_x += (int)strlen(de.name) * g_char_w;
                window_draw_text(g_win, (uint32_t)g_cursor_x, (uint32_t)g_cursor_y,
                                 "/", 0xFF89B4FA, 14.0f);
                g_cursor_x += g_char_w;
            } else {
                shell_puts(de.name);
                shell_putchar('/');
            }
        } else {
            shell_puts(de.name);
        }
        if (!is_dir) {
            shell_puts("  ");
            shell_print_u32(de.size);
            shell_puts("B");
        }
        shell_putchar('\n');
        count++;
    }
    file_closedir(dh);
    if (count == 0) {
        shell_puts("(empty)\n");
    }
    return 0;
}

static int cmd_cat(int argc, char **argv)
{
    if (argc < 2) {
        shell_puts("cat: missing operand\n");
        return 1;
    }
    char resolved[ISH_MAX_PATH];
    resolve_path(argv[1], resolved, ISH_MAX_PATH);
    int32_t fd = file_open(resolved, 0);
    if (fd < 0) {
        shell_puts("cat: ");
        shell_puts(argv[1]);
        shell_puts(": No such file\n");
        return 1;
    }
    char buf[256];
    int64_t n;
    while ((n = file_read(fd, buf, 255)) > 0) {
        buf[n] = '\0';
        shell_puts(buf);
    }
    file_close(fd);
    shell_putchar('\n');
    return 0;
}

static int cmd_mkdir(int argc, char **argv)
{
    if (argc < 2) { shell_puts("mkdir: missing operand\n"); return 1; }
    char resolved[ISH_MAX_PATH];
    resolve_path(argv[1], resolved, ISH_MAX_PATH);
    int32_t ret = file_mkdir(resolved);
    if (ret < 0) {
        shell_puts("mkdir: failed to create '");
        shell_puts(argv[1]);
        shell_puts("'\n");
        return 1;
    }
    return 0;
}

static int cmd_rm(int argc, char **argv)
{
    if (argc < 2) { shell_puts("rm: missing operand\n"); return 1; }
    char resolved[ISH_MAX_PATH];
    resolve_path(argv[1], resolved, ISH_MAX_PATH);
    int32_t ret = file_unlink(resolved);
    if (ret < 0) {
        shell_puts("rm: cannot remove '");
        shell_puts(argv[1]);
        shell_puts("'\n");
        return 1;
    }
    return 0;
}

static int cmd_cp(int argc, char **argv)
{
    if (argc < 3) { shell_puts("cp: missing operand\n"); return 1; }
    char src[ISH_MAX_PATH], dst[ISH_MAX_PATH];
    resolve_path(argv[1], src, ISH_MAX_PATH);
    resolve_path(argv[2], dst, ISH_MAX_PATH);

    int32_t fd_in = file_open(src, 0);
    if (fd_in < 0) {
        shell_puts("cp: cannot open '");
        shell_puts(argv[1]);
        shell_puts("'\n");
        return 1;
    }
    int32_t fd_out = file_creat(dst);
    if (fd_out < 0) {
        file_close(fd_in);
        shell_puts("cp: cannot create '");
        shell_puts(argv[2]);
        shell_puts("'\n");
        return 1;
    }
    char buf[512];
    int64_t n;
    while ((n = file_read(fd_in, buf, 512)) > 0) {
        file_write(fd_out, buf, (uint64_t)n);
    }
    file_close(fd_in);
    file_close(fd_out);
    return 0;
}

static int cmd_ps(int argc, char **argv)
{
    (void)argc; (void)argv;
    shell_puts("  PID  PPID  STATE\n");
    int32_t max = get_process_count();
    for (int32_t i = 0; i < max; i++) {
        process_info_t info;
        if (get_process_info(i, &info) == 0) {
            shell_puts("  ");
            shell_print_number(info.pid);
            shell_puts("    ");
            shell_print_number(info.parent_pid);
            shell_puts("    ");
            shell_puts(info.state ? "ALIVE" : "DEAD");
            shell_putchar('\n');
        }
    }
    return 0;
}

static int cmd_clear(int argc, char **argv)
{
    (void)argc; (void)argv;
    draw_fill_rect(0, 0, (uint32_t)g_win_w, (uint32_t)g_win_h, 0xFF1E1E2E);
    draw_present();
    g_cursor_x = 0;
    g_cursor_y = 4;
    return 0;
}

static int cmd_uname(int argc, char **argv)
{
    (void)argc; (void)argv;
    shell_puts("ImplusOS x86_64\n");
    return 0;
}

static int cmd_uptime(int argc, char **argv)
{
    (void)argc; (void)argv;
    uint64_t ms = get_uptime_ms();
    uint32_t secs = (uint32_t)(ms / 1000);
    uint32_t mins = secs / 60;
    uint32_t hours = mins / 60;
    shell_puts("up ");
    shell_print_u32(hours);
    shell_putchar('h');
    shell_print_u32(mins % 60);
    shell_putchar('m');
    shell_print_u32(secs % 60);
    shell_puts("s\n");
    return 0;
}

static int cmd_stat(int argc, char **argv)
{
    if (argc < 2) { shell_puts("stat: missing operand\n"); return 1; }
    char resolved[ISH_MAX_PATH];
    resolve_path(argv[1], resolved, ISH_MAX_PATH);
    file_stat_t st;
    if (file_stat(resolved, &st) < 0 || !st.exists) {
        shell_puts("stat: cannot stat '");
        shell_puts(argv[1]);
        shell_puts("'\n");
        return 1;
    }
    shell_puts("  File: ");
    shell_puts(argv[1]);
    shell_putchar('\n');
    shell_puts("  Size: ");
    shell_print_u32(st.size);
    shell_putchar('\n');
    shell_puts("  Type: ");
    shell_puts(st.is_dir ? "directory" : "regular file");
    shell_putchar('\n');
    return 0;
}

static int cmd_touch(int argc, char **argv)
{
    if (argc < 2) { shell_puts("touch: missing operand\n"); return 1; }
    char resolved[ISH_MAX_PATH];
    resolve_path(argv[1], resolved, ISH_MAX_PATH);
    int32_t fd = file_creat(resolved);
    if (fd < 0) {
        shell_puts("touch: cannot create '");
        shell_puts(argv[1]);
        shell_puts("'\n");
        return 1;
    }
    file_close(fd);
    return 0;
}

static int cmd_help(int argc, char **argv)
{
    (void)argc; (void)argv;
    shell_puts("ImplusOS Shell (ish) - Available commands:\n");
    shell_puts("  echo <text>      Print text\n");
    shell_puts("  pwd              Print working directory\n");
    shell_puts("  cd <dir>         Change directory\n");
    shell_puts("  ls [dir]         List directory contents\n");
    shell_puts("  cat <file>       Show file contents\n");
    shell_puts("  mkdir <dir>      Create directory\n");
    shell_puts("  rm <file>        Remove file\n");
    shell_puts("  cp <src> <dst>   Copy file\n");
    shell_puts("  touch <file>     Create empty file\n");
    shell_puts("  stat <path>      Show file info\n");
    shell_puts("  ps               List processes\n");
    shell_puts("  clear            Clear screen\n");
    shell_puts("  uname            Show system info\n");
    shell_puts("  uptime           Show uptime\n");
    shell_puts("  Apps: editor, files, procman, store, nettest, vm, version\n");
    shell_puts("  help             This help\n");
    shell_puts("  exit             Exit shell\n");
    return 0;
}

typedef struct {
    const char *name;
    const char *path;
} app_alias_t;

static const app_alias_t g_app_aliases[] = {
    {"shell",   "/Userland/SystemApps/com_ImplusOS_shell/com_ImplusOS_shell.ELF"},
    {"version", "/Userland/SystemApps/com_ImplusOS_version/com_ImplusOS_version.ELF"},
    {"about",   "/Userland/SystemApps/com_ImplusOS_version/com_ImplusOS_version.ELF"},
    {"editor",  "/Userland/UserApps/com_ImplusOS_editor/com_ImplusOS_editor.ELF"},
    {"files",   "/Userland/UserApps/com_ImplusOS_filemanager/com_ImplusOS_filemanager.ELF"},
    {"filemanager", "/Userland/UserApps/com_ImplusOS_filemanager/com_ImplusOS_filemanager.ELF"},
    {"procman", "/Userland/UserApps/com_ImplusOS_procman/com_ImplusOS_procman.ELF"},
    {"store",   "/Userland/UserApps/com_ImplusOS_ImplusStore/com_ImplusOS_ImplusStore.ELF"},
    {"nettest", "/Userland/UserApps/com_ImplusOS_NetworkTest/com_ImplusOS_NetworkTest.ELF"},
    {"network", "/Userland/UserApps/com_ImplusOS_NetworkTest/com_ImplusOS_NetworkTest.ELF"},
    {"vm",      "/Userland/UserApps/com_ImplusOS_vm/com_ImplusOS_vm.ELF"},
};

static const char *find_app_alias(const char *name)
{
    for (uint32_t i = 0; i < sizeof(g_app_aliases) / sizeof(g_app_aliases[0]); ++i) {
        if (strcmp(name, g_app_aliases[i].name) == 0) {
            return g_app_aliases[i].path;
        }
    }
    return (const char *)0;
}

static int execute_builtin(int argc, char **argv)
{
    if (argc == 0) return -1;

    if (strcmp(argv[0], "echo") == 0)   return cmd_echo(argc, argv);
    if (strcmp(argv[0], "pwd") == 0)    return cmd_pwd(argc, argv);
    if (strcmp(argv[0], "cd") == 0)     return cmd_cd(argc, argv);
    if (strcmp(argv[0], "ls") == 0)     return cmd_ls(argc, argv);
    if (strcmp(argv[0], "cat") == 0)    return cmd_cat(argc, argv);
    if (strcmp(argv[0], "mkdir") == 0)  return cmd_mkdir(argc, argv);
    if (strcmp(argv[0], "rm") == 0)     return cmd_rm(argc, argv);
    if (strcmp(argv[0], "cp") == 0)     return cmd_cp(argc, argv);
    if (strcmp(argv[0], "touch") == 0)  return cmd_touch(argc, argv);
    if (strcmp(argv[0], "stat") == 0)   return cmd_stat(argc, argv);
    if (strcmp(argv[0], "ps") == 0)     return cmd_ps(argc, argv);
    if (strcmp(argv[0], "clear") == 0)  return cmd_clear(argc, argv);
    if (strcmp(argv[0], "uname") == 0)  return cmd_uname(argc, argv);
    if (strcmp(argv[0], "uptime") == 0) return cmd_uptime(argc, argv);
    if (strcmp(argv[0], "help") == 0)   return cmd_help(argc, argv);
    if (strcmp(argv[0], "exit") == 0)   { process_exit(0); return 0; }

    return -1;
}

static void execute_external(const char *path)
{
    int32_t pid = process_spawn(path);
    if (pid < 0) {
        shell_puts("ish: command not found: ");
        shell_puts(path);
        shell_putchar('\n');
        return;
    }

    int32_t status = 0;
    while (1) {
        int32_t ret = process_waitpid(pid, &status, 0);
        if (ret > 0) break;
        if (ret < 0) break;
        process_yield();
    }
}

static void execute_command(char *cmd)
{
    while (*cmd == ' ' || *cmd == '\t') cmd++;
    if (*cmd == '\0' || *cmd == '#') return;

    history_add(cmd);

    char cmd_copy[ISH_MAX_CMD_LEN];
    strncpy(cmd_copy, cmd, ISH_MAX_CMD_LEN - 1);
    cmd_copy[ISH_MAX_CMD_LEN - 1] = '\0';

    char *argv[ISH_MAX_ARGS];
    int argc = parse_args(cmd_copy, argv);
    if (argc == 0) return;

    int ret = execute_builtin(argc, argv);
    if (ret >= 0) {
        draw_present();
        return;
    }

    const char *alias_path = find_app_alias(argv[0]);
    if (alias_path) {
        execute_external(alias_path);
        draw_present();
        return;
    }

    char exec_path[ISH_MAX_PATH];
    resolve_path(argv[0], exec_path, ISH_MAX_PATH);
    file_stat_t st;
    if (file_stat(exec_path, &st) == 0 && st.exists) {
        execute_external(exec_path);
    } else {
        strncat(exec_path, ".ELF", (size_t)(ISH_MAX_PATH - (int)strlen(exec_path) - 1));
        if (file_stat(exec_path, &st) == 0 && st.exists) {
            execute_external(exec_path);
        } else {
            shell_puts("ish: ");
            shell_puts(argv[0]);
            shell_puts(": command not found\n");
        }
    }
    draw_present();
}

static void replace_current_command(const char *cmd)
{
    while (g_cmd_len > 0) {
        g_cmd_len--;
        shell_putchar('\b');
    }
    memset(g_cmd_buf, 0, sizeof(g_cmd_buf));
    if (!cmd) return;
    strncpy(g_cmd_buf, cmd, ISH_MAX_CMD_LEN - 1);
    g_cmd_buf[ISH_MAX_CMD_LEN - 1] = '\0';
    g_cmd_len = (int)strlen(g_cmd_buf);
    shell_puts(g_cmd_buf);
    draw_present();
}

void _start(void)
{
    g_win = window_create_ex(50, 50, 720, 480, 0xFF1E1E2E, "Terminal - ish");
    if (g_win == 0) {
        while (1) process_yield();
    }

    window_subscribe_keyboard(g_win);
    graphics_init(g_win);

    uint32_t wx, wy, ww, wh;
    if (window_get_rect(g_win, &wx, &wy, &ww, &wh) == 0) {
        g_win_w = (int)ww;
        g_win_h = (int)wh;
    } else {
        g_win_w = 720;
        g_win_h = 480;
    }
    g_cols = g_win_w / g_char_w;
    g_rows = g_win_h / g_char_h;

    draw_fill_rect(0, 0, (uint32_t)g_win_w, (uint32_t)g_win_h, 0xFF1E1E2E);

    g_cursor_x = 0;
    g_cursor_y = 4;

    if (g_win != 0) {
        window_draw_text(g_win, 8, 4, "ImplusOS Shell (ish) v1.0", 0xFFF5C2E7, 14.0f);
        g_cursor_y += g_char_h;
        window_draw_text(g_win, 8, (uint32_t)g_cursor_y, "Type 'help' for available commands.", 0xFF6C7086, 14.0f);
        g_cursor_y += g_char_h;
        g_cursor_x = 0;
        g_cursor_y += 4;
    } else {
        shell_puts("ImplusOS Shell (ish) v1.0\n");
        shell_puts("Type 'help' for available commands.\n\n");
    }

    draw_present();
    shell_prompt();

    g_cmd_len = 0;
    memset(g_cmd_buf, 0, sizeof(g_cmd_buf));

    while (1) {
        input_keyboard_event_t kbd;
        if (window_input_keyboard_poll(&kbd) > 0) {
            if (!kbd.pressed) {
                continue;
            }
            char c = kbd.ascii;

            if (c == '\n' || kbd.keycode == 0x1C) {
                shell_putchar('\n');
                draw_present();
                g_cmd_buf[g_cmd_len] = '\0';
                execute_command(g_cmd_buf);
                g_cmd_len = 0;
                memset(g_cmd_buf, 0, sizeof(g_cmd_buf));
                shell_prompt();
                continue;
            }

            if (c == '\b' || kbd.keycode == 0x0E) {
                if (g_cmd_len > 0) {
                    g_cmd_len--;
                    g_cmd_buf[g_cmd_len] = '\0';
                    shell_putchar('\b');
                    draw_present();
                }
                continue;
            }

            if (kbd.keycode == 0x48) {
                if (g_history_count > 0 && g_history_pos > 0) {
                    g_history_pos--;
                    replace_current_command(g_history[g_history_pos % ISH_HISTORY_SIZE]);
                }
                continue;
            }

            if (kbd.keycode == 0x50) {
                if (g_history_pos < g_history_count - 1) {
                    g_history_pos++;
                    replace_current_command(g_history[g_history_pos % ISH_HISTORY_SIZE]);
                } else {
                    g_history_pos = g_history_count;
                    replace_current_command("");
                }
                continue;
            }
            
            if (c >= 0x20 && c <= 0x7E && g_cmd_len < ISH_MAX_CMD_LEN - 1) {
                g_cmd_buf[g_cmd_len++] = c;
                shell_putchar(c);
                draw_present();
            }
        }
        process_yield();
    }
}
