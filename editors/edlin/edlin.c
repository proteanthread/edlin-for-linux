/* 
 *
 * Standalone DOS EDLIN Clone
 * VERSION: 4.1.0
 * LICENSE: MIT License
 * COPYLEFT: BASIC++ Community
 *
 * edlin.c - based on the original MSDOS line editor
 *
 *    The editor presents a '*' prompt and accepts single-letter
 *    commands: l(ist), i(nsert), d(elete), e(nd/save), q(uit),
 *    c(opy), m(ove), p(age), s(earch), r(eplace), t(ransfer),
 *    w(rite), a(ppend), h/?. Numeric input edits that specific line.
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
#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

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
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif


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

/* =====================================================================
 * Internal State
 * ===================================================================== */

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


static Line *edlin_buffer = NULL;
static int edlin_buffer_capacity = 0;
static int  edlin_line_count = 0;
static int  edlin_page_pos   = 0;
static char edlin_filename[4096] = "";

static FILE *orig_file = NULL;
static FILE *index_file = NULL;
static char view_buf[8192];
static bool is_fully_loaded = false;
static bool is_read_only = false;

static Line get_line_info(int row) {
    if (edlin_buffer) return edlin_buffer[row];
    Line l;
    memset(&l, 0, sizeof(Line));
    if (index_file) {
        fseek_64(index_file, (int64_t)row * (int64_t)sizeof(Line), SEEK_SET);
        fread(&l, sizeof(Line), 1, index_file);
    }
    return l;
}

static const char* get_line_text(int row) {
    int clines = edlin_line_count;
    if (row < 0 || row >= clines) return "";
    if (edlin_buffer && edlin_buffer[row].text) return edlin_buffer[row].text;
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
    if (row < 0 || row >= edlin_line_count) return;
    if (!edlin_buffer[row].text) {
        edlin_buffer[row].capacity = get_line_info(row).length + 128;
        edlin_buffer[row].text = malloc((size_t)edlin_buffer[row].capacity);
        if (!edlin_buffer[row].text) { fprintf(stderr, "OOM\n"); exit(1); }
        if (get_line_info(row).length > 0 && orig_file) {
            fseek_64(orig_file, get_line_info(row).disk_offset, SEEK_SET);
            fread(edlin_buffer[row].text, 1, (size_t)get_line_info(row).length, orig_file);
        }
        edlin_buffer[row].text[get_line_info(row).length] = '\0';
    }
}


static void oom(void) {
    fprintf(stderr, "\n\nOut of memory!\n");
    exit(1);
}

static void ensure_line_capacity(int row, size_t needed) {
    ensure_line_in_memory(row);
    if (needed > (size_t)edlin_buffer[row].capacity) {
        size_t new_cap = (size_t)edlin_buffer[row].capacity * 2;
        if (new_cap < needed) new_cap = needed;
        if (new_cap < 128) new_cap = 128;
        char *new_text = realloc(edlin_buffer[row].text, new_cap);
        if (!new_text) oom();
        edlin_buffer[row].text = new_text;
        if (new_cap > (size_t)edlin_buffer[row].capacity) {
            memset(&new_text[edlin_buffer[row].capacity], 0, new_cap - (size_t)edlin_buffer[row].capacity);
        }
        edlin_buffer[row].capacity = (int)new_cap;
    }
}

static void ensure_buffer_capacity(int needed) {
    if (needed > edlin_buffer_capacity) {
        int new_cap = edlin_buffer_capacity * 2;
        if (new_cap < needed) new_cap = needed;
        if (new_cap < 256) new_cap = 256;
        Line *new_buf = realloc(edlin_buffer, (size_t)new_cap * sizeof(Line));
        if (!new_buf) oom();
        memset(&new_buf[edlin_buffer_capacity], 0, (size_t)(new_cap - edlin_buffer_capacity) * sizeof(Line));
        edlin_buffer = new_buf;
        edlin_buffer_capacity = new_cap;
    }
}

static void free_line(int row) {
    if (edlin_buffer[row].text) {
        free(edlin_buffer[row].text);
        edlin_buffer[row].text = NULL;
    }
}

static void insert_empty_line_at(int row) {
    ensure_buffer_capacity(edlin_line_count + 1);
    for (int i = edlin_line_count; i > row; i--) {
        edlin_buffer[i] = edlin_buffer[i - 1];
    }
    edlin_buffer[row].text = malloc(128);
    if (!edlin_buffer[row].text) oom();
    edlin_buffer[row].text[0] = '\0';
    edlin_buffer[row].disk_offset = 0;
    edlin_buffer[row].length = 0;
    edlin_buffer[row].capacity = 128;
    edlin_buffer[row].in_multiline_comment = 0;
    edlin_line_count++;
}


/* =====================================================================
 * Syntax Highlighting
 * ===================================================================== */

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

static void print_syntax_highlighted(const char *text, int *in_multiline_comment, int is_selected) {
    if (!text) return;
    int len = (int)strlen(text);
    int i = 0;
    
    // Base color for normal text
    const char *base_color = is_selected ? "\x1b[7m" : "\x1b[0m";
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
    printf("\x1b[0m");
    fflush(stdout);
}


/* =====================================================================
 * Output Helpers
 * ===================================================================== */

static void edlin_print(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    fflush(stdout);
}

static int get_edlin_page_size(void)
{
    int height = 23;
#if defined(_WIN32) || defined(WIN32)
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        height = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    }
#else
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_row > 0) {
        height = w.ws_row;
    }
#endif
    /* Paginate with (height - 2) to leave room for the prompt. */
    if (height > 5) {
        return height - 2;
    }
    return 23; /* Fallback default */
}

/* =====================================================================
 * Input Helpers
 * ===================================================================== */

static char *edlin_read_line(char *buf, size_t max_len)
{
    char *ret = fgets(buf, (int)max_len, stdin);
    if (ret != NULL) {
        
    }
    return ret;
}

static int edlin_get_int_prompt(const char *prompt)
{
    char input[4096];
    edlin_print("%s", prompt);
    if (edlin_read_line(input, 4096) == NULL) {
        return 0;
    }
    char *endptr = NULL;
    long val = strtol(input, &endptr, 10);
    if (endptr == input) {
        return 0;  /* No digits found */
    }
    return (int)val;
}

static void edlin_get_string_prompt(const char *prompt, char *buffer)
{
    edlin_print("%s", prompt);
    if (edlin_read_line(buffer, 4096) != NULL) {
        buffer[strcspn(buffer, "\n")] = '\0';
    } else {
        buffer[0] = '\0';
    }
}

/* =====================================================================
 * Core Editor Functions
 * ===================================================================== */

static void display_edlin_help(void)
{
    int page_size = get_edlin_page_size();
    edlin_print("\nedlin - Line Editor v4.1.0\n");
    edlin_print("Available Commands:\n");
    edlin_print("  [line] - Edit a specific line (enter line number)\n");
    edlin_print("  a      - Append lines from disk into memory\n");
    edlin_print("  c      - Copy lines\n");
    edlin_print("  d      - Delete line(s)\n");
    edlin_print("  e      - End editing (Save and Exit)\n");
    edlin_print("  h, ?   - Display this help message\n");
    edlin_print("  i      - Insert lines at the end of the buffer\n");
    edlin_print("  l      - List all lines currently in the buffer\n");
    edlin_print("  m      - Move lines\n");
    edlin_print("  p      - Page display (%d lines at a time)\n", page_size);
    edlin_print("  q      - Quit without saving\n");
    edlin_print("  r      - Replace text\n");
    edlin_print("  s      - Search text\n");
    edlin_print("  t      - Transfer (merge) another file\n");
    edlin_print("  w      - Write lines to disk\n\n");
}

static void load_edlin_file(const char *filename)
{
    if (edlin_buffer) {
        for (int i = 0; i < edlin_line_count; i++) free_line(i);
        free(edlin_buffer); edlin_buffer = NULL;
    }
    if (orig_file) { fclose(orig_file); orig_file = NULL; }
    if (index_file) { fclose(index_file); index_file = NULL; }
    is_fully_loaded = false;
    is_read_only = false;
    edlin_line_count = 0;
    edlin_buffer_capacity = 0;

    FILE *file = fopen(filename, "rb");
    if (!file) {
        is_fully_loaded = true;
        edlin_print("New file\n");
        snprintf(edlin_filename, 4096, "%s", filename);
        return;
    }

    fseek_64(file, 0, SEEK_END);
    int64_t file_size = ftell_64(file);
    fseek_64(file, 0, SEEK_SET);

    if (file_size < 64 * 1024 * 1024) {
        is_fully_loaded = true;
        ensure_buffer_capacity(1);
        char line_buf[4096];
        int in_comment = 0;
        while (fgets(line_buf, sizeof(line_buf), file)) {
            sanitize_ascii(line_buf);
            size_t len = strlen(line_buf);
            while (len > 0 && (line_buf[len - 1] == '\n' || line_buf[len - 1] == '\r')) {
                line_buf[len - 1] = '\0'; len--;
            }
            insert_empty_line_at(edlin_line_count);
            ensure_line_capacity(edlin_line_count - 1, len + 1);
            strcpy(edlin_buffer[edlin_line_count - 1].text, line_buf);
            edlin_buffer[edlin_line_count - 1].length = (int)len;
            edlin_buffer[edlin_line_count - 1].in_multiline_comment = in_comment;
            
            int i = 0;
            while (i < (int)len) {
                if (in_comment) {
                    if (line_buf[i] == '*' && i + 1 < (int)len && line_buf[i+1] == '/') {
                        in_comment = 0; i += 2;
                    } else i++;
                } else {
                    if (line_buf[i] == '/' && i + 1 < (int)len && line_buf[i+1] == '*') {
                        in_comment = 1; i += 2;
                    } else if (line_buf[i] == '"' || line_buf[i] == '\'') {
                        char quote = line_buf[i++];
                        while (i < (int)len) {
                            if (line_buf[i] == '\\' && i + 1 < (int)len) i += 2;
                            else if (line_buf[i] == quote) { i++; break; }
                            else i++;
                        }
                    } else if (line_buf[i] == '/' && i + 1 < (int)len && line_buf[i+1] == '/') {
                        break;
                    } else {
                        i++;
                    }
                }
            }
        }
        fclose(file);
        edlin_print("End of input file\n");
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
            if (!is_read_only && (size_t)(edlin_line_count + 1) * sizeof(Line) > 256 * 1024 * 1024) {
                is_read_only = true;
                index_file = tmpfile();
                if (index_file && edlin_buffer) {
                    fwrite(edlin_buffer, sizeof(Line), (size_t)edlin_line_count, index_file);
                }
                if (edlin_buffer) { free(edlin_buffer); edlin_buffer = NULL; }
            }
            
            if (!is_read_only) {
                ensure_buffer_capacity(edlin_line_count + 1);
                edlin_buffer[edlin_line_count].text = NULL;
                edlin_buffer[edlin_line_count].disk_offset = current_offset;
                edlin_buffer[edlin_line_count].length = (int)len;
                edlin_buffer[edlin_line_count].capacity = 0;
            } else if (index_file) {
                Line l;
                memset(&l, 0, sizeof(Line));
                l.disk_offset = current_offset;
                l.length = (int)len;
                fwrite(&l, sizeof(Line), 1, index_file);
            }
            edlin_line_count++;
            current_offset = ftell_64(orig_file);
        }
        edlin_print("End of input file\n");
    }
    snprintf(edlin_filename, 4096, "%s", filename);
}

static void save_edlin_file(void)
{
    if (is_read_only) {
        edlin_print("Error: File is too large, opened in read-only mode.\n");
        return;
    }
    if (edlin_filename[0] == '\0') {
        edlin_print("Error: No filename specified.\n");
        return;
    }
    FILE *file = fopen(edlin_filename, "w");
    if (file == NULL) {
        edlin_print("Error: Cannot save file.\n");
        return;
    }
    for (int i = 0; i < edlin_line_count; i++) {
        fprintf(file, "%s\n", get_line_text(i));
    }
    fclose(file);
}

static void list_edlin_lines(void)
{
    for (int i = 0; i < edlin_line_count; i++) {
        edlin_print("%d: ", i + 1);
        int mc = get_line_info(i).in_multiline_comment;
        print_syntax_highlighted(get_line_text(i), &mc, 0);
        edlin_print("\n");
    }
}

static void insert_edlin_line(void)
{
    char input[4096];

    while (1) {
        edlin_print("%d:*", edlin_line_count + 1);
        if (edlin_read_line(input, 4096) == NULL) {
            break;
        }
        input[strcspn(input, "\n")] = '\0';
        if (input[strcspn(input, "\r")] == '\r') input[strcspn(input, "\r")] = '\0';
        
        if (strcmp(input, ".") == 0) {
            break;
        }
        insert_empty_line_at(edlin_line_count);
        size_t len = strlen(input);
        ensure_line_capacity(edlin_line_count - 1, len + 1);
        strcpy(edlin_buffer[edlin_line_count - 1].text, input);
        edlin_buffer[edlin_line_count - 1].length = (int)len;
    }
}

static void delete_edlin_line(void)
{
    if (edlin_line_count == 0) {
        edlin_print("Error: Buffer is empty.\n");
        return;
    }

    int index = edlin_get_int_prompt("Line to delete: ") - 1;
    if (index >= 0 && index < edlin_line_count) {
        free_line(index);
        for (int i = index; i < edlin_line_count - 1; i++) {
            edlin_buffer[i] = edlin_buffer[i + 1];
        }
        edlin_line_count--;
        memset(&edlin_buffer[edlin_line_count], 0, sizeof(Line));
        edlin_print("Line deleted.\n");
    } else {
        edlin_print("Error: Invalid line number.\n");
    }
}

static void edit_edlin_line(int index)
{
    char input[4096];

    if (index >= 0 && index < edlin_line_count) {
        edlin_print("%d: ", index + 1);
        int mc = get_line_info(index).in_multiline_comment;
        print_syntax_highlighted(get_line_text(index), &mc, 0);
        edlin_print("\n");
        edlin_print("%d:*", index + 1);
        if (edlin_read_line(input, 4096) != NULL) {
            input[strcspn(input, "\n")] = '\0';
            if (input[strcspn(input, "\r")] == '\r') input[strcspn(input, "\r")] = '\0';
            
            if (strlen(input) > 0) {
                size_t len = strlen(input);
                ensure_line_capacity(index, len + 1);
                strcpy(edlin_buffer[index].text, input);
                edlin_buffer[index].length = (int)len;
            }
        }
    } else {
        edlin_print("Error: Invalid line number.\n");
    }
}

static void copy_edlin_lines(void)
{
    int start = edlin_get_int_prompt("Start line: ") - 1;
    int end   = edlin_get_int_prompt("End line: ") - 1;
    int dest  = edlin_get_int_prompt("Destination line: ") - 1;

    if (start < 0 || end >= edlin_line_count || start > end ||
        dest < 0 || dest > edlin_line_count) {
        edlin_print("Error: Invalid range.\n");
        return;
    }
    if (dest >= start && dest <= end) {
        edlin_print("Error: Cannot copy into source range.\n");
        return;
    }

    int count = end - start + 1;
    ensure_buffer_capacity(edlin_line_count + count);

    for (int i = edlin_line_count - 1; i >= dest; i--) {
        edlin_buffer[i + count] = edlin_buffer[i];
    }

    int src_offset = (dest < start) ? count : 0;
    for (int j = 0; j < count; j++) {
        int src_idx = start + src_offset + j;
        size_t len = (size_t)get_line_info(src_idx).length;
        edlin_buffer[dest + j].text = malloc(len + 1);
        if (!edlin_buffer[dest + j].text) oom();
        strcpy(edlin_buffer[dest + j].text, get_line_text(src_idx));
        edlin_buffer[dest + j].length = (int)len;
        edlin_buffer[dest + j].capacity = (int)(len + 1);
    }
    edlin_line_count += count;
    edlin_print("%d lines copied.\n", count);
}

static void move_edlin_lines(void)
{
    int start = edlin_get_int_prompt("Start line: ") - 1;
    int end   = edlin_get_int_prompt("End line: ") - 1;
    int dest  = edlin_get_int_prompt("Destination line: ") - 1;

    if (start < 0 || end >= edlin_line_count || start > end ||
        dest < 0 || dest > edlin_line_count) {
        edlin_print("Error: Invalid range.\n");
        return;
    }
    if (dest >= start && dest <= end) {
        edlin_print("Error: Cannot move into source range.\n");
        return;
    }

    int count = end - start + 1;
    ensure_buffer_capacity(edlin_line_count + count);

    for (int i = edlin_line_count - 1; i >= dest; i--) {
        edlin_buffer[i + count] = edlin_buffer[i];
    }

    int del_start;
    if (dest < start) {
        for (int j = 0; j < count; j++) {
            edlin_buffer[dest + j] = edlin_buffer[start + count + j];
        }
        del_start = start + count;
    } else {
        for (int j = 0; j < count; j++) {
            edlin_buffer[dest + j] = edlin_buffer[start + j];
        }
        del_start = start;
    }

    for (int i = del_start; i < edlin_line_count; i++) {
        edlin_buffer[i] = edlin_buffer[i + count];
    }
    edlin_print("%d lines moved.\n", count);
}

static void page_edlin_display(void)
{
    if (edlin_page_pos >= edlin_line_count) {
        edlin_page_pos = 0;
    }
    int page_size = get_edlin_page_size();
    int end = edlin_page_pos + page_size;
    if (end > edlin_line_count) {
        end = edlin_line_count;
    }

    for (int i = edlin_page_pos; i < end; i++) {
        edlin_print("%d: ", i + 1);
        int mc = get_line_info(i).in_multiline_comment;
        print_syntax_highlighted(get_line_text(i), &mc, 0);
        edlin_print("\n");
    }
    edlin_page_pos = end;
}

static void search_edlin_text(void)
{
    int start = edlin_get_int_prompt("Start line: ") - 1;
    int end   = edlin_get_int_prompt("End line: ") - 1;
    char search_str[4096];
    int found = 0;

    edlin_get_string_prompt("Search for: ", search_str);
    if (start < 0 || end >= edlin_line_count || start > end ||
        strlen(search_str) == 0) {
        return;
    }

    for (int i = start; i <= end; i++) {
        if (strstr(get_line_text(i), search_str) != NULL) {
            edlin_print("%d: ", i + 1);
            int mc = get_line_info(i).in_multiline_comment;
            print_syntax_highlighted(get_line_text(i), &mc, 0);
            edlin_print("\n");
            found++;
        }
    }
    edlin_print("%d matches found.\n", found);
}

static void replace_edlin_text(void)
{
    int start = edlin_get_int_prompt("Start line: ") - 1;
    int end   = edlin_get_int_prompt("End line: ") - 1;
    char search_str[4096];
    char replace_str[4096];
    int replaced = 0;

    edlin_get_string_prompt("Search for: ", search_str);
    edlin_get_string_prompt("Replace with: ", replace_str);

    if (start < 0 || end >= edlin_line_count || start > end ||
        strlen(search_str) == 0) {
        return;
    }

    for (int i = start; i <= end; i++) {
        char *pos = strstr(get_line_text(i), search_str);
        if (pos != NULL) {
            char temp[8192];
            int prefix_len = (int)(pos - get_line_text(i));
            snprintf(temp, sizeof(temp), "%.*s%s%s",
                     prefix_len, edlin_buffer[i].text,
                     replace_str,
                     pos + strlen(search_str));
            size_t temp_len = strlen(temp);
            ensure_line_capacity(i, temp_len + 1);
            strcpy(edlin_buffer[i].text, temp);
            edlin_buffer[i].length = (int)temp_len;
            
            edlin_print("%d: ", i + 1);
            int mc = get_line_info(i).in_multiline_comment;
            print_syntax_highlighted(get_line_text(i), &mc, 0);
            edlin_print("\n");
            replaced++;
        }
    }
    edlin_print("%d lines updated.\n", replaced);
}

static void transfer_edlin_file(void)
{
    int dest = edlin_get_int_prompt("Insert before line: ") - 1;
    char filename[4096];
    char input[4096];

    edlin_get_string_prompt("Filename: ", filename);
    if (dest < 0) dest = 0;
    if (dest > edlin_line_count) dest = edlin_line_count;

    FILE *f = fopen(filename, "r");
    if (f == NULL) {
        edlin_print("Error: Cannot open %s\n", filename);
        return;
    }

    while (fgets(input, sizeof(input), f) != NULL) {
        input[strcspn(input, "\n")] = '\0';
        if (input[strcspn(input, "\r")] == '\r') input[strcspn(input, "\r")] = '\0';
        
        insert_empty_line_at(dest);
        size_t len = strlen(input);
        ensure_line_capacity(dest, len + 1);
        strcpy(edlin_buffer[dest].text, input);
        edlin_buffer[dest].length = (int)len;
        dest++;
    }
    fclose(f);
    edlin_print("File transferred.\n");
}

static void write_edlin_lines(void)
{
    int count = edlin_get_int_prompt("Number of lines to write: ");
    if (count <= 0 || count > edlin_line_count) {
        return;
    }

    FILE *file = fopen(edlin_filename, "a");
    if (file == NULL) {
        edlin_print("Error: Cannot write to file.\n");
        return;
    }
    for (int i = 0; i < count; i++) {
        fprintf(file, "%s\n", get_line_text(i));
    }
    fclose(file);

    for (int i = 0; i < count; i++) {
        free_line(i);
    }
    for (int i = count; i < edlin_line_count; i++) {
        edlin_buffer[i - count] = edlin_buffer[i];
    }
    edlin_line_count -= count;
    memset(&edlin_buffer[edlin_line_count], 0, (size_t)count * sizeof(Line));
    edlin_print("%d lines written to disk and cleared from memory.\n", count);
}

static void append_edlin_lines(void)
{
    char input[4096];
    int appended = 0;

    FILE *file = fopen(edlin_filename, "r");
    if (file == NULL) {
        edlin_print("Error: Cannot read file.\n");
        return;
    }

    int skip = edlin_line_count;
    while (skip > 0 && fgets(input, sizeof(input), file) != NULL) {
        sanitize_ascii(input);
        skip--;
    }

    while (fgets(input, sizeof(input), file) != NULL) {
        input[strcspn(input, "\n")] = '\0';
        if (input[strcspn(input, "\r")] == '\r') input[strcspn(input, "\r")] = '\0';
        
        insert_empty_line_at(edlin_line_count);
        size_t len = strlen(input);
        ensure_line_capacity(edlin_line_count - 1, len + 1);
        strcpy(edlin_buffer[edlin_line_count - 1].text, input);
        edlin_buffer[edlin_line_count - 1].length = (int)len;
        appended++;
    }
    fclose(file);
    edlin_print("%d lines appended from disk.\n", appended);
}

/* =====================================================================
 * Public API
 * ===================================================================== */

int main(int argc, char **argv)
{
    
#if defined(_WIN32) || defined(WIN32)
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD dwMode = 0;
        if (GetConsoleMode(hOut, &dwMode)) {
            dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(hOut, dwMode);
        }
    }
#endif

    edlin_print("%s", bright_colors[color_index]);

    char command[4096];

    edlin_line_count = 0;
    edlin_page_pos   = 0;
    memset(edlin_filename, 0, sizeof(edlin_filename));
    // memset(edlin_buffer, 0, sizeof(edlin_buffer));

    if (argc > 1) {
        load_edlin_file(argv[1]);
    }

    edlin_print("Type '?' or 'h' for a list of available commands.\n");

    for (;;) {
        edlin_print("*");
        if (edlin_read_line(command, 4096) == NULL) {
            break;
        }
        command[strcspn(command, "\n")] = '\0';
        if (command[strcspn(command, "\r")] == '\r') command[strcspn(command, "\r")] = '\0';

        if (command[0] == '\0') {
            continue;
        }

        if (isdigit((unsigned char)command[0])) {
            char *endptr = NULL;
            long line_val = strtol(command, &endptr, 10);
            edit_edlin_line((int)line_val - 1);
            continue;
        }

        switch (tolower((unsigned char)command[0])) {
        case 'a':
            append_edlin_lines();
            break;
        case 'c':
            copy_edlin_lines();
            break;
        case 'd':
            delete_edlin_line();
            break;
        case 'e':
            save_edlin_file();
            goto edlin_exit;
        case 'h':
        case '?':
            display_edlin_help();
            break;
        case 'i':
            insert_edlin_line();
            break;
        case 'l':
            list_edlin_lines();
            break;
        case 'm':
            move_edlin_lines();
            break;
        case 'o':
            color_index = (int)((unsigned)(color_index + 1) % NUM_BRIGHT_COLORS);
            edlin_print("%s\x1b[2J\x1b[H[Color Changed]\r\n", bright_colors[color_index]);
            break;
        case 'p':
            page_edlin_display();
            break;
        case 'q':
            goto edlin_exit;
        case 'r':
            replace_edlin_text();
            break;
        case 's':
            search_edlin_text();
            break;
        case 't':
            transfer_edlin_file();
            break;
        case 'w':
            write_edlin_lines();
            break;
        default:
            edlin_print("Entry error (type '?' for help)\n");
            break;
        }
    }

edlin_exit:
    /* Cleanup: free all buffer memory and close file handles */
    if (edlin_buffer) {
        for (int i = 0; i < edlin_line_count; i++) {
            free(edlin_buffer[i].text);
        }
        free(edlin_buffer);
        edlin_buffer = NULL;
    }
    if (orig_file) { fclose(orig_file); orig_file = NULL; }
    if (index_file) { fclose(index_file); index_file = NULL; }
    edlin_print("\x1b[0m\x1b[2J\x1b[H");
    return 0;
}