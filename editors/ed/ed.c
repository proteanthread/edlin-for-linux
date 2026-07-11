/*
 *
 * VERSION: 3.0.0
 * LICENSE: MIT License
 * COPYLEFT: BASIC++ Community
 *
 * ed.c - a MS-DOS 5.0 / QBASIC EDIT.COM clone
 * Keyboard-driven terminal text editor. Absolutely no mouse required.
 *
 */

#if !defined(_WIN32) && !defined(WIN32) && !defined(__MSDOS__) && !defined(__DOS__)
    #define _DEFAULT_SOURCE
    #define _BSD_SOURCE
    #define _POSIX_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>

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

#define MAX_EDIT_LINES 1000
#define MAX_EDIT_LENGTH 255
#define TAB_STOP 4

/* --- ANSI DOS EDIT Themes --- */
#define COL_BG_BW    "\x1b[0m"
#define COL_MENU     "\x1b[30;47m"
#define COL_MENU_SEL "\x1b[37;40m"
#define COL_STATUS   "\x1b[30;46m"
#define COL_SHADOW   "\x1b[40m"

/* --- Key Codes --- */
enum editorKey {
    KEY_ESC = 27,
    KEY_ENTER = 13,
    KEY_BACKSPACE = 8,
    KEY_TAB = 9,
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN,
    INS_KEY,
    KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F10,
    ALT_F, ALT_E, ALT_S, ALT_D, ALT_H,
    SHIFT_ARROW_UP, SHIFT_ARROW_DOWN, SHIFT_ARROW_LEFT, SHIFT_ARROW_RIGHT,
    KEY_CTRL_INS, KEY_SHIFT_DEL, KEY_SHIFT_INS
};

/* --- Global State --- */
static char text_buffer[MAX_EDIT_LINES][MAX_EDIT_LENGTH];
static int num_lines = 0;
static char current_filename[MAX_EDIT_LENGTH] = "";
static char clipboard_line[MAX_EDIT_LENGTH] = "";
static char search_term[MAX_EDIT_LENGTH] = "";

static int cx = 0, cy = 0;
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

static void clear_sel(void) {
    sel_active = false;
}
             
static int target_rx = 0;              
static int row_off = 0, col_off = 0;   
static int screen_rows = 24, screen_cols = 80;
static bool exit_editor = false;
static int menu_mode = 0; /* 0: Editor, 1: Menu Bar, 2: Dropdown */
static int menu_col = 0;
static int menu_row = 0;

/* Toggles */
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
static bool display_gutter = false;

#define MAX_RENDER_BUF 2048

/* --- Menus Data Structure --- */
static const char* menu_names[] = { " File ", " Edit ", " Search ", " Display ", " Help " };
static int menu_x[] = { 2, 10, 19, 30, 42 };
static const int num_menus = 5;

static const char* file_menu[] = { " New          ", " Open...      ", " Save         ", " Save As...   ", " Exit         " };
static const char* edit_menu[] = { " Cut          ", " Copy         ", " Paste        ", " Clear        " };
static const char* search_menu[]={" Find...      ", " Find Next    " };
static char options_menu[2][20] = { " [COLOR MODE]  ", " [LINE GUTTER] " };
static const char* options_ptrs[] = { options_menu[0], options_menu[1] };
static const char* help_menu[] = { " About...     " };

static const char** dropdowns[] = { file_menu, edit_menu, search_menu, options_ptrs, help_menu };
static const int drop_sizes[] = { 5, 4, 2, 2, 1 };

static char render_buf[65536];
static int render_buf_pos = 0;

/* --- Terminal I/O --- */
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

void get_terminal_size_edit(int *rows, int *cols) {
#if defined(_WIN32) || defined(WIN32)
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        *rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
        *cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    } else {
        *rows = 24; *cols = 80;
    }
#elif defined(__MSDOS__) || defined(__DOS__)
    *rows = 25; *cols = 80;
#else
    struct winsize w;
    if (ioctl(1, TIOCGWINSZ, &w) != -1 && w.ws_row > 0) {
        *rows = w.ws_row;
        *cols = w.ws_col;
    } else {
        *rows = 24; *cols = 80;
    }
#endif
    if (*rows < 5) *rows = 24;
    if (*cols < 10) *cols = 80;
}

static int read_key_edit(void) {
#if defined(_WIN32) || defined(WIN32) || defined(__MSDOS__) || defined(__DOS__)
    int c = GETCH();
    if (c < 0) { exit_editor = true; return 0; }
    if (c == '\r' || c == '\n') return KEY_ENTER;
    if (c == 8 || c == 127) return KEY_BACKSPACE;
    if (c == 0 || c == 224) {
        int seq = GETCH();
        switch (seq) {
            case 72: return ARROW_UP;
            case 80: return ARROW_DOWN;
            case 75: return ARROW_LEFT;
            case 77: return ARROW_RIGHT;
            case 71: return HOME_KEY;
            case 79: return END_KEY;
            case 73: return PAGE_UP;
            case 81: return PAGE_DOWN;
            case 82: return INS_KEY;
            case 83: return DEL_KEY;
            case 59: return KEY_F1;
            case 60: return KEY_F2;
            case 61: return KEY_F3;
            case 62: return KEY_F4;
            case 63: return KEY_F5;
            case 68: return KEY_F10;
            case 33: return ALT_F;
            case 18: return ALT_E;
            case 31: return ALT_S;
            case 32: return ALT_D;
            case 35: return ALT_H;
            case 152: return SHIFT_ARROW_UP;
            case 160: return SHIFT_ARROW_DOWN;
            case 164: return KEY_CTRL_INS;
            case 165: return KEY_SHIFT_DEL;
            case 166: return KEY_SHIFT_INS;
        }
        return KEY_ESC;
    }
    return c;
#else
    char c, seq1, seq2, seq3;
    if (read(0, &c, 1) != 1) { exit_editor = true; return 0; }
    if (c == '\r' || c == '\n') return KEY_ENTER;
    if (c == 8 || c == 127) return KEY_BACKSPACE;

    if (c == 27) {
        struct termios raw;
        tcgetattr(0, &raw);
        raw.c_cc[VMIN] = 0; raw.c_cc[VTIME] = 1;
        tcsetattr(0, TCSANOW, &raw);
        
        if (read(0, &seq1, 1) != 1) {
            tcsetattr(0, TCSANOW, &orig_termios);
            return KEY_ESC;
        }

        if (seq1 == 'f' || seq1 == 'F') { tcsetattr(0, TCSANOW, &orig_termios); return ALT_F; }
        if (seq1 == 'e' || seq1 == 'E') { tcsetattr(0, TCSANOW, &orig_termios); return ALT_E; }
        if (seq1 == 's' || seq1 == 'S') { tcsetattr(0, TCSANOW, &orig_termios); return ALT_S; }
        if (seq1 == 'd' || seq1 == 'D') { tcsetattr(0, TCSANOW, &orig_termios); return ALT_D; }
        if (seq1 == 'h' || seq1 == 'H') { tcsetattr(0, TCSANOW, &orig_termios); return ALT_H; }

        if (seq1 == '[') {
            if (read(0, &seq2, 1) == 1) {
                if (seq2 >= '0' && seq2 <= '9') {
                    if (read(0, &seq3, 1) == 1) {
                        if (seq3 == '~') {
                            tcsetattr(0, TCSANOW, &orig_termios);
                            switch(seq2) {
                                case '1': return HOME_KEY;
                                case '2': return INS_KEY;
                                case '3': return DEL_KEY;
                                case '4': return END_KEY;
                                case '5': return PAGE_UP;
                                case '6': return PAGE_DOWN;
                                case '7': return HOME_KEY;
                                case '8': return END_KEY;
                            }
                        } else if (seq3 == ';') {
                            char seq4, seq5;
                            if (read(0, &seq4, 1) == 1 && read(0, &seq5, 1) == 1) {
                                tcsetattr(0, TCSANOW, &orig_termios);
                                if (seq2 == '1' && seq4 == '2') {
                                    if (seq5 == 'A') return SHIFT_ARROW_UP;
                                    if (seq5 == 'B') return SHIFT_ARROW_DOWN;
                                    if (seq5 == 'C') return SHIFT_ARROW_RIGHT;
                                    if (seq5 == 'D') return SHIFT_ARROW_LEFT;
                                }
                                if (seq2 == '2' && seq4 == '5' && seq5 == '~') return KEY_CTRL_INS;
                                if (seq2 == '3' && seq4 == '2' && seq5 == '~') return KEY_SHIFT_DEL;
                                if (seq2 == '2' && seq4 == '2' && seq5 == '~') return KEY_SHIFT_INS;
                            }
                        } else {
                            char seq4;
                            if (read(0, &seq4, 1) == 1) {
                                tcsetattr(0, TCSANOW, &orig_termios);
                                if (seq4 == '~') {
                                    switch(seq3) {
                                        case '1': return KEY_F1;
                                        case '2': return KEY_F2;
                                        case '3': return KEY_F3;
                                        case '4': return KEY_F4;
                                        case '5': return KEY_F5;
                                    }
                                } else if (seq2 == '2' && seq3 == '1' && seq4 == '~') {
                                    return KEY_F10;
                                }
                            }
                        }
                    }
                } else {
                    tcsetattr(0, TCSANOW, &orig_termios);
                    switch(seq2) {
                        case 'A': return ARROW_UP;
                        case 'B': return ARROW_DOWN;
                        case 'C': return ARROW_RIGHT;
                        case 'D': return ARROW_LEFT;
                        case 'H': return HOME_KEY;
                        case 'F': return END_KEY;
                    }
                }
            }
        } else if (seq1 == 'O') {
            if (read(0, &seq2, 1) == 1) {
                tcsetattr(0, TCSANOW, &orig_termios);
                switch(seq2) {
                    case 'H': return HOME_KEY;
                    case 'F': return END_KEY;
                    case 'P': return KEY_F1;
                    case 'Q': return KEY_F2;
                    case 'R': return KEY_F3;
                    case 'S': return KEY_F4;
                    case 't': return KEY_F5;
                }
            }
        }
        tcsetattr(0, TCSANOW, &orig_termios);
        return KEY_ESC;
    }
    return c;
#endif
}

/* --- Output Helpers --- */
static void edit_print(const char *fmt, ...)
{
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    int len = (int)strlen(buf);
    if (render_buf_pos + len < (int)sizeof(render_buf) - 1) {
        strcpy(render_buf + render_buf_pos, buf);
        render_buf_pos += len;
    }
}

static void edit_flush(void) {
    if (render_buf_pos > 0) {
        render_buf[render_buf_pos] = '\0';
        printf("%s", render_buf);
        fflush(stdout);
        render_buf_pos = 0;
    }
}

/* --- Editor Core Logistics --- */
static void load_file_edit(const char *filename, bool is_initial) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        if (!is_initial) {
            return;
        } else {
            strncpy(current_filename, filename, MAX_EDIT_LENGTH - 1);
            current_filename[MAX_EDIT_LENGTH - 1] = '\0';
            num_lines = 1; text_buffer[0][0] = '\0';
            cx = 0; cy = 0; row_off = 0; col_off = 0;
            return;
        }
    }
    num_lines = 0;
    while (num_lines < MAX_EDIT_LINES && fgets(text_buffer[num_lines], MAX_EDIT_LENGTH, file)) {
        sanitize_ascii(text_buffer[num_lines]);
        
        size_t len = strlen(text_buffer[num_lines]);
        while (len > 0 && (text_buffer[num_lines][len - 1] == '\n' || text_buffer[num_lines][len - 1] == '\r')) {
            text_buffer[num_lines][len - 1] = '\0';
            len--;
        }
        num_lines++;
    }
    fclose(file);
    if (num_lines == 0) {
        num_lines = 1; text_buffer[0][0] = '\0';
    }
    strncpy(current_filename, filename, MAX_EDIT_LENGTH - 1);
    current_filename[MAX_EDIT_LENGTH - 1] = '\0';
    cx = 0; cy = 0; row_off = 0; col_off = 0;
}

static void save_file_edit(void) {
    if (current_filename[0] == '\0') return;
    FILE *file = fopen(current_filename, "w");
    if (!file) return;
    for (int i = 0; i < num_lines; i++) {
        fprintf(file, "%s\n", text_buffer[i]);
    }
    fclose(file);
}

static void insert_char(int c) {
    int len = (int)strlen(text_buffer[cy]);
    if (len >= MAX_EDIT_LENGTH - 1) return;
    memmove(&text_buffer[cy][cx + 1], &text_buffer[cy][cx], len - cx + 1);
    text_buffer[cy][cx] = (char)c;
    cx++;
}

static void insert_newline(void) {
    if (num_lines >= MAX_EDIT_LINES) return;
    for (int i = num_lines; i > cy + 1; i--) {
        memmove(text_buffer[i], text_buffer[i - 1], strlen(text_buffer[i - 1]) + 1);
    }
    memmove(text_buffer[cy + 1], text_buffer[cy] + cx, strlen(text_buffer[cy] + cx) + 1);
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
        if (prev_len + cur_len < MAX_EDIT_LENGTH) {
            memmove(&text_buffer[cy - 1][prev_len], text_buffer[cy], cur_len + 1);
            for (int i = cy; i < num_lines - 1; i++) {
                memmove(text_buffer[i], text_buffer[i + 1], strlen(text_buffer[i + 1]) + 1);
            }
            num_lines--; cy--; cx = prev_len;
        }
    }
}

static void handle_delete(void) {
    int cur_len, next_len;
    cur_len = (int)strlen(text_buffer[cy]);
    if (cx < cur_len) {
        memmove(&text_buffer[cy][cx], &text_buffer[cy][cx + 1], cur_len - cx);
    } else if (cy < num_lines - 1) {
        next_len = (int)strlen(text_buffer[cy + 1]);
        if (cur_len + next_len < MAX_EDIT_LENGTH) {
            memmove(&text_buffer[cy][cur_len], text_buffer[cy + 1], next_len + 1);
            for (int i = cy + 1; i < num_lines - 1; i++) {
                memmove(text_buffer[i], text_buffer[i + 1], strlen(text_buffer[i + 1]) + 1);
            }
            num_lines--;
        }
    }
}

static void delete_current_line(void) {
    if (num_lines > 1) {
        for (int i = cy; i < num_lines - 1; i++) strcpy(text_buffer[i], text_buffer[i + 1]);
        num_lines--;
        if (cy >= num_lines) cy = num_lines - 1;
        if (cx > (int)strlen(text_buffer[cy])) cx = (int)strlen(text_buffer[cy]);
    } else {
        text_buffer[0][0] = '\0';
        cx = 0;
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

/* --- UI Rendering Primitives --- */
static void draw_all(void) {
    int y, i, file_row, len, print_len, pad;
    char r_buf[MAX_RENDER_BUF];
    char status[512];
    char title_str[MAX_EDIT_LENGTH + 64];
    
    edit_print("\x1b[?25l");
    
    /* 1. Menu Bar */
    edit_print("\x1b[1;1H" COL_MENU "\x1b[K");
    for (i = 0; i < num_menus; i++) {
        edit_print("\x1b[1;%dH", menu_x[i]);
        if (menu_mode > 0 && menu_col == i) edit_print(COL_MENU_SEL);
        else edit_print(COL_MENU);
        edit_print("%s", menu_names[i]);
    }
    
    /* 2. Editor Header */
    edit_print("\x1b[2;1H%s\x1b[K", bright_colors[color_index]);
    sprintf(title_str, "[ %s ]", current_filename[0] ? current_filename : "Untitled");
    pad = (screen_cols - (int)strlen(title_str)) / 2;
    if (pad < 0) pad = 0;
    edit_print("\x1b[2;%dH%s", pad + 1, title_str);
    
    /* 3. Editor Content Area */
    for (y = 3; y <= screen_rows - 1; y++) {
        file_row = row_off + (y - 3);
        edit_print("\x1b[%d;1H%s", y, bright_colors[color_index]); 
        
        int draw_cols = screen_cols;
        int gutter_offset = 0;

        if (display_gutter) {
            char gutter[16];
            if (file_row < num_lines) sprintf(gutter, "%5d |", file_row + 1);
            else sprintf(gutter, "      |");
            edit_print("\x1b[36m%s\x1b[0m%s", gutter, bright_colors[color_index]);
            gutter_offset = 7;
            draw_cols -= gutter_offset;
        }

        if (file_row < num_lines) {
            render_row(file_row, r_buf);
            len = (int)strlen(r_buf);
            if (len > col_off) {
                print_len = len - col_off;
                if (print_len > draw_cols) print_len = draw_cols;
                edit_print("%.*s\x1b[K", print_len, r_buf + col_off);
            } else {
                edit_print("\x1b[K");
            }
        } else {
            edit_print("\x1b[K");
        }
    }
    
    /* 5. Status Bar */
    sprintf(status, " ESC=Menu %-20s                LINE:%-4d/%-4d COL:%-4d", 
            current_filename[0] ? current_filename : "NEW FILE", 
            cy + 1, num_lines, cx + 1);
    
    len = (int)strlen(status);
    if (len > screen_cols - 1) len = screen_cols - 1;
    edit_print("\x1b[%d;1H" COL_STATUS, screen_rows);
    edit_print("%.*s", len, status);
    for (i = len; i < screen_cols - 1; i++) edit_print(" ");
    
    /* 6. Active Dropdown Modal */
    if (menu_mode == 2) {
        int mx = menu_x[menu_col];
        int mh = drop_sizes[menu_col];
        const char **items = dropdowns[menu_col];
        
        for (i = 0; i < mh; i++) {
            edit_print("\x1b[%d;%dH", 2 + i, mx);
            if (menu_row == i) edit_print(COL_MENU_SEL);
            else edit_print(COL_MENU);
            edit_print("%s", items[i]);
            edit_print(COL_SHADOW " \x1b[0m");
        }
        edit_print("\x1b[%d;%dH" COL_SHADOW, 2 + mh, mx + 1);
        for (i = 0; i < (int)strlen(items[0]) + 1; i++) edit_print(" ");
    }
}

/* --- Dialog Elements --- */
static int prompt_input_edit(const char *title, char *buf) {
    int w = 40, h = 7;
    int x, y, len, c, i, j;
    len = (int)strlen(buf);
    
    get_terminal_size_edit(&screen_rows, &screen_cols);
    draw_all();

    while(true) {
        x = (screen_cols - w) / 2;
        y = (screen_rows - h) / 2;
        
        edit_print(COL_SHADOW);
        for (j = 1; j <= h; j++) edit_print("\x1b[%d;%dH  ", y + j, x + w);
        edit_print("\x1b[%d;%dH", y + h, x + 2);
        for (i = 0; i < w; i++) edit_print(" ");
        
        edit_print(COL_MENU);
        for (j = 0; j < h; j++) {
            edit_print("\x1b[%d;%dH", y + j, x);
            if (j == 0 || j == h - 1) {
                edit_print("+"); for (i = 1; i < w - 1; i++) edit_print("-"); edit_print("+");
            } else {
                edit_print("|"); for (i = 1; i < w - 1; i++) edit_print(" "); edit_print("|");
            }
        }
        
        edit_print("\x1b[%d;%dH" COL_MENU " %s ", y, x + (w - (int)strlen(title) - 2) / 2, title);
        
        edit_print("\x1b[%d;%dH" COL_MENU_SEL, y + 3, x + 4);
        for (i = 0; i < w - 8; i++) {
            if (i < len) edit_print("%c", buf[i]);
            else edit_print(" ");
        }
        
        edit_print("\x1b[%d;%dH" COL_MENU " < OK > ", y + 5, x + w / 2 - 4);
        
        edit_print("\x1b[%d;%dH\x1b[?25h", y + 3, x + 4 + len);
        edit_flush();
        c = read_key_edit();
        if (c == KEY_ESC) return 0;
        if (c == KEY_ENTER) return 1;
        if (c == KEY_BACKSPACE) {
            if (len > 0) buf[--len] = '\0';
        } else if (c >= 32 && c <= 126 && len < w - 10) {
            buf[len++] = (char)c;
            buf[len] = '\0';
        }
    }
}

static void show_message(const char *title, const char *msg) {
    int w = 50, h = 8;
    int x, y, c, i, j;
    
    get_terminal_size_edit(&screen_rows, &screen_cols);
    draw_all();

    while(true) {
        x = (screen_cols - w) / 2;
        y = (screen_rows - h) / 2;
        
        edit_print(COL_SHADOW);
        for (j = 1; j <= h; j++) edit_print("\x1b[%d;%dH  ", y + j, x + w);
        edit_print("\x1b[%d;%dH", y + h, x + 2);
        for (i = 0; i < w; i++) edit_print(" ");
        
        edit_print(COL_MENU);
        for (j = 0; j < h; j++) {
            edit_print("\x1b[%d;%dH", y + j, x);
            if (j == 0 || j == h - 1) {
                edit_print("+"); for (i = 1; i < w - 1; i++) edit_print("-"); edit_print("+");
            } else {
                edit_print("|"); for (i = 1; i < w - 1; i++) edit_print(" "); edit_print("|");
            }
        }
        
        edit_print("\x1b[%d;%dH" COL_MENU " %s ", y, x + (w - (int)strlen(title) - 2) / 2, title);
        edit_print("\x1b[%d;%dH%s", y + 3, x + 2, msg);
        edit_print("\x1b[%d;%dH" COL_MENU " < OK > ", y + 6, x + w / 2 - 4);
        edit_flush();
        c = read_key_edit();
        if (c == KEY_ESC || c == KEY_ENTER || c == 32) return;
    }
}

static void do_find(void) {
    char *p;
    for (int r = cy; r < num_lines; r++) {
        int start_x = (r == cy) ? cx + 1 : 0;
        if (start_x < (int)strlen(text_buffer[r])) {
            p = strstr(text_buffer[r] + start_x, search_term);
            if (p) {
                cy = r;
                cx = (int)(p - text_buffer[r]);
                target_rx = get_render_x(cy, cx);
                return;
            }
        }
    }
    show_message(" Find ", "Search term not found.");
}

static void get_sel_bounds(int *r1, int *c1, int *r2, int *c2) {
    if (sel_start_r < sel_end_r || (sel_start_r == sel_end_r && sel_start_c <= sel_end_c)) {
        *r1 = sel_start_r; *c1 = sel_start_c;
        *r2 = sel_end_r; *c2 = sel_end_c;
    } else {
        *r1 = sel_end_r; *c1 = sel_end_c;
        *r2 = sel_start_r; *c2 = sel_start_c;
    }
}

static char* get_selected_text_edit(void) {
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
        if (r < r2) {
            buf[pos++] = '\n';
        }
    }
    buf[pos] = '\0';
    return buf;
}

static void delete_selected_text_edit(void) {
    if (!sel_active) return;
    int r1, c1, r2, c2;
    get_sel_bounds(&r1, &c1, &r2, &c2);
    
    char rem[MAX_EDIT_LENGTH];
    strcpy(rem, text_buffer[r2] + c2);
    
    text_buffer[r1][c1] = '\0';
    strncat(text_buffer[r1], rem, MAX_EDIT_LENGTH - strlen(text_buffer[r1]) - 1);
    
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

static void insert_text_at_cursor_edit(const char *text) {
    if (sel_active) delete_selected_text_edit();
    const char *p = text;
    while (*p) {
        if (*p == '\n' || *p == '\r') {
            if (*p == '\r' && *(p+1) == '\n') p++; 
            insert_newline();
            p++;
        } else {
            if (strlen(text_buffer[cy]) < MAX_EDIT_LENGTH - 1) {
                memmove(&text_buffer[cy][cx + 1], &text_buffer[cy][cx], strlen(text_buffer[cy]) - cx + 1);
                text_buffer[cy][cx] = *p;
                cx++;
            }
            p++;
        }
    }
}

static void execute_menu_edit(void) {
    char buf[MAX_EDIT_LENGTH];
    menu_mode = 0;
    
    if (menu_col == 0) {
        if (menu_row == 0) {
            num_lines = 1; text_buffer[0][0] = '\0'; cy=0; cx=0; current_filename[0]='\0';
            row_off = 0; col_off = 0;
        } else if (menu_row == 1) {
            buf[0] = '\0';
            if (prompt_input_edit(" Open ", buf) && buf[0] != '\0') {
                load_file_edit(buf, false);
            }
        } else if (menu_row == 2) {
            if (current_filename[0] == '\0') {
                buf[0] = '\0';
                if (prompt_input_edit(" Save As ", buf) && buf[0] != '\0') {
                    strcpy(current_filename, buf);
                    save_file_edit();
                }
            } else {
                save_file_edit();
            }
        } else if (menu_row == 3) {
            strcpy(buf, current_filename);
            if (prompt_input_edit(" Save As ", buf) && buf[0] != '\0') {
                strcpy(current_filename, buf);
                save_file_edit();
            }
        } else if (menu_row == 4) {
            exit_editor = true;
        }
    } else if (menu_col == 1) {
        if (menu_row == 0) {
            char *sel = get_selected_text_edit();
            if (sel) { strcpy(clipboard_line, sel); free(sel); delete_selected_text_edit(); }
            else { strcpy(clipboard_line, text_buffer[cy]); delete_current_line(); }
        } else if (menu_row == 1) {
            char *sel = get_selected_text_edit();
            if (sel) { strcpy(clipboard_line, sel); free(sel); }
            else { strcpy(clipboard_line, text_buffer[cy]); }
        } else if (menu_row == 2) {
            insert_text_at_cursor_edit(clipboard_line);
        } else if (menu_row == 3) {
            if (sel_active) delete_selected_text_edit();
            else delete_current_line();
        }
    } else if (menu_col == 2) {
        if (menu_row == 0) {
            buf[0] = '\0';
            if (prompt_input_edit(" Find ", buf) && buf[0] != '\0') {
                strcpy(search_term, buf);
                do_find();
            }
        } else if (menu_row == 1) {
            if (search_term[0] != '\0') do_find();
        }
    } else if (menu_col == 3) {
        if (menu_row == 0) {
            color_index = (color_index + 1) % NUM_BRIGHT_COLORS;
        } else if (menu_row == 1) {
            display_gutter = !display_gutter;
        }
    } else if (menu_col == 4) {
        if (menu_row == 0) {
            char about_msg[128];
            sprintf(about_msg, "Standalone Plaintext Editor");
            show_message(" About ", about_msg);
        }
    }
}

int main(int argc, char **argv) {
    int c, rx, visible_rows, visible_cols, max_rows;
    bool moved_vertically = false;
    
    if (argc > 1) load_file_edit(argv[1], true);
    else { num_lines = 1; text_buffer[0][0] = '\0'; current_filename[0] = '\0'; }

    init_term();
    
    while (!exit_editor) {
        get_terminal_size_edit(&screen_rows, &screen_cols);
        
        visible_rows = screen_rows - 3; 
        visible_cols = screen_cols - 1 - (display_gutter ? 7 : 0);
        
        if (cy < row_off) row_off = cy;
        if (cy >= row_off + visible_rows) row_off = cy - visible_rows + 1;
        rx = get_render_x(cy, cx);
        if (rx < col_off) col_off = rx;
        if (rx >= col_off + visible_cols) col_off = rx - visible_cols + 1;
        
        draw_all();
        
        if (menu_mode == 0) {
            rx = get_render_x(cy, cx);
            edit_print("\x1b[%d;%dH\x1b[?25h", (cy - row_off) + 3, (rx - col_off) + 1 + (display_gutter ? 7 : 0));
        } else {
            edit_print("\x1b[?25l");
        }
        
        edit_flush();
        c = read_key_edit();
        moved_vertically = false;
        
        if (c == KEY_ESC) {
            if (menu_mode == 0) { menu_mode = 1; menu_col = 0; }
            else if (menu_mode == 2) { menu_mode = 1; }
            else { menu_mode = 0; }
            continue;
        }

        /* Invisible F-Keys */
        if (c == KEY_F10) { color_index = (color_index + 1) % NUM_BRIGHT_COLORS; continue; }
        if (c == KEY_F1) { show_message(" Help ", "Standalone C17 Editor"); continue; }
        if (c == KEY_F2) {
            if (current_filename[0] == '\0') {
                char buf[MAX_EDIT_LENGTH]; buf[0] = '\0';
                if (prompt_input_edit(" Save As ", buf) && buf[0] != '\0') {
                    strcpy(current_filename, buf); save_file_edit();
                }
            } else save_file_edit();
            continue;
        }
        if (c == KEY_F3) {
            char buf[MAX_EDIT_LENGTH]; buf[0] = '\0';
            if (prompt_input_edit(" Open ", buf) && buf[0] != '\0') load_file_edit(buf, false);
            continue;
        }
        if (c == KEY_F4) { exit_editor = true; continue; }
        if (c == KEY_F5) { continue; /* Execution removed */ }
        
        if (c == ALT_F) { menu_mode = 2; menu_col = 0; menu_row = 0; continue; }
        if (c == ALT_E) { menu_mode = 2; menu_col = 1; menu_row = 0; continue; }
        if (c == ALT_S) { menu_mode = 2; menu_col = 2; menu_row = 0; continue; }
        if (c == ALT_D) { menu_mode = 2; menu_col = 3; menu_row = 0; continue; }
        if (c == ALT_H) { menu_mode = 2; menu_col = 4; menu_row = 0; continue; }
        
        if (menu_mode == 1) {
            if (c == ARROW_LEFT) { menu_col = (menu_col + num_menus - 1) % num_menus; }
            else if (c == ARROW_RIGHT) { menu_col = (menu_col + 1) % num_menus; }
            else if (c == ARROW_DOWN || c == KEY_ENTER) { menu_mode = 2; menu_row = 0; }
            else if (c == ARROW_UP) { menu_mode = 2; menu_row = drop_sizes[menu_col] - 1; }
        } else if (menu_mode == 2) {
            max_rows = drop_sizes[menu_col];
            if (c == ARROW_UP) { menu_row = (menu_row + max_rows - 1) % max_rows; }
            else if (c == ARROW_DOWN) { menu_row = (menu_row + 1) % max_rows; }
            else if (c == ARROW_LEFT) { menu_col = (menu_col + num_menus - 1) % num_menus; menu_row = 0; }
            else if (c == ARROW_RIGHT) { menu_col = (menu_col + 1) % num_menus; menu_row = 0; }
            else if (c == KEY_ENTER) { execute_menu_edit(); }
        } else {
            if (c == SHIFT_ARROW_UP) { update_sel_end(cy > 0 ? cy - 1 : 0, cx); cy = sel_end_r; }
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
                char *txt = get_selected_text_edit();
                if (txt) { strcpy(clipboard_line, txt); free(txt); clear_sel(); }
            }
            else if (c == KEY_SHIFT_DEL) {
                char *txt = get_selected_text_edit();
                if (txt) { strcpy(clipboard_line, txt); free(txt); delete_selected_text_edit(); }
            }
            else if (c == KEY_SHIFT_INS) {
                insert_text_at_cursor_edit(clipboard_line);
            }
            else if (c == ARROW_UP || c == ARROW_DOWN || c == ARROW_LEFT || c == ARROW_RIGHT || c == HOME_KEY || c == END_KEY || c == PAGE_UP || c == PAGE_DOWN) {
                clear_sel();
            }
            else {
                if ((c >= 32 && c <= 126) || c == KEY_BACKSPACE || c == KEY_ENTER || c == DEL_KEY) clear_sel();
            }

            switch (c) {
                case ARROW_UP: if (cy > 0) { cy--; moved_vertically = true; } break;
                case ARROW_DOWN: if (cy < num_lines - 1) { cy++; moved_vertically = true; } break;
                case ARROW_LEFT: 
                    if (cx > 0) cx--; 
                    else if (cy > 0) { cy--; cx = (int)strlen(text_buffer[cy]); }
                    break;
                case ARROW_RIGHT:
                    if (cx < (int)strlen(text_buffer[cy])) cx++;
                    else if (cy < num_lines - 1) { cy++; cx = 0; }
                    break;
                case PAGE_UP: cy -= (screen_rows - 3); if (cy < 0) cy = 0; moved_vertically = true; break;
                case PAGE_DOWN: cy += (screen_rows - 3); if (cy >= num_lines) cy = num_lines - 1; moved_vertically = true; break;
                case HOME_KEY: cx = 0; break;
                case END_KEY: cx = (int)strlen(text_buffer[cy]); break;
                case DEL_KEY: handle_delete(); break;
                case KEY_ENTER: insert_newline(); break;
                case KEY_BACKSPACE: handle_backspace(); break;
                default: 
                    if ((c >= 32 && c <= 126) || c == KEY_TAB) {
                        insert_char(c);
                    }
                    break;
            }
            if (moved_vertically) cx = get_physical_x(cy, target_rx);
            else {
                if (cx > (int)strlen(text_buffer[cy])) cx = (int)strlen(text_buffer[cy]);
                target_rx = get_render_x(cy, cx);
            }
        }
    }
    
    edit_print("\x1b[0m\x1b[2J\x1b[H\x1b[?25h");
    edit_flush();
    return 0;
}