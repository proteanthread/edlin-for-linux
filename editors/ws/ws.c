/*
 *
 * VERSION: 3.0.0
 * LICENSE: MIT License
 * COPYLEFT: BASIC++ Community
 *
 * ws.c - Portable WordStar-like Full-Screen Editor
 *
 */

#if !defined(_WIN32) && !defined(WIN32) && !defined(__MSDOS__) && !defined(__DOS__)
    #define _DEFAULT_SOURCE
    #define _BSD_SOURCE
    #define _POSIX_SOURCE /* Exposes POSIX unbuffered terminal I/O */
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>

/* Filter to ensure strictly 7-bit ASCII */
static void sanitize_ascii(char* str) {
    if (!str) return;
    char* p = str;
    while (*str) {
        if ((unsigned char)(*str) < 128) {
            *p++ = *str;
        }
        str++;
    }
    *p = '\0';
}

/* --- Platform Specific Terminal Handling --- */
#if defined(_WIN32) || defined(WIN32)
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <conio.h>
    #ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
        #define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
    #endif
    #define GETCH _getch
#elif defined(__MSDOS__) || defined(__DOS__)
    #include <conio.h>
    #define GETCH getch
#else
    #include <termios.h>
    #include <unistd.h>
    #include <sys/ioctl.h>
    struct termios orig_termios;
#endif

#define MAX_WS_LINES 1000
#define MAX_WS_LENGTH 255
#define TAB_STOP 4

#define SHIFT_ARROW_UP 1100
#define SHIFT_ARROW_DOWN 1101
#define SHIFT_ARROW_LEFT 1102
#define SHIFT_ARROW_RIGHT 1103
#define KEY_CTRL_INS 1104
#define KEY_SHIFT_DEL 1105
#define KEY_SHIFT_INS 1106

/* --- Key Definitions --- */
enum editorKey {
    KEY_UP = 1000,
    KEY_DOWN = 1001,
    KEY_LEFT = 1002,
    KEY_RIGHT = 1003,
    KEY_HOME = 1004,
    KEY_END = 1005,
    KEY_PGUP = 1006,
    KEY_PGDN = 1007,
    KEY_INS = 1008,
    KEY_DEL = 1009,
    KEY_F10 = 1020
};

/* --- Global State --- */
static char text_buffer[MAX_WS_LINES][MAX_WS_LENGTH];
static int num_lines = 0;
static char current_filename[MAX_WS_LENGTH] = "";

static int cx = 0, cy = 0;             
static int target_rx = 0;              
static int row_off = 0, col_off = 0;   
static int screen_rows = 24, screen_cols = 80;
#ifndef DEFAULT_COLOR_MODE
#define DEFAULT_COLOR_MODE true
#endif
static const char *bright_colors[] = {
    "\x1b[40;97m", /* Black bg, Bright White */
    "\x1b[40;96m", /* Black bg, Bright Cyan */
    "\x1b[40;92m", /* Black bg, Bright Green */
    "\x1b[40;93m", /* Black bg, Bright Yellow */
    "\x1b[40;95m", /* Black bg, Bright Magenta */
    "\x1b[40;91m"  /* Black bg, Bright Red */
};
#define NUM_BRIGHT_COLORS (sizeof(bright_colors)/sizeof(bright_colors[0]))
static int color_index = 0;
static bool running = true;
static int pushed_char = -1;

static char *ws_clipboard = NULL;

static char *ws_strdup(const char *s) {
    if (!s) return NULL;
    char *dup = malloc(strlen(s) + 1);
    if (dup) strcpy(dup, s);
    return dup;
}

static void ws_set_clipboard(const char* text) {
    if (ws_clipboard) free(ws_clipboard);
    ws_clipboard = text ? ws_strdup(text) : NULL;
}

static char* ws_get_clipboard(void) {
    return ws_clipboard ? ws_strdup(ws_clipboard) : NULL;
}

static bool help_active = false;
static bool sel_active = false;
static int sel_start_r = 0, sel_start_c = 0;
static int sel_end_r = 0, sel_end_c = 0;

static void update_sel_end(int r, int c) {
    if (!sel_active) {
        sel_start_r = cy;
        sel_start_c = cx;
        sel_active = true;
    }
    sel_end_r = r;
    sel_end_c = c;
}

static void clear_sel(void) { sel_active = false; }

static void get_sel_bounds(int *r1, int *c1, int *r2, int *c2) {
    if (sel_start_r < sel_end_r || (sel_start_r == sel_end_r && sel_start_c <= sel_end_c)) {
        *r1 = sel_start_r; *c1 = sel_start_c;
        *r2 = sel_end_r; *c2 = sel_end_c;
    } else {
        *r1 = sel_end_r; *c1 = sel_end_c;
        *r2 = sel_start_r; *c2 = sel_start_c;
    }
}

static char* get_selected_text_ws(void) {
    if (!sel_active) return NULL;
    int r1, c1, r2, c2;
    get_sel_bounds(&r1, &c1, &r2, &c2);
    char *buf = malloc(65536);
    if (!buf) return NULL;
    buf[0] = '\0';
    int pos = 0;
    for (int r = r1; r <= r2; r++) {
        int start = (r == r1) ? c1 : 0;
        int end = (r == r2) ? c2 : (int)strlen(text_buffer[r]);
        for (int i = start; i < end; i++) {
            buf[pos++] = text_buffer[r][i];
        }
        if (r < r2) buf[pos++] = '\n';
    }
    buf[pos] = '\0';
    return buf;
}

static void delete_selected_text_ws(void) {
    if (!sel_active) return;
    int r1, c1, r2, c2;
    get_sel_bounds(&r1, &c1, &r2, &c2);
    char rem[MAX_WS_LENGTH];
    strcpy(rem, text_buffer[r2] + c2);
    text_buffer[r1][c1] = '\0';
    strncat(text_buffer[r1], rem, MAX_WS_LENGTH - strlen(text_buffer[r1]) - 1);
    int lines_to_del = r2 - r1;
    if (lines_to_del > 0) {
        for (int i = r1 + 1; i < num_lines - lines_to_del; i++) {
            strcpy(text_buffer[i], text_buffer[i + lines_to_del]);
        }
        num_lines -= lines_to_del;
    }
    cy = r1; cx = c1;
    sel_active = false;
}

static void insert_newline_ws(void) {
    if (num_lines >= MAX_WS_LINES) return;
    for (int i = num_lines; i > cy; i--) {
        strcpy(text_buffer[i], text_buffer[i-1]);
    }
    num_lines++;
    text_buffer[cy + 1][0] = '\0';
}

static void insert_text_at_cursor_ws(const char *text) {
    if (sel_active) delete_selected_text_ws();
    const char *p = text;
    while (*p) {
        if (*p == '\n' || *p == '\r') {
            if (*p == '\r' && *(p+1) == '\n') p++; 
            insert_newline_ws();
            cy++; cx = 0; p++;
        } else {
            if (strlen(text_buffer[cy]) < MAX_WS_LENGTH - 1) {
                memmove(&text_buffer[cy][cx + 1], &text_buffer[cy][cx], strlen(text_buffer[cy]) - cx + 1);
                text_buffer[cy][cx] = *p;
                cx++;
            }
            p++;
        }
    }
}

static bool prefix_k = false;

#define MAX_RENDER_BUF 1024

/* --- Output Helpers --- */
static void ws_print(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

void get_terminal_size(void) {
#if defined(_WIN32) || defined(WIN32)
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        screen_rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
        screen_cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    } else {
        screen_rows = 24; screen_cols = 80;
    }
#elif defined(__MSDOS__) || defined(__DOS__)
    screen_rows = 25; screen_cols = 80; /* DOS text-mode standard */
#else
    struct winsize w;
    if (ioctl(1, TIOCGWINSZ, &w) != -1 && w.ws_row > 0) {
        screen_rows = w.ws_row;
        screen_cols = w.ws_col;
    } else {
        screen_rows = 24; screen_cols = 80;
    }
#endif
    if (screen_rows < 5) screen_rows = 24;
    if (screen_cols < 10) screen_cols = 80;
}

void reset_term(void) {
#if !defined(_WIN32) && !defined(WIN32) && !defined(__MSDOS__) && !defined(__DOS__)
    tcsetattr(0, TCSANOW, &orig_termios);
#endif
}

void init_term(void) {
#if defined(_WIN32) || defined(WIN32)
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    SetConsoleMode(hOut, dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#elif !defined(__MSDOS__) && !defined(__DOS__)
    struct termios raw;
    tcgetattr(0, &orig_termios);
    atexit(reset_term);
    raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON | ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(0, TCSANOW, &raw);
#endif
}

int get_input(void) {
    if (pushed_char != -1) {
        int c = pushed_char;
        pushed_char = -1;
        return c;
    }

#if defined(_WIN32) || defined(WIN32) || defined(__MSDOS__) || defined(__DOS__)
    int c = GETCH();
    if (c < 0) {
        running = false; 
        return 0;
    }
    if (c == 0 || c == 224) {
        int seq = GETCH();
        switch (seq) {
            case 72: return KEY_UP;
            case 80: return KEY_DOWN;
            case 75: return KEY_LEFT;
            case 77: return KEY_RIGHT;
            case 71: return KEY_HOME;
            case 79: return KEY_END;
            case 73: return KEY_PGUP;
            case 81: return KEY_PGDN;
            case 82: return KEY_INS;
            case 83: return KEY_DEL;
            case 68: return KEY_F10;
        }
        return 0;
    }
    return c;
#else
    char c, seq1, seq2, seq3;
    if (read(0, &c, 1) != 1) {
        running = false;
        return 0;
    }
    
    if (c == 27) {
        struct termios raw;
        tcgetattr(0, &raw);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 1;
        tcsetattr(0, TCSANOW, &raw);
        
        if (read(0, &seq1, 1) == 1) {
            if (seq1 == '[' || seq1 == 'O') {
                if (read(0, &seq2, 1) == 1) {
                    if (seq1 == '[' && seq2 >= '0' && seq2 <= '9') {
                        if (read(0, &seq3, 1) == 1) {
                            if (seq3 == '~') {
                                tcsetattr(0, TCSANOW, &orig_termios);
                                raw.c_cc[VMIN] = 1; raw.c_cc[VTIME] = 0;
                                tcsetattr(0, TCSANOW, &raw);
                                switch(seq2) {
                                    case '1': return KEY_HOME;
                                    case '2': return KEY_INS;
                                    case '3': return KEY_DEL;
                                    case '4': return KEY_END;
                                    case '5': return KEY_PGUP;
                                    case '6': return KEY_PGDN;
                                    case '7': return KEY_HOME;
                                    case '8': return KEY_END;
                                }
                            } else if (seq2 == '2' && seq3 == '1') {
                                char seq4;
                                if (read(0, &seq4, 1) == 1 && seq4 == '~') {
                                    raw.c_cc[VMIN] = 1; raw.c_cc[VTIME] = 0; tcsetattr(0, TCSANOW, &raw);
                                    return KEY_F10;
                                }
                            }
                        }
                    } else {
                        raw.c_cc[VMIN] = 1; raw.c_cc[VTIME] = 0; tcsetattr(0, TCSANOW, &raw);
                        if (seq1 == '[') {
                            switch(seq2) {
                                case 'A': return KEY_UP;
                                case 'B': return KEY_DOWN;
                                case 'C': return KEY_RIGHT;
                                case 'D': return KEY_LEFT;
                                case 'H': return KEY_HOME;
                                case 'F': return KEY_END;
                            }
                        } else if (seq1 == 'O') {
                            switch(seq2) {
                                case 'H': return KEY_HOME;
                                case 'F': return KEY_END;
                            }
                        }
                    }
                }
            } else {
                pushed_char = seq1;
                raw.c_cc[VMIN] = 1; raw.c_cc[VTIME] = 0; tcsetattr(0, TCSANOW, &raw);
                return 27;
            }
        }
        raw.c_cc[VMIN] = 1; raw.c_cc[VTIME] = 0; tcsetattr(0, TCSANOW, &raw);
        return 27;
    }
    return c;
#endif
}

/* --- Core Editor Functions --- */
static void load_file(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (file) {
        num_lines = 0;
        while (num_lines < MAX_WS_LINES && fgets(text_buffer[num_lines], MAX_WS_LENGTH, file)) {
            sanitize_ascii(text_buffer[num_lines]);
            
        size_t len = strlen(text_buffer[num_lines]);
            if (len > 0 && (text_buffer[num_lines][len - 1] == '\n' || text_buffer[num_lines][len - 1] == '\r')) {
                text_buffer[num_lines][len - 1] = '\0';
                if (len > 1 && text_buffer[num_lines][len - 2] == '\r') {
                    text_buffer[num_lines][len - 2] = '\0';
                }
            }
            num_lines++;
        }
        fclose(file);
    }
    strncpy(current_filename, filename, MAX_WS_LENGTH - 1);
    current_filename[MAX_WS_LENGTH - 1] = '\0';
    
    if (num_lines == 0) {
        num_lines = 1;
        text_buffer[0][0] = '\0';
    }
}

static void save_file(void) {
    FILE *file = fopen(current_filename, "w");
    if (!file) return;
    for (int i = 0; i < num_lines; i++) {
        fprintf(file, "%s\n", text_buffer[i]);
    }
    fclose(file);
}

static void insert_char(int c) {
    int len = (int)strlen(text_buffer[cy]);
    if (len >= MAX_WS_LENGTH - 1) return;
    memmove(&text_buffer[cy][cx + 1], &text_buffer[cy][cx], len - cx + 1);
    text_buffer[cy][cx] = (char)c;
    cx++;
}

static void insert_newline(void) {
    if (num_lines >= MAX_WS_LINES) return;
    for (int i = num_lines; i > cy + 1; i--) {
        strcpy(text_buffer[i], text_buffer[i - 1]);
    }
    strcpy(text_buffer[cy + 1], text_buffer[cy] + cx);
    text_buffer[cy][cx] = '\0';
    num_lines++;
    cy++;
    cx = 0;
}

static void handle_backspace(void) {
    int len, prev_len, cur_len;
    if (cx > 0) {
        len = (int)strlen(text_buffer[cy]);
        memmove(&text_buffer[cy][cx - 1], &text_buffer[cy][cx], len - cx + 1);
        cx--;
    } else if (cy > 0) {
        prev_len = (int)strlen(text_buffer[cy - 1]);
        cur_len = (int)strlen(text_buffer[cy]);
        if (prev_len + cur_len < MAX_WS_LENGTH) {
            strcat(text_buffer[cy - 1], text_buffer[cy]);
            for (int i = cy; i < num_lines - 1; i++) {
                strcpy(text_buffer[i], text_buffer[i + 1]);
            }
            num_lines--;
            cy--;
            cx = prev_len;
        }
    }
}

static int get_render_x(int row, int physical_x) {
    int rx = 0;
    for (int j = 0; j < physical_x && text_buffer[row][j] != '\0'; j++) {
        if (text_buffer[row][j] == '\t') rx += (TAB_STOP - 1) - (rx % TAB_STOP);
        rx++;
    }
    return rx;
}

static int get_physical_x(int row, int target_x) {
    int rx = 0, j;
    for (j = 0; text_buffer[row][j] != '\0'; j++) {
        int next_rx = rx;
        if (text_buffer[row][j] == '\t') next_rx += (TAB_STOP - 1) - (rx % TAB_STOP);
        next_rx++;
        if (next_rx > target_x) return j;
        rx = next_rx;
    }
    return j;
}

static void render_row(int row, char *out_buf) {
    int j = 0, idx = 0;
    while (text_buffer[row][j] != '\0' && idx < (MAX_RENDER_BUF - 1)) {
        if (text_buffer[row][j] == '\t') {
            out_buf[idx++] = ' ';
            while (idx % TAB_STOP != 0 && idx < (MAX_RENDER_BUF - 1)) out_buf[idx++] = ' ';
        } else {
            out_buf[idx++] = text_buffer[row][j];
        }
        j++;
    }
    out_buf[idx] = '\0';
}

static void refresh_screen(void) {
    int y, y_start = 0, print_len, len, file_row, rx;
    char status_bar[512];
    char r_buf[MAX_RENDER_BUF];
    
    get_terminal_size();
    
    ws_print("\x1b[?25l"); 
    ws_print("\x1b[H");    
    
    ws_print("%s", bright_colors[color_index]);
    
    if (help_active) {
        ws_print("\x1b[47;30m");
        ws_print("----------------- Help (^K^H toggles) ----------------------\r\n");
        ws_print(" ^S = Left | ^D = Right | ^E = Up | ^X = Down               \r\n");
        ws_print(" ^K^D = Save & Exit | ^K^Q = Quit                           \r\n");
        ws_print("%s", bright_colors[color_index]);
        y_start = 4;
    }
    
    for (y = y_start; y < screen_rows - 1; y++) {
        file_row = row_off + (y - y_start);
        if (file_row < num_lines) {
            render_row(file_row, r_buf);
            len = (int)strlen(r_buf);
            if (len > col_off) {
                print_len = len - col_off;
                if (print_len > screen_cols) print_len = screen_cols;
                for (int i = col_off; i < col_off + print_len; i++) {
                    int in_sel = 0;
                    if (sel_active) {
                        int r1, c1, r2, c2;
                        get_sel_bounds(&r1, &c1, &r2, &c2);
                        if (file_row > r1 && file_row < r2) in_sel = 1;
                        else if (file_row == r1 && file_row == r2 && i >= c1 && i < c2) in_sel = 1;
                        else if (file_row == r1 && file_row < r2 && i >= c1) in_sel = 1;
                        else if (file_row == r2 && file_row > r1 && i < c2) in_sel = 1;
                    }
                    if (in_sel) {
                        ws_print("\x1b[47;30m%c%s", r_buf[i], bright_colors[color_index]);
                    }
                    else ws_print("%c", r_buf[i]);
                }
            }
        }
        ws_print("\x1b[K\r\n"); 
    }
    
    ws_print("\x1b[47;30m");
    sprintf(status_bar, " %s%s | File: %s | %d:%d ", 
            prefix_k ? "^K " : "", 
            help_active ? "" : "(Press ^K^H for Help)", 
            current_filename[0] ? current_filename : "NEW FILE", 
            cy + 1, num_lines);
            
    len = (int)strlen(status_bar);
    ws_print("%s", status_bar);
    for (y = len; y < screen_cols; y++) ws_print(" "); 
    ws_print("%s", bright_colors[color_index]);
    
    rx = get_render_x(cy, cx);
    ws_print("\x1b[%d;%dH", (cy - row_off) + y_start + 1, (rx - col_off) + 1);
    ws_print("\x1b[?25h"); 
    fflush(stdout);
}

int main(int argc, char **argv) {
    int c, len, cur_len, next_len, visible_rows, rx;
    bool moved_vertically = false;
    
    if (argc > 1) {
        load_file(argv[1]);
    } else {
        num_lines = 1;
        text_buffer[0][0] = '\0';
    }
    
    init_term();
    
    while (running) {
        refresh_screen();
        c = get_input();
        if (c == 0) break;
        moved_vertically = false;
        
        if (prefix_k) {
            if (c == 8 || c == 'h' || c == 'H') {         
                help_active = !help_active;
            } else if (c == 15 || c == 'o' || c == 'O' || c == 'v' || c == 'V') {
                color_index = (color_index + 1) % NUM_BRIGHT_COLORS;
            } else if (c == 4 || c == 'd' || c == 'D') {  
                if (current_filename[0]) save_file();
                break;
            } else if (c == 17 || c == 'q' || c == 'Q') { 
                break;
            }
            prefix_k = false;
            continue;
        }
        
        if (c == 27) { help_active = false; clear_sel(); }
        else if (c == KEY_F10) { color_index = (color_index + 1) % NUM_BRIGHT_COLORS; }
        else if (c == SHIFT_ARROW_UP) { update_sel_end(cy > 0 ? cy - 1 : 0, cx); cy = sel_end_r; }
        else if (c == SHIFT_ARROW_DOWN) { update_sel_end(cy < num_lines - 1 ? cy + 1 : num_lines - 1, cx); cy = sel_end_r; }
        else if (c == SHIFT_ARROW_LEFT) { 
            update_sel_end(cy, cx > 0 ? cx - 1 : 0); 
            if (cx == 0 && cy > 0) update_sel_end(cy - 1, (int)strlen(text_buffer[cy-1]));
            cx = sel_end_c; cy = sel_end_r; 
        }
        else if (c == SHIFT_ARROW_RIGHT) { 
            update_sel_end(cy, cx < (int)strlen(text_buffer[cy]) ? cx + 1 : cx); 
            if (cx == (int)strlen(text_buffer[cy]) && cy < num_lines - 1) update_sel_end(cy + 1, 0);
            cx = sel_end_c; cy = sel_end_r; 
        }
        else if (c == KEY_CTRL_INS) {
            char *txt = get_selected_text_ws();
            if (txt) { ws_set_clipboard(txt); free(txt); clear_sel(); }
        }
        else if (c == KEY_SHIFT_DEL) {
            char *txt = get_selected_text_ws();
            if (txt) { ws_set_clipboard(txt); free(txt); delete_selected_text_ws(); }
        }
        else if (c == KEY_SHIFT_INS) {
            char *txt = ws_get_clipboard();
            if (txt) { insert_text_at_cursor_ws(txt); free(txt); }
        }
        else if (c == KEY_UP || c == KEY_DOWN || c == KEY_LEFT || c == KEY_RIGHT || c == KEY_HOME || c == KEY_END || c == KEY_PGUP || c == KEY_PGDN) {
            clear_sel();
        }
        else {
            if ((c >= 32 && c <= 126) || c == 8 || c == 127 || c == 13 || c == KEY_DEL) clear_sel();
        }

        switch (c) {
            case 11: /* ^K Prefix */
                prefix_k = true;
                break;
            case 5: /* ^E Up */
            case KEY_UP:
                if (cy > 0) { cy--; moved_vertically = true; }
                break;
            case 24: /* ^X Down */
            case KEY_DOWN:
                if (cy < num_lines - 1) { cy++; moved_vertically = true; }
                break;
            case 19: /* ^S Left */
            case KEY_LEFT:
                if (cx > 0) cx--;
                else if (cy > 0) { cy--; cx = (int)strlen(text_buffer[cy]); }
                break;
            case 4: /* ^D Right */
            case KEY_RIGHT:
                if (cx < (int)strlen(text_buffer[cy])) cx++;
                else if (cy < num_lines - 1) { cy++; cx = 0; }
                break;
            case KEY_PGUP:
                cy -= (screen_rows - (help_active ? 4 : 1));
                if (cy < 0) cy = 0;
                moved_vertically = true;
                break;
            case KEY_PGDN:
                cy += (screen_rows - (help_active ? 4 : 1));
                if (cy >= num_lines) cy = num_lines - 1;
                moved_vertically = true;
                break;
            case KEY_HOME:
                cx = 0;
                break;
            case KEY_END:
                cx = (int)strlen(text_buffer[cy]);
                break;
            case KEY_DEL:
                len = (int)strlen(text_buffer[cy]);
                if (cx < len) {
                    memmove(&text_buffer[cy][cx], &text_buffer[cy][cx + 1], len - cx);
                } else if (cy < num_lines - 1) {
                    cur_len = (int)strlen(text_buffer[cy]);
                    next_len = (int)strlen(text_buffer[cy + 1]);
                    if (cur_len + next_len < MAX_WS_LENGTH) {
                        strcat(text_buffer[cy], text_buffer[cy + 1]);
                        for (int i = cy + 1; i < num_lines - 1; i++) {
                            strcpy(text_buffer[i], text_buffer[i + 1]);
                        }
                        num_lines--;
                    }
                }
                break;
            case KEY_INS:
            case KEY_F10:
                break;
            case 10:
            case 13: 
                insert_newline();
                break;
            case 8:
            case 127: 
                handle_backspace();
                break;
            default:
                if ((c >= 32 && c <= 126) || c == '\t') {
                    insert_char(c);
                }
                break;
        }
        
        len = (int)strlen(text_buffer[cy]);
        if (moved_vertically) {
            cx = get_physical_x(cy, target_rx);
        } else {
            if (cx > len) cx = len;
            target_rx = get_render_x(cy, cx);
        }
        
        visible_rows = screen_rows - (help_active ? 5 : 1);
        if (cy < row_off) row_off = cy;
        if (cy >= row_off + visible_rows) row_off = cy - visible_rows + 1;
        
        rx = get_render_x(cy, cx);
        if (rx < col_off) col_off = rx;
        if (rx >= col_off + screen_cols) col_off = rx - screen_cols + 1;
    }
    
    ws_print("\x1b[0m\x1b[2J\x1b[H"); 
    return 0;
}