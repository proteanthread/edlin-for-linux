/*
 *
 * VERSION: 4.1.0
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
#include <stdint.h>
#include <stdarg.h>
#include <time.h>
#include <ctype.h>

#if !defined(_WIN32) && !defined(WIN32) && !defined(__MSDOS__) && !defined(__DOS__)
    #include <poll.h>
#endif

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
    KEY_F10 = 1020,
    KEY_TIMEOUT = 1021,
    KEY_CTRL_HOME = 1022,
    KEY_CTRL_END = 1023
};

static void ws_print(const char *fmt, ...);

static int is_syntax_keyword(const char *word, int len) {
    static const char *keywords[] = {
        "if", "else", "while", "for", "return", "switch", "case", "break", "continue",
        "int", "char", "void", "float", "double", "bool", "static", "const", "struct", "class", "public", "private", "unsigned", "long", "short", "sizeof"
    };
    for (int i = 0; i < (int)(sizeof(keywords)/sizeof(keywords[0])); i++) {
        if (len == (int)strlen(keywords[i]) && strncmp(word, keywords[i], (size_t)len) == 0) return 1;
    }
    return 0;
}

/* Forward declarations for theme colors (defined later) */
static const char *bright_colors[6];
static int color_index;

static void print_syntax_highlighted(const char *text, int *in_multiline_comment, int is_selected) {
    if (!text) return;
    int len = (int)strlen(text);
    int i = 0;
    
    // Base color for normal text - use the editor's theme color
    const char *base_color = is_selected ? "\x1b[7m" : bright_colors[color_index];
    const char *kw_color = "\x1b[96m"; // Cyan for keywords
    const char *str_color = "\x1b[93m"; // Yellow for strings
    const char *num_color = "\x1b[95m"; // Magenta for numbers
    const char *op_color = "\x1b[92m"; // Green for operators
    const char *comm_color = "\x1b[90m"; // Gray for comments
    const char *struct_color = "\x1b[94m"; // Bright Blue for (){}[]
    
    ws_print("%s", base_color);

    while (i < len) {
        if (*in_multiline_comment) {
            ws_print("%s", comm_color);
            while (i < len) {
                if (text[i] == '*' && i + 1 < len && text[i+1] == '/') {
                    ws_print("*/%s", base_color);
                    *in_multiline_comment = 0;
                    i += 2;
                    break;
                }
                ws_print("%c", text[i++]);
            }
            continue;
        }

        if (text[i] == '"' || text[i] == '\'') {
            char quote = text[i];
            ws_print("%s%c", str_color, text[i++]);
            while (i < len) {
                if (text[i] == '\\' && i + 1 < len) {
                    ws_print("\\%c", text[i+1]);
                    i += 2;
                } else if (text[i] == quote) {
                    ws_print("%c%s", text[i++], base_color);
                    break;
                } else {
                    ws_print("%c", text[i++]);
                }
            }
            continue;
        }

        if (text[i] == '/' && i + 1 < len && text[i+1] == '*') {
            ws_print("%s/*", comm_color);
            *in_multiline_comment = 1;
            i += 2;
            continue;
        }

        if (text[i] == '/' && i + 1 < len && text[i+1] == '/') {
            ws_print("%s", comm_color);
            while (i < len) ws_print("%c", text[i++]);
            ws_print("%s", base_color);
            break;
        }

        if (isalpha((unsigned char)text[i]) || text[i] == '_') {
            int start = i;
            while (i < len && (isalnum((unsigned char)text[i]) || text[i] == '_')) i++;
            int wlen = i - start;
            if (is_syntax_keyword(text + start, wlen)) {
                ws_print("%s", kw_color);
                for (int j = start; j < i; j++) ws_print("%c", text[j]);
                ws_print("%s", base_color);
            } else {
                for (int j = start; j < i; j++) ws_print("%c", text[j]);
            }
            continue;
        }

        if (isdigit((unsigned char)text[i])) {
            ws_print("%s", num_color);
            while (i < len && (isalnum((unsigned char)text[i]) || text[i] == '.')) {
                ws_print("%c", text[i++]);
            }
            ws_print("%s", base_color);
            continue;
        }

        if (strchr("+-*/=<>!&|%^~?:", text[i])) {
            ws_print("%s%c%s", op_color, text[i++], base_color);
            continue;
        }
        if (strchr("()[]{}", text[i])) {
            ws_print("%s%c%s", struct_color, text[i++], base_color);
            continue;
        }

        ws_print("%c", text[i++]);
    }
    ws_print("%s", bright_colors[color_index]);
}


/* --- Global State --- */
#if defined(_WIN32) || defined(WIN32)
  #define fseek_64 _fseeki64
  #define ftell_64 _ftelli64
#else
  #define fseek_64 fseeko
  #define ftell_64 ftello
#endif

typedef struct {
    char *text;
    int64_t disk_offset;
    int length;
    int capacity;
    int in_multiline_comment;
} Line;
static Line *text_buffer = NULL;
static int text_buffer_capacity = 0;
static int num_lines = 0;
static char current_filename[4096] = "";

static FILE *orig_file = NULL;
static FILE *index_file = NULL;
static char view_buf[8192];
static bool is_fully_loaded = false;
static bool is_read_only = false;

static void oom(void) {
    fprintf(stderr, "\n\nOut of memory!\n");
    exit(1);
}

static Line get_line_info(int row) {
    if (text_buffer) return text_buffer[row];
    Line l;
    memset(&l, 0, sizeof(Line));
    if (index_file) {
        fseek_64(index_file, (int64_t)row * (int64_t)sizeof(Line), SEEK_SET);
        fread(&l, sizeof(Line), 1, index_file);
    }
    return l;
}

static const char* get_line_text(int row) {
    int clines = num_lines;
    if (row < 0 || row >= clines) return "";
    if (text_buffer && text_buffer[row].text) return text_buffer[row].text;
    Line l = get_line_info(row);
    if (l.text) return l.text;
    if (!orig_file) return "";
    fseek_64(orig_file, l.disk_offset, SEEK_SET);
    int to_read = l.length;
    if (to_read >= (int)sizeof(view_buf)) to_read = sizeof(view_buf) - 1;
    if (to_read > 0) fread(view_buf, 1, (size_t)to_read, orig_file);
    view_buf[to_read] = '\0';
    return view_buf;
}

static void ensure_line_in_memory(int row) {
    if (is_read_only) return;
    if (is_fully_loaded) return;
    if (row < 0 || row >= num_lines) return;
    if (!text_buffer[row].text) {
        text_buffer[row].capacity = text_buffer[row].length + 128;
        text_buffer[row].text = malloc((size_t)text_buffer[row].capacity);
        if (!text_buffer[row].text) oom();
        if (text_buffer[row].length > 0 && orig_file) {
            fseek_64(orig_file, text_buffer[row].disk_offset, SEEK_SET);
            fread(text_buffer[row].text, 1, (size_t)text_buffer[row].length, orig_file);
        }
        text_buffer[row].text[text_buffer[row].length] = '\0';
    }
}

static void ensure_line_capacity(int row, size_t needed) {
    ensure_line_in_memory(row);
    if (needed > (size_t)text_buffer[row].capacity) {
        size_t new_cap = (size_t)text_buffer[row].capacity * 2;
        if (new_cap < needed) new_cap = needed;
        if (new_cap < 128) new_cap = 128;
        char *new_text = realloc(text_buffer[row].text, new_cap);
        if (!new_text) oom();
        text_buffer[row].text = new_text;
        if (new_cap > (size_t)text_buffer[row].capacity) {
            memset(&new_text[text_buffer[row].capacity], 0, new_cap - (size_t)text_buffer[row].capacity);
        }
        text_buffer[row].capacity = (int)new_cap;
    }
}

static void ensure_buffer_capacity(int needed) {
    if (needed > text_buffer_capacity) {
        int new_cap = text_buffer_capacity * 2;
        if (new_cap < needed) new_cap = needed;
        if (new_cap < 256) new_cap = 256;
        Line *new_buf = realloc(text_buffer, (size_t)new_cap * sizeof(Line));
        if (!new_buf) oom();
        memset(&new_buf[text_buffer_capacity], 0, (size_t)(new_cap - text_buffer_capacity) * sizeof(Line));
        text_buffer = new_buf;
        text_buffer_capacity = new_cap;
    }
}

static void insert_empty_line(int row) {
    ensure_buffer_capacity(num_lines + 1);
    for (int i = num_lines; i > row; i--) {
        text_buffer[i] = text_buffer[i - 1];
    }
    text_buffer[row].text = malloc(128);
    if (!text_buffer[row].text) oom();
    text_buffer[row].text[0] = '\0';
    text_buffer[row].disk_offset = 0;
    text_buffer[row].length = 0;
    text_buffer[row].capacity = 128;
    text_buffer[row].in_multiline_comment = 0;
    num_lines++;
}

static void free_line(int row) {
    if (text_buffer[row].text) {
        free(text_buffer[row].text);
        text_buffer[row].text = NULL;
    }
}

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
        int end = (r == r2) ? c2 : (int)get_line_info(r).length;
        for (int i = start; i < end; i++) {
            if (pos >= 65534) break;
            buf[pos++] = get_line_text(r)[i];
        }
        if (r < r2 && pos < 65534) buf[pos++] = '\n';
    }
    buf[pos] = '\0';
    return buf;
}

static void delete_selected_text_ws(void) {
    if(is_read_only) return;
    if (!sel_active) return;
    int r1, c1, r2, c2;
    get_sel_bounds(&r1, &c1, &r2, &c2);
    int rem_len = get_line_info(r2).length - c2;
    char *rem = malloc((size_t)(rem_len + 1));
    if (!rem) oom();
    ensure_line_in_memory(r2);
    ensure_line_in_memory(r1);
    strcpy(rem, get_line_text(r2) + c2);
    
    text_buffer[r1].text[c1] = '\0';
    text_buffer[r1].length = c1;
    ensure_line_capacity(r1, (size_t)(c1 + rem_len + 1));
    strcat(text_buffer[r1].text, rem);
    text_buffer[r1].length += rem_len;
    free(rem);
    
    int lines_to_del = r2 - r1;
    if (lines_to_del > 0) {
        for (int i = r1 + 1; i <= r2; i++) free_line(i);
        for (int i = r1 + 1; i < num_lines - lines_to_del; i++) {
            text_buffer[i] = text_buffer[i + lines_to_del];
        }
        num_lines -= lines_to_del;
        memset(&text_buffer[num_lines], 0, (size_t)lines_to_del * sizeof(Line));
    }
    cy = r1; cx = c1;
    sel_active = false;
}

static void insert_newline_ws(void) {
    insert_empty_line(cy + 1);
}

static void insert_text_at_cursor_ws(const char *text) {
    if(is_read_only) return;
    ensure_line_in_memory(cy);
    if (sel_active) delete_selected_text_ws();
    const char *p = text;
    while (*p) {
        if (*p == '\n' || *p == '\r') {
            if (*p == '\r' && *(p+1) == '\n') p++; 
            insert_newline_ws();
            cy++; cx = 0; p++;
        } else {
            int len = get_line_info(cy).length;
            ensure_line_capacity(cy, (size_t)(len + 2));
            memmove(&text_buffer[cy].text[cx + 1], &text_buffer[cy].text[cx], (size_t)(len - cx + 1));
            text_buffer[cy].text[cx] = *p;
            text_buffer[cy].length++;
            cx++;
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
    
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    DWORD dwInMode = 0;
    GetConsoleMode(hIn, &dwInMode);
    SetConsoleMode(hIn, dwInMode & ~(0x0001)); /* Disable ENABLE_PROCESSED_INPUT to allow ^S and ^Q */
#elif !defined(__MSDOS__) && !defined(__DOS__)
    struct termios raw;
    tcgetattr(0, &orig_termios);
    atexit(reset_term);
    raw = orig_termios;
    raw.c_iflag &= (tcflag_t)~(IXON | ICRNL);
    raw.c_lflag &= (tcflag_t)~(ECHO | ICANON | ISIG | IEXTEN);
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

#if defined(_WIN32) || defined(WIN32)
    DWORD start = GetTickCount();
    while (!_kbhit()) {
        if (GetTickCount() - start > 1000) return KEY_TIMEOUT;
        Sleep(50);
    }
    int c = GETCH();
#elif defined(__MSDOS__) || defined(__DOS__)
    int c = GETCH();
#endif
#if defined(_WIN32) || defined(WIN32) || defined(__MSDOS__) || defined(__DOS__)
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
            case 119: return KEY_CTRL_HOME;
            case 117: return KEY_CTRL_END;
        }
        return 0;
    }
    return c;
#else
    char c, seq1, seq2, seq3;
    struct pollfd pfd;
    pfd.fd = 0;
    pfd.events = POLLIN;
    int ret = poll(&pfd, 1, 1000);
    if (ret == 0) return KEY_TIMEOUT;
    if (ret < 0 || read(0, &c, 1) != 1) {
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
                                raw.c_cc[VMIN] = 1; raw.c_cc[VTIME] = 0; tcsetattr(0, TCSANOW, &raw);
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
                            } else if (seq2 == '1' && seq3 == ';') {
                                char seq4, seq5;
                                if (read(0, &seq4, 1) == 1 && read(0, &seq5, 1) == 1) {
                                    if (seq4 == '5') {
                                        raw.c_cc[VMIN] = 1; raw.c_cc[VTIME] = 0; tcsetattr(0, TCSANOW, &raw);
                                        if (seq5 == 'H') return KEY_CTRL_HOME;
                                        if (seq5 == 'F') return KEY_CTRL_END;
                                    }
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
                                case 'A': return KEY_UP;
                                case 'B': return KEY_DOWN;
                                case 'C': return KEY_RIGHT;
                                case 'D': return KEY_LEFT;
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
    if (text_buffer) {
        for (int i = 0; i < num_lines; i++) free_line(i);
        free(text_buffer); text_buffer = NULL;
    }
    if (orig_file) { fclose(orig_file); orig_file = NULL; }
    if (index_file) { fclose(index_file); index_file = NULL; }
    is_fully_loaded = false;
    is_read_only = false;
    num_lines = 0;
    text_buffer_capacity = 0;

    FILE *file = fopen(filename, "rb");
    if (!file) return;

    fseek_64(file, 0, SEEK_END);
    int64_t file_size = ftell_64(file);
    fseek_64(file, 0, SEEK_SET);

    if (file_size < 64 * 1024 * 1024) {
        is_fully_loaded = true;
        ensure_buffer_capacity(1);
        char line_buf[4096];
        int current_mc = 0;
        while (fgets(line_buf, sizeof(line_buf), file)) {
            sanitize_ascii(line_buf);
            size_t len = strlen(line_buf);
            while (len > 0 && (line_buf[len - 1] == '\n' || line_buf[len - 1] == '\r')) {
                line_buf[len - 1] = '\0'; len--;
            }
            insert_empty_line(num_lines);
            ensure_line_capacity(num_lines - 1, len + 1);
            strcpy(text_buffer[num_lines - 1].text, line_buf);
            text_buffer[num_lines - 1].length = (int)len;
            text_buffer[num_lines - 1].in_multiline_comment = current_mc;
            
            for (int i = 0; i < (int)len; i++) {
                if (current_mc) {
                    if (line_buf[i] == '*' && i + 1 < (int)len && line_buf[i+1] == '/') {
                        current_mc = 0;
                        i++;
                    }
                } else {
                    if (line_buf[i] == '/' && i + 1 < (int)len && line_buf[i+1] == '*') {
                        current_mc = 1;
                        i++;
                    }
                }
            }
        }
        fclose(file);
    } else {
        orig_file = file;
        char line_buf[4096];
        int64_t current_offset = 0;
        while (fgets(line_buf, sizeof(line_buf), orig_file)) {
            sanitize_ascii(line_buf);
            size_t len = strlen(line_buf);
            while (len > 0 && (line_buf[len - 1] == '\n' || line_buf[len - 1] == '\r')) {
                line_buf[len - 1] = '\0'; len--;
            }
            if (!is_read_only && (size_t)(num_lines + 1) * sizeof(Line) > 256 * 1024 * 1024) {
                is_read_only = true;
                index_file = tmpfile();
                if (index_file && text_buffer) {
                    fwrite(text_buffer, sizeof(Line), (size_t)num_lines, index_file);
                }
                if (text_buffer) { free(text_buffer); text_buffer = NULL; }
            }
            
            if (!is_read_only) {
                ensure_buffer_capacity(num_lines + 1);
                text_buffer[num_lines].text = NULL;
                text_buffer[num_lines].disk_offset = current_offset;
                text_buffer[num_lines].length = (int)len;
                text_buffer[num_lines].capacity = 0;
                text_buffer[num_lines].in_multiline_comment = 0;
            } else if (index_file) {
                Line l;
                memset(&l, 0, sizeof(Line));
                l.disk_offset = current_offset;
                l.length = (int)len;
                l.in_multiline_comment = 0;
                fwrite(&l, sizeof(Line), 1, index_file);
            }
            num_lines++;
            current_offset = ftell_64(orig_file);
        }
    }
    strncpy(current_filename, filename, 4095);
    current_filename[4095] = '\0';
    if (num_lines == 0) { is_read_only = false; insert_empty_line(0); }
}

static void save_file(void) {
    if (is_read_only) return;
    FILE *file = fopen(current_filename, "w");
    if (!file) return;
    for (int i = 0; i < num_lines; i++) {
        fprintf(file, "%s\n", get_line_text(i));
    }
    fclose(file);
}

static void insert_char(int c) {
    if(is_read_only) return;
    ensure_line_in_memory(cy);
    int len = get_line_info(cy).length;
    ensure_line_capacity(cy, (size_t)(len + 2));
    memmove(&text_buffer[cy].text[cx + 1], &text_buffer[cy].text[cx], (size_t)(len - cx + 1));
    text_buffer[cy].text[cx] = (char)c;
    text_buffer[cy].length++;
    cx++;
}

static void insert_newline(void) {
    if(is_read_only) return;
    ensure_line_in_memory(cy);
    ensure_line_in_memory(cy+1);
    insert_empty_line(cy + 1);
    int remaining_len = get_line_info(cy).length - cx;
    ensure_line_capacity(cy + 1, (size_t)(remaining_len + 1));
    memmove(text_buffer[cy + 1].text, get_line_text(cy) + cx, (size_t)(remaining_len + 1));
    text_buffer[cy + 1].length = remaining_len;
    text_buffer[cy].text[cx] = '\0';
    text_buffer[cy].length = cx;
    cy++;
    cx = 0;
}

static void handle_backspace(void) {
    if(is_read_only) return;
    ensure_line_in_memory(cy);
    if(cy>0) ensure_line_in_memory(cy-1);
    if (cx > 0) {
        int len = get_line_info(cy).length;
        memmove(&text_buffer[cy].text[cx - 1], &text_buffer[cy].text[cx], (size_t)(len - cx + 1));
        text_buffer[cy].length--;
        cx--;
    } else if (cy > 0) {
        int prev_len = get_line_info(cy - 1).length;
        int cur_len = get_line_info(cy).length;
        ensure_line_capacity(cy - 1, (size_t)(prev_len + cur_len + 1));
        memmove(&text_buffer[cy - 1].text[prev_len], get_line_text(cy), (size_t)(cur_len + 1));
        text_buffer[cy - 1].length += cur_len;
        free_line(cy);
        for (int i = cy; i < num_lines - 1; i++) {
            text_buffer[i] = text_buffer[i + 1];
        }
        num_lines--; cy--; cx = prev_len;
        memset(&text_buffer[num_lines], 0, sizeof(Line));
    }
}

static int get_render_x(int row, int physical_x) {
    int rx = 0;
    for (int j = 0; j < physical_x && get_line_text(row)[j] != '\0'; j++) {
        if (get_line_text(row)[j] == '\t') rx += (TAB_STOP - 1) - (rx % TAB_STOP);
        rx++;
    }
    return rx;
}

static int get_physical_x(int row, int target_x) {
    int rx = 0, j;
    for (j = 0; get_line_text(row)[j] != '\0'; j++) {
        int next_rx = rx;
        if (get_line_text(row)[j] == '\t') next_rx += (TAB_STOP - 1) - (rx % TAB_STOP);
        next_rx++;
        if (next_rx > target_x) return j;
        rx = next_rx;
    }
    return j;
}

static void render_row(int row, char *out_buf) {
    int j = 0, idx = 0;
    while (get_line_text(row)[j] != '\0' && idx < (MAX_RENDER_BUF - 1)) {
        if (get_line_text(row)[j] == '\t') {
            out_buf[idx++] = ' ';
            while (idx % TAB_STOP != 0 && idx < (MAX_RENDER_BUF - 1)) out_buf[idx++] = ' ';
        } else {
            out_buf[idx++] = get_line_text(row)[j];
        }
        j++;
    }
    out_buf[idx] = '\0';
}

static void format_filename_for_status(char *out_buf, const char *in_filename, int max_len) {
    if (!in_filename || !in_filename[0]) {
        strcpy(out_buf, "NEW FILE");
        return;
    }
    char abs_path[4096];
#if defined(_WIN32) || defined(WIN32) || defined(__MSDOS__) || defined(__DOS__)
    if (_fullpath(abs_path, in_filename, 4096) == NULL) {
        strcpy(abs_path, in_filename);
    }
#else
    if (realpath(in_filename, abs_path) == NULL) {
        strcpy(abs_path, in_filename);
    }
#endif

    int len = (int)strlen(abs_path);
    if (len <= max_len) {
        strcpy(out_buf, abs_path);
        return;
    }
    
    /* Find base filename */
    const char *base = abs_path;
    for (int i = len - 1; i >= 0; i--) {
        if (abs_path[i] == '/' || abs_path[i] == '\\') {
            base = &abs_path[i + 1];
            break;
        }
    }
    
    int base_len = (int)strlen(base);
    if (base_len >= max_len) {
        strcpy(out_buf, base);
        return;
    }
    
#if defined(_WIN32) || defined(WIN32) || defined(__MSDOS__) || defined(__DOS__)
    const char *prefix = "C:\\...\\";
#else
    const char *prefix = "/.../";
#endif

    int prefix_len = (int)strlen(prefix);
    if (prefix_len + base_len <= max_len) {
        sprintf(out_buf, "%s%s", prefix, base);
    } else {
        strcpy(out_buf, base);
    }
}

static void refresh_screen(void) {
    int y, y_start = 0, print_len, len, file_row, rx;
    char status_bar[4200];
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
		ws_print("---------- WordStar-like text editor version 4.1.0 ---------\r\n");
        ws_print("%s", bright_colors[color_index]);
        y_start = 4;
    }
    
    /* Track multiline comment state sequentially across visible lines */
    int first_visible = row_off;
    int mc = (first_visible < num_lines) ? get_line_info(first_visible).in_multiline_comment : 0;
    
    for (y = y_start; y < screen_rows - 1; y++) {
        file_row = row_off + (y - y_start);
        if (file_row < num_lines) {
            render_row(file_row, r_buf);
            len = (int)strlen(r_buf);
            if (len > col_off) {
                print_len = len - col_off;
                if (print_len > screen_cols) print_len = screen_cols;
                int in_sel = 0;
                if (sel_active) {
                    int r1, c1, r2, c2;
                    get_sel_bounds(&r1, &c1, &r2, &c2);
                    if (file_row >= r1 && file_row <= r2) in_sel = 1;
                }
                char tmp = r_buf[col_off + print_len];
                r_buf[col_off + print_len] = '\0';
                print_syntax_highlighted(&r_buf[col_off], &mc, in_sel);
                r_buf[col_off + print_len] = tmp;
                ws_print("%s", bright_colors[color_index]);
            } else {
                /* Line is empty/short but still need to update mc state */
                const char *full_text = get_line_text(file_row);
                int full_len = (int)strlen(full_text);
                for (int k = 0; k < full_len; k++) {
                    if (mc) {
                        if (full_text[k] == '*' && k + 1 < full_len && full_text[k+1] == '/') { mc = 0; k++; }
                    } else {
                        if (full_text[k] == '/' && k + 1 < full_len && full_text[k+1] == '*') { mc = 1; k++; }
                        else if (full_text[k] == '/' && k + 1 < full_len && full_text[k+1] == '/') break;
                        else if (full_text[k] == '"' || full_text[k] == '\'') {
                            char q = full_text[k++];
                            while (k < full_len) {
                                if (full_text[k] == '\\' && k + 1 < full_len) k += 2;
                                else if (full_text[k] == q) { k++; break; }
                                else k++;
                            }
                            k--; /* outer loop will k++ */
                        }
                    }
                }
            }
        }
        ws_print("\x1b[K\r\n"); 
    }
    
    ws_print("\x1b[%d;1H\x1b[47;30m\x1b[K", screen_rows);
    char trunc_name[2048];
    format_filename_for_status(trunc_name, current_filename, screen_cols - 40);

    snprintf(status_bar, sizeof(status_bar), " %s%s | File: %s | %d:%d ", 
            prefix_k ? "^K " : "", 
            help_active ? "" : "(Press ^K^H for Help)", 
            trunc_name, 
            cy + 1, num_lines);
            
    time_t rawtime;
    struct tm *timeinfo;
    char time_str[64];
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    strftime(time_str, sizeof(time_str), "%H:%M:%S", timeinfo);
    
    int ll = (int)strlen(status_bar);
    int tl = (int)strlen(time_str);
    int pad = screen_cols - ll - tl - 2;
    if (pad < 1) pad = 1;
    
    ws_print("%s", status_bar);
    for (int i = 0; i < pad; i++) ws_print(" ");
    ws_print("%s", time_str);
    ws_print("%s", bright_colors[color_index]);
    
    rx = get_render_x(cy, cx);
    ws_print("\x1b[%d;%dH", (cy - row_off) + y_start + 1, (rx - col_off) + 1);
    ws_print("\x1b[?25h"); 
    fflush(stdout);
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IOFBF, 65536);
    
    int c, len, visible_rows, rx;
    bool moved_vertically = false;
    
    if (argc > 1) {
        load_file(argv[1]);
    } else {
        num_lines = 0;
        insert_empty_line(0);
    }
    
    init_term();
    
    while (running) {
        refresh_screen();
        c = get_input();
        if (c == 0) break;
        if (c == KEY_TIMEOUT) continue;
        moved_vertically = false;
        
        if (prefix_k) {
            if (c == 8 || c == 'h' || c == 'H') {         
                help_active = !help_active;
            } else if (c == 15 || c == 'o' || c == 'O' || c == 'v' || c == 'V') {
                color_index = (int)((unsigned)(color_index + 1) % NUM_BRIGHT_COLORS);
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
        else if (c == KEY_F10) { color_index = (int)((unsigned)(color_index + 1) % NUM_BRIGHT_COLORS); }
        else if (c == SHIFT_ARROW_UP) { update_sel_end(cy > 0 ? cy - 1 : 0, cx); cy = sel_end_r; }
        else if (c == SHIFT_ARROW_DOWN) { update_sel_end(cy < num_lines - 1 ? cy + 1 : num_lines - 1, cx); cy = sel_end_r; }
        else if (c == SHIFT_ARROW_LEFT) { 
            update_sel_end(cy, cx > 0 ? cx - 1 : 0); 
            if (cx == 0 && cy > 0) update_sel_end(cy - 1, (int)get_line_info(cy-1).length);
            cx = sel_end_c; cy = sel_end_r; 
        }
        else if (c == SHIFT_ARROW_RIGHT) { 
            update_sel_end(cy, cx < (int)get_line_info(cy).length ? cx + 1 : cx); 
            if (cx == (int)get_line_info(cy).length && cy < num_lines - 1) update_sel_end(cy + 1, 0);
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
                else if (cy > 0) { cy--; cx = (int)get_line_info(cy).length; }
                break;
            case 4: /* ^D Right */
            case KEY_RIGHT:
                if (cx < (int)get_line_info(cy).length) cx++;
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
                cx = (int)get_line_info(cy).length;
                break;
            case KEY_CTRL_HOME:
                cy = 0; cx = 0; moved_vertically = true;
                break;
            case KEY_CTRL_END:
                cy = num_lines - 1; cx = (int)get_line_info(cy).length; moved_vertically = true;
                break;
            case KEY_DEL:
                {
                    int curl = get_line_info(cy).length;
                    if (cx < curl) {
                        memmove(&text_buffer[cy].text[cx], &text_buffer[cy].text[cx + 1], (size_t)(curl - cx));
                        text_buffer[cy].length--;
                    } else if (cy < num_lines - 1) {
                        int nl = get_line_info(cy + 1).length;
                        ensure_line_capacity(cy, (size_t)(curl + nl + 1));
                        ensure_line_in_memory(cy + 1);
                        memmove(&text_buffer[cy].text[curl], get_line_text(cy + 1), (size_t)(nl + 1));
                        text_buffer[cy].length += nl;
                        free_line(cy + 1);
                        for (int i = cy + 1; i < num_lines - 1; i++) {
                            text_buffer[i] = text_buffer[i + 1];
                        }
                        num_lines--;
                        memset(&text_buffer[num_lines], 0, sizeof(Line));
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
        
        len = (int)get_line_info(cy).length;
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
    /* Cleanup: free all buffer memory and close file handles */
    if (text_buffer) {
        for (int i = 0; i < num_lines; i++) {
            free(text_buffer[i].text);
        }
        free(text_buffer);
        text_buffer = NULL;
    }
    if (orig_file) { fclose(orig_file); orig_file = NULL; }
    if (index_file) { fclose(index_file); index_file = NULL; }
    return 0;
}