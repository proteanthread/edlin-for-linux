/*
 *
 * VERSION: 4.1.0
 * LICENSE: MIT License
 * COPYLEFT: BASIC++ Community
 *
 * vi.c - Bare minimum vi-style visual text editor
 *
 */

#if !defined(_WIN32) && !defined(WIN32) && !defined(__MSDOS__) && !defined(__DOS__)
    #define _DEFAULT_SOURCE
    #define _BSD_SOURCE
    #define _POSIX_SOURCE /* Exposes POSIX unbuffered terminal I/O */
#endif

#include <stdio.h>
#define _FILE_OFFSET_BITS 64
#define _FILE_OFFSET_BITS 64
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <ctype.h>

#if defined(_WIN32) || defined(WIN32)
  #define fseek_64 _fseeki64
  #define ftell_64 _ftelli64
#else
  #define fseek_64 fseeko
  #define ftell_64 ftello
#endif

#include <time.h>

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


/* --- Extended Key Codes --- */
#define KEY_UP    1000
#define KEY_DOWN  1001
#define KEY_LEFT  1002
#define KEY_RIGHT 1003
#define KEY_HOME  1004
#define KEY_END   1005
#define KEY_PGUP  1006
#define KEY_PGDN  1007
#define KEY_INS   1008
#define KEY_DEL   1009
#define SHIFT_ARROW_UP 1100
#define SHIFT_ARROW_DOWN 1101
#define SHIFT_ARROW_LEFT 1102
#define SHIFT_ARROW_RIGHT 1103
#define KEY_CTRL_INS 1104
#define KEY_SHIFT_DEL 1105
#define KEY_SHIFT_INS 1106

#define KEY_F1    1011
#define KEY_F2    1012
#define KEY_F3    1013
#define KEY_F4    1014
#define KEY_F5    1015
#define KEY_F10   1020
#define KEY_TIMEOUT 1021
#define KEY_CTRL_HOME 1022
#define KEY_CTRL_END  1023

int current_lines = 0;
typedef struct {
    char *text;
    int64_t disk_offset;
    int length;
    int capacity;
    int in_multiline_comment;
} Line;
Line *text_buffer = NULL;

static int is_syntax_keyword(const char *word, int len) {
    static const char *keywords[] = {
        "if", "else", "while", "for", "return", "switch", "case", "break", "continue",
        "int", "char", "void", "float", "double", "bool", "static", "const", "struct", "class", "public", "private", "unsigned", "long", "short", "sizeof"
    };
    for (int i = 0; i < (int)(sizeof(keywords)/sizeof(keywords[0])); i++) {
        if (len == (int)strlen(keywords[i]) && strncmp(word, keywords[i], len) == 0) return 1;
    }
    return 0;
}

/* Forward declarations for theme colors (defined later) */
static const char *bright_colors[];
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
    
    printf("%s", base_color);

    while (i < len) {
        if (*in_multiline_comment) {
            printf("%s", comm_color);
            while (i < len) {
                if (text[i] == '*' && i + 1 < len && text[i+1] == '/') {
                    printf("*/%s", base_color);
                    *in_multiline_comment = 0;
                    i += 2;
                    break;
                }
                printf("%c", text[i++]);
            }
            continue;
        }

        if (text[i] == '"' || text[i] == '\'') {
            char quote = text[i];
            printf("%s%c", str_color, text[i++]);
            while (i < len) {
                if (text[i] == '\\' && i + 1 < len) {
                    printf("\\%c", text[i+1]);
                    i += 2;
                } else if (text[i] == quote) {
                    printf("%c%s", text[i++], base_color);
                    break;
                } else {
                    printf("%c", text[i++]);
                }
            }
            continue;
        }

        if (text[i] == '/' && i + 1 < len && text[i+1] == '*') {
            printf("%s/*", comm_color);
            *in_multiline_comment = 1;
            i += 2;
            continue;
        }

        if (text[i] == '/' && i + 1 < len && text[i+1] == '/') {
            printf("%s", comm_color);
            while (i < len) printf("%c", text[i++]);
            printf("%s", base_color);
            break;
        }

        if (isalpha((unsigned char)text[i]) || text[i] == '_') {
            int start = i;
            while (i < len && (isalnum((unsigned char)text[i]) || text[i] == '_')) i++;
            int wlen = i - start;
            if (is_syntax_keyword(text + start, wlen)) {
                printf("%s", kw_color);
                for (int j = start; j < i; j++) printf("%c", text[j]);
                printf("%s", base_color);
            } else {
                for (int j = start; j < i; j++) printf("%c", text[j]);
            }
            continue;
        }

        if (isdigit((unsigned char)text[i])) {
            printf("%s", num_color);
            while (i < len && (isalnum((unsigned char)text[i]) || text[i] == '.')) {
                printf("%c", text[i++]);
            }
            printf("%s", base_color);
            continue;
        }

        if (strchr("+-*/=<>!&|%^~?:", text[i])) {
            printf("%s%c%s", op_color, text[i++], base_color);
            continue;
        }
        if (strchr("()[]{}", text[i])) {
            printf("%s%c%s", struct_color, text[i++], base_color);
            continue;
        }

        printf("%c", text[i++]);
    }
    printf("%s", bright_colors[color_index]);
}


static char view_buf[8192];
static FILE *orig_file = NULL;
static FILE *index_file = NULL;
static bool is_fully_loaded = false;
static bool is_read_only = false;

static Line get_line_info(int row);
static void free_line(int row);
static void ensure_line_in_memory(int row);
static void ensure_line_capacity(int row, size_t needed);
int text_buffer_capacity = 0;
char current_filename[4096] = "";
char cmd_buffer[4096] = "";

static void oom(void) {
    fprintf(stderr, "\n\nOut of memory!\n");
    exit(1);
}

static Line get_line_info(int row) {
    if (text_buffer) return text_buffer[row];
    Line l;
    memset(&l, 0, sizeof(Line));
    if (index_file) {
        fseek_64(index_file, (int64_t)row * sizeof(Line), SEEK_SET);
        fread(&l, sizeof(Line), 1, index_file);
    }
    return l;
}

static const char* get_line_text(int row) {
    int clines = current_lines;
    if (row < 0 || row >= clines) return "";
    if (text_buffer && text_buffer[row].text) return text_buffer[row].text;
    Line l = get_line_info(row);
    if (l.text) return l.text;
    if (!orig_file) return "";
    fseek_64(orig_file, l.disk_offset, SEEK_SET);
    int to_read = l.length;
    if (to_read >= (int)sizeof(view_buf)) to_read = sizeof(view_buf) - 1;
    if (to_read > 0) fread(view_buf, 1, to_read, orig_file);
    view_buf[to_read] = '\0';
    return view_buf;
}

static void ensure_line_in_memory(int row) {
    if (is_read_only) return;
    if (is_fully_loaded) return;
    if (row < 0 || row >= current_lines) return;
    if (!get_line_text(row)) {
        text_buffer[row].capacity = get_line_info(row).length + 128;
        text_buffer[row].text = malloc(text_buffer[row].capacity);
        if (!get_line_text(row)) oom();
        if (get_line_info(row).length > 0 && orig_file) {
            fseek_64(orig_file, text_buffer[row].disk_offset, SEEK_SET);
            fread(text_buffer[row].text, 1, get_line_info(row).length, orig_file);
        }
        text_buffer[row].text[get_line_info(row).length] = '\0';
    }
}

static void ensure_line_capacity(int row, size_t needed) {
    ensure_line_in_memory(row);
    if (needed > (size_t)text_buffer[row].capacity) {
        size_t new_cap = text_buffer[row].capacity * 2;
        if (new_cap < needed) new_cap = needed;
        if (new_cap < 128) new_cap = 128;
        char *new_text = realloc(text_buffer[row].text, new_cap);
        if (!new_text) oom();
        text_buffer[row].text = new_text;
        text_buffer[row].capacity = new_cap;
    }
}

static void ensure_buffer_capacity(int needed) {
    if (needed > text_buffer_capacity) {
        int new_cap = text_buffer_capacity * 2;
        if (new_cap < needed) new_cap = needed;
        if (new_cap < 256) new_cap = 256;
        Line *new_buf = realloc(text_buffer, new_cap * sizeof(Line));
        if (!new_buf) oom();
        text_buffer = new_buf;
        text_buffer_capacity = new_cap;
    }
}

static void insert_empty_line(int row) {
    ensure_buffer_capacity(current_lines + 1);
    for (int i = current_lines; i > row; i--) {
        text_buffer[i] = text_buffer[i - 1];
    }
    text_buffer[row].text = malloc(128);
    if (!text_buffer[row].text) oom();
    text_buffer[row].text[0] = '\0';
    text_buffer[row].length = 0;
    text_buffer[row].capacity = 128;
    text_buffer[row].in_multiline_comment = 0;
    current_lines++;
}

static void free_line(int row) {
    if (text_buffer && text_buffer[row].text) {
        free(text_buffer[row].text);
        text_buffer[row].text = NULL;
    }
}

int cursor_r = 0, cursor_c = 0;
int row_offset = 0;
int mode = 0; /* 0: Normal, 1: Insert, 2: Command */
int cmd_len = 0;
bool running = true;
int screen_rows = 24;

static const char *bright_colors[] = {
    "\x1b[97m", /* Bright White */
    "\x1b[96m", /* Bright Cyan */
    "\x1b[92m", /* Bright Green */
    "\x1b[93m", /* Bright Yellow */
    "\x1b[95m", /* Bright Magenta */
    "\x1b[91m"  /* Bright Red */
};
#define NUM_BRIGHT_COLORS (sizeof(bright_colors)/sizeof(bright_colors[0]))
static int color_index = 0;
static int pushed_char = -1;
int screen_cols = 80;

#if !defined(_WIN32) && !defined(WIN32) && !defined(__MSDOS__) && !defined(__DOS__)
struct termios orig_termios;
#else
HANDLE hOut;
DWORD dwMode;
#endif



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
    if (ioctl(1, TIOCGWINSZ, &w) != -1 && w.ws_row > 0 && w.ws_col > 0) {
        screen_rows = w.ws_row;
        screen_cols = w.ws_col;
    } else {
        screen_rows = 24; screen_cols = 80;
    }
#endif
    if (screen_rows < 5) screen_rows = 24;
    if (screen_cols < 20) screen_cols = 80;
}

void reset_term(void) {
#if !defined(_WIN32) && !defined(WIN32) && !defined(__MSDOS__) && !defined(__DOS__)
    tcsetattr(0, TCSANOW, &orig_termios);
#endif
}

void init_term(void) {
#if defined(_WIN32) || defined(WIN32)
    hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    dwMode = 0;
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
                            } else if (seq3 == ';') {
                                char seq4, seq5;
                                if (read(0, &seq4, 1) == 1 && read(0, &seq5, 1) == 1) {
                                    if (seq4 == '5') {
                                        raw.c_cc[VMIN] = 1; raw.c_cc[VTIME] = 0; tcsetattr(0, TCSANOW, &raw);
                                        if (seq5 == 'H') return KEY_CTRL_HOME;
                                        if (seq5 == 'F') return KEY_CTRL_END;
                                    }
                                }
                            } else if (seq2 == '1') {
                                if (seq3 >= '1' && seq3 <= '5') {
                                    char seq4;
                                    if (read(0, &seq4, 1) == 1 && seq4 == '~') {
                                        raw.c_cc[VMIN] = 1; raw.c_cc[VTIME] = 0; tcsetattr(0, TCSANOW, &raw);
                                        switch(seq3) {
                                            case '1': return KEY_F1;
                                            case '2': return KEY_F2;
                                            case '3': return KEY_F3;
                                            case '4': return KEY_F4;
                                            case '5': return KEY_F5;
                                        }
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

void load_file(const char *filename) {
    if (text_buffer) {
        for (int i = 0; i < current_lines; i++) free_line(i);
        free(text_buffer); text_buffer = NULL;
    }
    if (orig_file) { fclose(orig_file); orig_file = NULL; }
    if (index_file) { fclose(index_file); index_file = NULL; }
    is_fully_loaded = false;
    is_read_only = false;
    current_lines = 0;
    text_buffer_capacity = 0;

    FILE *file = fopen(filename, "r");
    if (!file) {
        current_lines = 0;
        insert_empty_line(0);
        strncpy(current_filename, filename, 4095);
        current_filename[4095] = '\0';
        return;
    }

    fseek_64(file, 0, SEEK_END);
    int64_t file_size = ftell_64(file);
    fseek_64(file, 0, SEEK_SET);

    if (file_size < 64 * 1024 * 1024) {
        is_fully_loaded = true;
        ensure_buffer_capacity(1);
        char line_buf[4096];
        int in_multi = 0;
        while (fgets(line_buf, sizeof(line_buf), file)) {
            sanitize_ascii(line_buf);
            int len = (int)strlen(line_buf);
            while (len > 0 && (line_buf[len - 1] == '\n' || line_buf[len - 1] == '\r')) {
                line_buf[len - 1] = '\0';
                len--;
            }
            insert_empty_line(current_lines);
            ensure_line_capacity(current_lines - 1, len + 1);
            strcpy(text_buffer[current_lines - 1].text, line_buf);
            text_buffer[current_lines - 1].length = len;
            text_buffer[current_lines - 1].in_multiline_comment = in_multi;
            
            for (int i = 0; i < len; i++) {
                if (in_multi) {
                    if (line_buf[i] == '*' && i + 1 < len && line_buf[i+1] == '/') {
                        in_multi = 0; i++;
                    }
                } else {
                    if (line_buf[i] == '/' && i + 1 < len && line_buf[i+1] == '*') {
                        in_multi = 1; i++;
                    } else if (line_buf[i] == '/' && i + 1 < len && line_buf[i+1] == '/') {
                        break;
                    } else if (line_buf[i] == '"' || line_buf[i] == '\'') {
                        char q = line_buf[i++];
                        while (i < len) {
                            if (line_buf[i] == '\\' && i + 1 < len) i += 2;
                            else if (line_buf[i] == q) break;
                            else i++;
                        }
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
            int len = (int)strlen(line_buf);
            while (len > 0 && (line_buf[len - 1] == '\n' || line_buf[len - 1] == '\r')) {
                line_buf[len - 1] = '\0';
                len--;
            }
            if (!is_read_only && (current_lines + 1) * sizeof(Line) > 256 * 1024 * 1024) {
                is_read_only = true;
                index_file = tmpfile();
                if (index_file && text_buffer) {
                    fwrite(text_buffer, sizeof(Line), current_lines, index_file);
                }
                if (text_buffer) { free(text_buffer); text_buffer = NULL; }
            }
            if (!is_read_only) {
                ensure_buffer_capacity(current_lines + 1);
                text_buffer[current_lines].text = NULL;
                text_buffer[current_lines].disk_offset = current_offset;
                text_buffer[current_lines].length = len;
                text_buffer[current_lines].capacity = 0;
                text_buffer[current_lines].in_multiline_comment = 0;
            } else if (index_file) {
                Line l;
                memset(&l, 0, sizeof(Line));
                l.disk_offset = current_offset;
                l.length = len;
                l.in_multiline_comment = 0;
                fwrite(&l, sizeof(Line), 1, index_file);
            }
            current_lines++;
            current_offset = ftell_64(orig_file);
        }
    }
    if (current_lines == 0) {
        is_read_only = false;
        insert_empty_line(0);
    }
    strncpy(current_filename, filename, 4095);
    current_filename[4095] = '\0';
}

void save_file(void) {
    if (is_read_only) return;
    FILE *file = fopen(current_filename, "w");
    if (file) {
        for (int i = 0; i < current_lines; i++) {
            fprintf(file, "%s\n", get_line_text(i));
        }
        fclose(file);
    }
}

void fix_cursor(void) {
    int len;
    if (cursor_r < 0) cursor_r = 0;
    if (cursor_r >= current_lines) cursor_r = current_lines - 1;

    len = get_line_info(cursor_r).length;
    if (mode == 0) {
        if (cursor_c >= len && len > 0) cursor_c = len - 1;
    } else {
        if (cursor_c > len) cursor_c = len;
    }
    if (cursor_c < 0) cursor_c = 0;

    if (cursor_r < row_offset) row_offset = cursor_r;
    if (cursor_r >= row_offset + screen_rows - 1) row_offset = cursor_r - (screen_rows - 2);
}

static void format_filename_for_status(char *out_buf, const char *in_filename, int max_len) {
    if (!in_filename || !in_filename[0]) {
        strcpy(out_buf, "NEW");
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
        /* Extreme edge case: fallback to base filename */
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

void render_screen(void) {
    printf("\x1b[?25l"); /* Hide cursor momentarily to prevent flicker */
    printf("\x1b[H");
    
    printf("%s", bright_colors[color_index]);
    
    /* Track multiline comment state sequentially across visible lines */
    int mc_state = (row_offset < current_lines) ? get_line_info(row_offset).in_multiline_comment : 0;
    
    for (int i = 0; i < screen_rows - 1; i++) {
        int line_idx = row_offset + i;
        if (line_idx < current_lines) {
            printf("%s", bright_colors[color_index]);
            print_syntax_highlighted(get_line_text(line_idx), &mc_state, 0);
            printf("\x1b[K\r\n");
        } else {
            printf("\x1b[36m~%s\x1b[K\r\n", bright_colors[color_index]);
        }
    }
    
    /* Dynamic Status Line */
    printf("\x1b[47;30m");
    char left_status[4200];
    if (mode == 2) {
        snprintf(left_status, sizeof(left_status), ":%s", cmd_buffer);
    } else if (mode == 1) {
        snprintf(left_status, sizeof(left_status), "-- INSERT --");
    } else {
        char trunc_name[4096];
        format_filename_for_status(trunc_name, current_filename, screen_cols - 30);
        snprintf(left_status, sizeof(left_status), "\"%s\" %d:%d", trunc_name, cursor_r + 1, current_lines);
    }
    
    time_t rawtime;
    struct tm *timeinfo;
    char time_str[64];
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    strftime(time_str, sizeof(time_str), "%H:%M:%S", timeinfo);
    
    int ll = (int)strlen(left_status);
    int tl = (int)strlen(time_str);
    int pad = screen_cols - ll - tl - 2;
    if (pad < 1) pad = 1;
    
    printf("\x1b[%d;1H\x1b[47;30m\x1b[K", screen_rows);
    printf("%s", left_status);
    for (int i = 0; i < pad; i++) printf(" ");
    printf("%s", time_str);
    printf("\x1b[0m");
    
    /* Set Physical Cursor Position */
    if (mode == 2) {
        printf("\x1b[%d;%dH", screen_rows, cmd_len + 2);
    } else {
        printf("\x1b[%d;%dH", (cursor_r - row_offset) + 1, cursor_c + 1);
    }
    printf("\x1b[?25h");
    fflush(stdout);
}

void display_help(void) {
    printf("\x1b[2J\x1b[H"); 
    printf("--- vi Built-in Help ---\r\n\n");
    printf(" NORMAL MODE:\r\n");
    printf("   h,j,k,l / Arrows : Move cursor\r\n");
    printf("   Home / End       : Jump to start/end of line\r\n");
    printf("   PgUp / PgDn      : Page up / Page down\r\n");
    printf("   0, $             : Jump to start/end of line\r\n");
    printf("   i, a, I, A       : Enter Insert Mode\r\n");
    printf("   o, O             : Insert new line / Insert Mode\r\n");
    printf("   x, Del           : Delete character under cursor\r\n");
    printf("   dd               : Delete current line\r\n");
    printf("   :                : Enter Command Mode\r\n\n");
    printf(" INSERT MODE:\r\n");
    printf("   Esc              : Return to Normal Mode\r\n");
    printf("   Enter            : Split line\r\n");
    printf("   Backspace        : Delete char or merge lines\r\n\n");
    printf(" COMMAND MODE:\r\n");
    printf("   :w               : Save file\r\n");
    printf("   :w <file>        : Save to new file\r\n");
    printf("   :q, :q!          : Quit / Quit without saving\r\n");
    printf("   :wq, :x          : Save and Quit\r\n");
    printf("   :?, :h           : Show this help screen\r\n\n");

    printf("Press any key to return...");
    fflush(stdout);
    get_input(); /* Block execution until user returns */
}

void handle_normal(int c) {
    static int pending_d = 0;
    
    if (pending_d) {
        pending_d = 0;
        if (c == 'd') {
            if (current_lines > 1) {
                free_line(cursor_r);
                for (int i = cursor_r; i < current_lines - 1; i++) {
                    text_buffer[i] = text_buffer[i + 1];
                }
                current_lines--;
                if (cursor_r >= current_lines) cursor_r = current_lines - 1;
            } else {
                text_buffer[0].text[0] = '\0';
                text_buffer[0].length = 0;
                cursor_c = 0;
            }
            return;
        }
    }

    switch (c) {
        case 'h': case KEY_LEFT:  cursor_c--; break;
        case 'l': case KEY_RIGHT: cursor_c++; break;
        case 'j': case KEY_DOWN:  cursor_r++; break;
        case 'k': case KEY_UP:    cursor_r--; break;
        case KEY_HOME: case '0':  cursor_c = 0; break;
        case KEY_END:  case '$':  cursor_c = (int)get_line_info(cursor_r).length; break;
        case KEY_PGUP: cursor_r -= (screen_rows - 2); break;
        case KEY_PGDN: cursor_r += (screen_rows - 2); break;
        case KEY_CTRL_HOME: cursor_r = 0; cursor_c = 0; break;
        case KEY_CTRL_END: cursor_r = current_lines - 1; cursor_c = (int)get_line_info(cursor_r).length; break;
        
        case 'i': case KEY_INS: mode = 1; break;
        case 'a': cursor_c++; mode = 1; break;
        case 'I': cursor_c = 0; mode = 1; break;
        case 'A': cursor_c = (int)get_line_info(cursor_r).length; mode = 1; break;
        
        case 'x': case KEY_DEL:
            if (get_line_text(cursor_r)[cursor_c] != '\0') {
                for (int i = cursor_c; get_line_text(cursor_r)[i]; i++) {
                    text_buffer[cursor_r].text[i] = get_line_text(cursor_r)[i + 1];
                }
                text_buffer[cursor_r].length--;
            }
            break;
            
        case 'd': pending_d = 1; break;
        case 'o':
            insert_empty_line(cursor_r + 1);
            cursor_r++;
            mode = 1;
            cursor_c = 0;
            break;
        case 'O':
            insert_empty_line(cursor_r);
            mode = 1;
            cursor_c = 0;
            break;
            
        case ':':
            mode = 2;
            cmd_len = 0;
            cmd_buffer[0] = '\0';
            break;
    }
}

void handle_insert(int c) {
    if (c == 27) { /* Escape */
        mode = 0;
        if (cursor_c > 0) cursor_c--;
    } else if (c == KEY_UP) { cursor_r--; }
    else if (c == KEY_DOWN) { cursor_r++; }
    else if (c == KEY_LEFT) { cursor_c--; }
    else if (c == KEY_RIGHT) { cursor_c++; }
    else if (c == KEY_HOME) { cursor_c = 0; }
    else if (c == KEY_END) { cursor_c = (int)get_line_info(cursor_r).length; }
    else if (c == KEY_PGUP) { cursor_r -= (screen_rows - 2); }
    else if (c == KEY_PGDN) { cursor_r += (screen_rows - 2); }
    else if (c == KEY_CTRL_HOME) { cursor_r = 0; cursor_c = 0; }
    else if (c == KEY_CTRL_END) { cursor_r = current_lines - 1; cursor_c = (int)get_line_info(cursor_r).length; }
    else if (c == KEY_DEL) {
        if (get_line_text(cursor_r)[cursor_c] != '\0') {
            for (int i = cursor_c; get_line_text(cursor_r)[i]; i++) {
                text_buffer[cursor_r].text[i] = get_line_text(cursor_r)[i + 1];
            }
            text_buffer[cursor_r].length--;
        } else if (cursor_r < current_lines - 1) {
            int len = get_line_info(cursor_r).length;
            int next_len = get_line_info(cursor_r + 1).length;
            ensure_line_capacity(cursor_r, len + next_len + 1);
            memmove(&text_buffer[cursor_r].text[len], get_line_text(cursor_r + 1), next_len + 1);
            text_buffer[cursor_r].length += next_len;
            free_line(cursor_r + 1);
            for (int i = cursor_r + 1; i < current_lines - 1; i++) {
                text_buffer[i] = text_buffer[i + 1];
            }
            current_lines--;
        }
    } else if (c == 10 || c == 13) { /* Enter */
        insert_empty_line(cursor_r + 1);
        int rem_len = get_line_info(cursor_r).length - cursor_c;
        ensure_line_capacity(cursor_r + 1, rem_len + 1);
        memmove(text_buffer[cursor_r + 1].text, &get_line_text(cursor_r)[cursor_c], rem_len + 1);
        text_buffer[cursor_r + 1].length = rem_len;
        text_buffer[cursor_r].text[cursor_c] = '\0';
        text_buffer[cursor_r].length = cursor_c;
        cursor_r++;
        cursor_c = 0;
    } else if (c == 8 || c == 127) { /* Backspace */
        if (cursor_c > 0) {
            int len = get_line_info(cursor_r).length;
            for (int i = cursor_c; i <= len; i++) {
                text_buffer[cursor_r].text[i - 1] = get_line_text(cursor_r)[i];
            }
            text_buffer[cursor_r].length--;
            cursor_c--;
        } else if (cursor_r > 0) {
            int prev_len = get_line_info(cursor_r - 1).length;
            int cur_len = get_line_info(cursor_r).length;
            ensure_line_capacity(cursor_r - 1, prev_len + cur_len + 1);
            strcat(text_buffer[cursor_r - 1].text, get_line_text(cursor_r));
            text_buffer[cursor_r - 1].length += cur_len;
            free_line(cursor_r);
            for (int i = cursor_r; i < current_lines - 1; i++) {
                text_buffer[i] = text_buffer[i + 1];
            }
            current_lines--;
            cursor_r--;
            cursor_c = prev_len;
        }
    } else if (c >= 32 && c <= 126) { /* Standard ASCII Characters */
        int len = get_line_info(cursor_r).length;
        ensure_line_capacity(cursor_r, len + 2);
        for (int i = len; i >= cursor_c; i--) {
            text_buffer[cursor_r].text[i + 1] = get_line_text(cursor_r)[i];
        }
        text_buffer[cursor_r].text[cursor_c] = (char)c;
        text_buffer[cursor_r].length++;
        cursor_c++;
    } else if (c == KEY_F1) {
        display_help();
    } else if (c == KEY_F2) {
        if (current_filename[0]) save_file();
        else {
            mode = 2;
            strcpy(cmd_buffer, "w ");
            cmd_len = 2;
        }
    } else if (c == KEY_F3) {
        mode = 2;
        strcpy(cmd_buffer, "load ");
        cmd_len = 5;
    } else if (c == KEY_F4) {
        running = false;
    }
}

void handle_command(int c) {
    if (c == 27) {
        mode = 0;
    } else if (c == 10 || c == 13) {
        if (strcmp(cmd_buffer, "w") == 0) save_file();
        else if (strncmp(cmd_buffer, "w ", 2) == 0) {
            char new_file[4096];
            strncpy(new_file, cmd_buffer + 2, 4096 - 1);
            new_file[4096 - 1] = '\0';
            strcpy(current_filename, new_file);
            save_file();
        }
        else if (strcmp(cmd_buffer, "q") == 0 || strcmp(cmd_buffer, "q!") == 0) running = false;
        else if (strcmp(cmd_buffer, "wq") == 0 || strcmp(cmd_buffer, "x") == 0) {
            save_file();
            running = false;
        }
        else if (strncmp(cmd_buffer, "load ", 5) == 0) {
            char new_file[4096];
            strncpy(new_file, cmd_buffer + 5, 4096 - 1);
            new_file[4096 - 1] = '\0';
            strcpy(current_filename, new_file);
            load_file(current_filename);
        }
        else if (strcmp(cmd_buffer, "color") == 0) {
            color_index = (color_index + 1) % NUM_BRIGHT_COLORS;
        }
        else if (strncmp(cmd_buffer, "color ", 6) == 0) {
            char new_color[4096];
            strncpy(new_color, cmd_buffer + 6, 4096 - 1);
            new_color[4096 - 1] = '\0';
            if (strcmp(new_color, "white") == 0) color_index = 0;
            else if (strcmp(new_color, "cyan") == 0) color_index = 1;
            else if (strcmp(new_color, "green") == 0) color_index = 2;
            else if (strcmp(new_color, "yellow") == 0) color_index = 3;
            else if (strcmp(new_color, "magenta") == 0) color_index = 4;
            else if (strcmp(new_color, "red") == 0) color_index = 5;
        }
        else if (strcmp(cmd_buffer, "?") == 0 || strcmp(cmd_buffer, "h") == 0) display_help();
        mode = 0;
    } else if (c == 8 || c == 127) {
        if (cmd_len > 0) cmd_buffer[--cmd_len] = '\0';
        else mode = 0;
    } else if (c >= 32 && c <= 126 && cmd_len < 4096 - 1) {
        cmd_buffer[cmd_len++] = (char)c;
        cmd_buffer[cmd_len] = '\0';
    }
}

void exit_editor(void) {
    printf("\x1b[2J\x1b[H\x1b[?25h"); /* Restore Screen bounds and physical cursor pointer */
    fflush(stdout);
    reset_term();
}

int main(int argc, char *argv[]) {
    setvbuf(stdout, NULL, _IOFBF, 65536);
    
    int c;

    if (argc > 1) {
        load_file(argv[1]);
    } else {
        current_lines = 1;
        insert_empty_line(0);
    }
    
    init_term();
    printf("\x1b[2J\x1b[H"); 
    
    while (running) {
        get_terminal_size(); /* Dynamically re-read layout per user iteration */
        fix_cursor();
        render_screen();
        
        c = get_input();
        if (c == 0 || c == KEY_TIMEOUT) continue;
        
        if (mode == 0) handle_normal(c);
        else if (mode == 1) handle_insert(c);
        else if (mode == 2) handle_command(c);
    }
    
    exit_editor();
    return 0;
}