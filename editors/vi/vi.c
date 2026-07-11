/*
 *
 * VERSION: 3.0.0
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
#include <stdlib.h>
#include <string.h>
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

#define MAX_VI_LINES 1000
#define MAX_VI_LENGTH 255

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

char text_buffer[MAX_VI_LINES][MAX_VI_LENGTH];
char current_filename[MAX_VI_LENGTH] = "";
char cmd_buffer[MAX_VI_LENGTH] = "";

int current_lines = 0;
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
static bool sel_active = false;
static int sel_start_r = 0, sel_start_c = 0;
static int sel_end_r = 0, sel_end_c = 0;

void get_terminal_size(void) {
#if defined(_WIN32) || defined(WIN32)
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        screen_rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    } else {
        screen_rows = 24;
    }
#elif defined(__MSDOS__) || defined(__DOS__)
    screen_rows = 25; /* DOS text-mode standard */
#else
    struct winsize w;
    if (ioctl(1, TIOCGWINSZ, &w) != -1 && w.ws_row > 0) {
        screen_rows = w.ws_row;
    } else {
        screen_rows = 24;
    }
#endif
    if (screen_rows < 5) screen_rows = 24;
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

static void get_sel_bounds(int *r1, int *c1, int *r2, int *c2) {
    if (sel_start_r < sel_end_r || (sel_start_r == sel_end_r && sel_start_c <= sel_end_c)) {
        *r1 = sel_start_r; *c1 = sel_start_c;
        *r2 = sel_end_r; *c2 = sel_end_c;
    } else {
        *r1 = sel_end_r; *c1 = sel_end_c;
        *r2 = sel_start_r; *c2 = sel_start_c;
    }
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
    FILE *file = fopen(filename, "r");
    current_lines = 0;
    if (file) {
        while (current_lines < MAX_VI_LINES && fgets(text_buffer[current_lines], MAX_VI_LENGTH, file)) {
            sanitize_ascii(text_buffer[current_lines]);
            
        int len = (int)strlen(text_buffer[current_lines]);
            while (len > 0 && (text_buffer[current_lines][len - 1] == '\n' || text_buffer[current_lines][len - 1] == '\r')) {
                text_buffer[current_lines][len - 1] = '\0';
                len--;
            }
            current_lines++;
        }
        fclose(file);
    }
    if (current_lines == 0) {
        text_buffer[0][0] = '\0';
        current_lines = 1;
    }
    strncpy(current_filename, filename, MAX_VI_LENGTH - 1);
    current_filename[MAX_VI_LENGTH - 1] = '\0';
}

void save_file(void) {
    FILE *file = fopen(current_filename, "w");
    if (file) {
        for (int i = 0; i < current_lines; i++) {
            fprintf(file, "%s\n", text_buffer[i]);
        }
        fclose(file);
    }
}

void fix_cursor(void) {
    int len;
    if (cursor_r < 0) cursor_r = 0;
    if (cursor_r >= current_lines) cursor_r = current_lines - 1;

    len = (int)strlen(text_buffer[cursor_r]);
    if (mode == 0) {
        if (cursor_c >= len && len > 0) cursor_c = len - 1;
    } else {
        if (cursor_c > len) cursor_c = len;
    }
    if (cursor_c < 0) cursor_c = 0;

    if (cursor_r < row_offset) row_offset = cursor_r;
    if (cursor_r >= row_offset + screen_rows - 1) row_offset = cursor_r - (screen_rows - 2);
}

void render_screen(void) {
    printf("\x1b[?25l"); /* Hide cursor momentarily to prevent flicker */
    printf("\x1b[H");
    
    printf("%s", bright_colors[color_index]);
    
    for (int i = 0; i < screen_rows - 1; i++) {
        int line_idx = row_offset + i;
        if (line_idx < current_lines) {
            printf("%s", bright_colors[color_index]);
            for (int j = 0; j < (int)strlen(text_buffer[line_idx]); j++) {
                int in_sel = 0;
                if (sel_active) {
                    int r1, c1, r2, c2;
                    get_sel_bounds(&r1, &c1, &r2, &c2);
                    if (line_idx > r1 && line_idx < r2) in_sel = 1;
                    else if (line_idx == r1 && line_idx == r2 && j >= c1 && j < c2) in_sel = 1;
                    else if (line_idx == r1 && line_idx < r2 && j >= c1) in_sel = 1;
                    else if (line_idx == r2 && line_idx > r1 && j < c2) in_sel = 1;
                }
                if (in_sel) {
                    printf("\x1b[47;30m%c%s", text_buffer[line_idx][j], bright_colors[color_index]);
                } else {
                    printf("%c", text_buffer[line_idx][j]);
                }
            }
            printf("\x1b[K\r\n");
        } else {
            printf("\x1b[36m~%s\x1b[K\r\n", bright_colors[color_index]);
        }
    }
    
    /* Dynamic Status Line */
    printf("\x1b[47;30m");
    if (mode == 2) {
        printf(":%s\x1b[K", cmd_buffer);
    } else if (mode == 1) {
        printf("-- INSERT --\x1b[K");
    } else {
        printf("\"%s\" %d:%d\x1b[K", current_filename[0] ? current_filename : "NEW", cursor_r + 1, current_lines);
    }
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
                for (int i = cursor_r; i < current_lines - 1; i++) {
                    strcpy(text_buffer[i], text_buffer[i + 1]);
                }
                current_lines--;
                if (cursor_r >= current_lines) cursor_r = current_lines - 1;
            } else {
                text_buffer[0][0] = '\0';
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
        case KEY_END:  case '$':  cursor_c = (int)strlen(text_buffer[cursor_r]); break;
        case KEY_PGUP: cursor_r -= (screen_rows - 2); break;
        case KEY_PGDN: cursor_r += (screen_rows - 2); break;
        
        case 'i': case KEY_INS: mode = 1; break;
        case 'a': cursor_c++; mode = 1; break;
        case 'I': cursor_c = 0; mode = 1; break;
        case 'A': cursor_c = (int)strlen(text_buffer[cursor_r]); mode = 1; break;
        
        case 'x': case KEY_DEL:
            if (text_buffer[cursor_r][cursor_c] != '\0') {
                for (int i = cursor_c; text_buffer[cursor_r][i]; i++) {
                    text_buffer[cursor_r][i] = text_buffer[cursor_r][i + 1];
                }
            }
            break;
            
        case 'd': pending_d = 1; break;
        case 'o':
            if (current_lines < MAX_VI_LINES) {
                for (int i = current_lines; i > cursor_r + 1; i--) {
                    strcpy(text_buffer[i], text_buffer[i - 1]);
                }
                cursor_r++;
                text_buffer[cursor_r][0] = '\0';
                current_lines++;
                mode = 1;
                cursor_c = 0;
            }
            break;
        case 'O':
            if (current_lines < MAX_VI_LINES) {
                for (int i = current_lines; i > cursor_r; i--) {
                    strcpy(text_buffer[i], text_buffer[i - 1]);
                }
                text_buffer[cursor_r][0] = '\0';
                current_lines++;
                mode = 1;
                cursor_c = 0;
            }
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
    else if (c == KEY_END) { cursor_c = (int)strlen(text_buffer[cursor_r]); }
    else if (c == KEY_PGUP) { cursor_r -= (screen_rows - 2); }
    else if (c == KEY_PGDN) { cursor_r += (screen_rows - 2); }
    else if (c == KEY_DEL) {
        if (text_buffer[cursor_r][cursor_c] != '\0') {
            for (int i = cursor_c; text_buffer[cursor_r][i]; i++) {
                text_buffer[cursor_r][i] = text_buffer[cursor_r][i + 1];
            }
        } else if (cursor_r < current_lines - 1) {
            int len = (int)strlen(text_buffer[cursor_r]);
            if (len + (int)strlen(text_buffer[cursor_r + 1]) < MAX_VI_LENGTH) {
                memmove(&text_buffer[cursor_r][len], text_buffer[cursor_r + 1], strlen(text_buffer[cursor_r + 1]) + 1);
                for (int i = cursor_r + 1; i < current_lines - 1; i++) {
                    memmove(text_buffer[i], text_buffer[i + 1], strlen(text_buffer[i + 1]) + 1);
                }
                current_lines--;
            }
        }
    } else if (c == 10 || c == 13) { /* Enter */
        if (current_lines < MAX_VI_LINES) {
            for (int i = current_lines; i > cursor_r; i--) {
                memmove(text_buffer[i], text_buffer[i - 1], strlen(text_buffer[i - 1]) + 1);
            }
            memmove(text_buffer[cursor_r + 1], &text_buffer[cursor_r][cursor_c], strlen(&text_buffer[cursor_r][cursor_c]) + 1);
            text_buffer[cursor_r][cursor_c] = '\0';
            current_lines++;
            cursor_r++;
            cursor_c = 0;
        }
    } else if (c == 8 || c == 127) { /* Backspace */
        if (cursor_c > 0) {
            int len = (int)strlen(text_buffer[cursor_r]);
            for (int i = cursor_c; i <= len; i++) {
                text_buffer[cursor_r][i - 1] = text_buffer[cursor_r][i];
            }
            cursor_c--;
        } else if (cursor_r > 0) {
            int prev_len = (int)strlen(text_buffer[cursor_r - 1]);
            if (prev_len + (int)strlen(text_buffer[cursor_r]) < MAX_VI_LENGTH) {
                strcat(text_buffer[cursor_r - 1], text_buffer[cursor_r]);
                for (int i = cursor_r; i < current_lines - 1; i++) {
                    strcpy(text_buffer[i], text_buffer[i + 1]);
                }
                current_lines--;
                cursor_r--;
                cursor_c = prev_len;
            }
        }
    } else if (c >= 32 && c <= 126) { /* Standard ASCII Characters */
        int len = (int)strlen(text_buffer[cursor_r]);
        if (len < MAX_VI_LENGTH - 1) {
            for (int i = len; i >= cursor_c; i--) {
                text_buffer[cursor_r][i + 1] = text_buffer[cursor_r][i];
            }
            text_buffer[cursor_r][cursor_c] = (char)c;
            cursor_c++;
        }
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
            char new_file[MAX_VI_LENGTH];
            strncpy(new_file, cmd_buffer + 2, MAX_VI_LENGTH - 1);
            new_file[MAX_VI_LENGTH - 1] = '\0';
            strcpy(current_filename, new_file);
            save_file();
        }
        else if (strcmp(cmd_buffer, "q") == 0 || strcmp(cmd_buffer, "q!") == 0) running = false;
        else if (strcmp(cmd_buffer, "wq") == 0 || strcmp(cmd_buffer, "x") == 0) {
            save_file();
            running = false;
        }
        else if (strncmp(cmd_buffer, "load ", 5) == 0) {
            char new_file[MAX_VI_LENGTH];
            strncpy(new_file, cmd_buffer + 5, MAX_VI_LENGTH - 1);
            new_file[MAX_VI_LENGTH - 1] = '\0';
            strcpy(current_filename, new_file);
            load_file(current_filename);
        }
        else if (strcmp(cmd_buffer, "color") == 0) {
            color_index = (color_index + 1) % NUM_BRIGHT_COLORS;
        }
        else if (strncmp(cmd_buffer, "color ", 6) == 0) {
            char new_color[MAX_VI_LENGTH];
            strncpy(new_color, cmd_buffer + 6, MAX_VI_LENGTH - 1);
            new_color[MAX_VI_LENGTH - 1] = '\0';
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
    } else if (c >= 32 && c <= 126 && cmd_len < MAX_VI_LENGTH - 1) {
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
    int c;

    if (argc > 1) {
        load_file(argv[1]);
    } else {
        current_lines = 1;
        text_buffer[0][0] = '\0';
    }
    
    init_term();
    printf("\x1b[2J\x1b[H"); 
    
    while (running) {
        get_terminal_size(); /* Dynamically re-read layout per user iteration */
        fix_cursor();
        render_screen();
        
        c = get_input();
        if (c == 0) continue;
        
        if (mode == 0) handle_normal(c);
        else if (mode == 1) handle_insert(c);
        else if (mode == 2) handle_command(c);
    }
    
    exit_editor();
    return 0;
}