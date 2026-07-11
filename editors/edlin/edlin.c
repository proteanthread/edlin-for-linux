/* 
 *
 * Standalone DOS EDLIN Clone
 * VERSION: 3.0.0
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
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
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

#define MAX_EDLIN_LINES 1000
#define MAX_EDLIN_LENGTH 255

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

static char edlin_buffer[MAX_EDLIN_LINES][MAX_EDLIN_LENGTH];
static int  edlin_line_count = 0;
static int  edlin_page_pos   = 0;
static char edlin_filename[MAX_EDLIN_LENGTH] = "";

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
    char input[MAX_EDLIN_LENGTH];
    edlin_print("%s", prompt);
    if (edlin_read_line(input, MAX_EDLIN_LENGTH) == NULL) {
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
    if (edlin_read_line(buffer, MAX_EDLIN_LENGTH) != NULL) {
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
    edlin_print("\nedlin - Built-in Line Editor\n");
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
    FILE *file = fopen(filename, "r");
    if (file != NULL) {
        edlin_line_count = 0;
        while (edlin_line_count < MAX_EDLIN_LINES &&
               fgets(edlin_buffer[edlin_line_count], MAX_EDLIN_LENGTH, file) != NULL) {
            
        size_t len = strlen(edlin_buffer[edlin_line_count]);
            if (len > 0 && edlin_buffer[edlin_line_count][len - 1] == '\n') {
                edlin_buffer[edlin_line_count][len - 1] = '\0';
            }
            if (len > 1 && edlin_buffer[edlin_line_count][len - 2] == '\r') {
                edlin_buffer[edlin_line_count][len - 2] = '\0';
            }
            edlin_line_count++;
        }
        fclose(file);
        edlin_print("End of input file\n");
    } else {
        edlin_print("New file\n");
    }
    snprintf(edlin_filename, MAX_EDLIN_LENGTH, "%s", filename);
}

static void save_edlin_file(void)
{
    FILE *file = fopen(edlin_filename, "w");
    if (file == NULL) {
        edlin_print("Error: Cannot save file.\n");
        return;
    }
    for (int i = 0; i < edlin_line_count; i++) {
        fprintf(file, "%s\n", edlin_buffer[i]);
    }
    fclose(file);
}

static void list_edlin_lines(void)
{
    for (int i = 0; i < edlin_line_count; i++) {
        edlin_print("%d: %s\n", i + 1, edlin_buffer[i]);
    }
}

static void insert_edlin_line(void)
{
    char input[MAX_EDLIN_LENGTH];

    if (edlin_line_count >= MAX_EDLIN_LINES) {
        edlin_print("Error: Buffer is full (%d lines).\n", MAX_EDLIN_LINES);
        return;
    }

    while (edlin_line_count < MAX_EDLIN_LINES) {
        edlin_print("%d:*", edlin_line_count + 1);
        if (edlin_read_line(input, MAX_EDLIN_LENGTH) == NULL) {
            break;
        }
        input[strcspn(input, "\n")] = '\0';
        if (input[strcspn(input, "\r")] == '\r') input[strcspn(input, "\r")] = '\0';
        
        if (strcmp(input, ".") == 0) {
            break;
        }
        snprintf(edlin_buffer[edlin_line_count], MAX_EDLIN_LENGTH, "%s", input);
        edlin_line_count++;
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
        for (int i = index; i < edlin_line_count - 1; i++) {
            memmove(edlin_buffer[i], edlin_buffer[i + 1], strlen(edlin_buffer[i + 1]) + 1);
        }
        edlin_line_count--;
        edlin_print("Line deleted.\n");
    } else {
        edlin_print("Error: Invalid line number.\n");
    }
}

static void edit_edlin_line(int index)
{
    char input[MAX_EDLIN_LENGTH];

    if (index >= 0 && index < edlin_line_count) {
        edlin_print("%d: %s\n", index + 1, edlin_buffer[index]);
        edlin_print("%d:*", index + 1);
        if (edlin_read_line(input, MAX_EDLIN_LENGTH) != NULL) {
            input[strcspn(input, "\n")] = '\0';
            if (input[strcspn(input, "\r")] == '\r') input[strcspn(input, "\r")] = '\0';
            
            if (strlen(input) > 0) {
                snprintf(edlin_buffer[index], MAX_EDLIN_LENGTH, "%s", input);
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
    if (edlin_line_count + count > MAX_EDLIN_LINES) {
        edlin_print("Error: Buffer limit reached.\n");
        return;
    }

    for (int i = edlin_line_count - 1; i >= dest; i--) {
        memmove(edlin_buffer[i + count], edlin_buffer[i], strlen(edlin_buffer[i]) + 1);
    }

    int src_offset = (dest < start) ? count : 0;
    for (int j = 0; j < count; j++) {
        memmove(edlin_buffer[dest + j], edlin_buffer[start + src_offset + j], strlen(edlin_buffer[start + src_offset + j]) + 1);
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

    for (int i = edlin_line_count - 1; i >= dest; i--) {
        memmove(edlin_buffer[i + count], edlin_buffer[i], strlen(edlin_buffer[i]) + 1);
    }

    int del_start;
    if (dest < start) {
        for (int j = 0; j < count; j++) {
            memmove(edlin_buffer[dest + j], edlin_buffer[start + count + j], strlen(edlin_buffer[start + count + j]) + 1);
        }
        del_start = start + count;
    } else {
        for (int j = 0; j < count; j++) {
            memmove(edlin_buffer[dest + j], edlin_buffer[start + j], strlen(edlin_buffer[start + j]) + 1);
        }
        del_start = start;
    }

    for (int i = del_start; i < edlin_line_count; i++) {
        memmove(edlin_buffer[i], edlin_buffer[i + count], strlen(edlin_buffer[i + count]) + 1);
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
        edlin_print("%d: %s\n", i + 1, edlin_buffer[i]);
    }
    edlin_page_pos = end;
}

static void search_edlin_text(void)
{
    int start = edlin_get_int_prompt("Start line: ") - 1;
    int end   = edlin_get_int_prompt("End line: ") - 1;
    char search_str[MAX_EDLIN_LENGTH];
    int found = 0;

    edlin_get_string_prompt("Search for: ", search_str);
    if (start < 0 || end >= edlin_line_count || start > end ||
        strlen(search_str) == 0) {
        return;
    }

    for (int i = start; i <= end; i++) {
        if (strstr(edlin_buffer[i], search_str) != NULL) {
            edlin_print("%d: %s\n", i + 1, edlin_buffer[i]);
            found++;
        }
    }
    edlin_print("%d matches found.\n", found);
}

static void replace_edlin_text(void)
{
    int start = edlin_get_int_prompt("Start line: ") - 1;
    int end   = edlin_get_int_prompt("End line: ") - 1;
    char search_str[MAX_EDLIN_LENGTH];
    char replace_str[MAX_EDLIN_LENGTH];
    int replaced = 0;

    edlin_get_string_prompt("Search for: ", search_str);
    edlin_get_string_prompt("Replace with: ", replace_str);

    if (start < 0 || end >= edlin_line_count || start > end ||
        strlen(search_str) == 0) {
        return;
    }

    for (int i = start; i <= end; i++) {
        char *pos = strstr(edlin_buffer[i], search_str);
        if (pos != NULL) {
            char temp[MAX_EDLIN_LENGTH * 2];
            int prefix_len = (int)(pos - edlin_buffer[i]);
            snprintf(temp, sizeof(temp), "%.*s%s%s",
                     prefix_len, edlin_buffer[i],
                     replace_str,
                     pos + strlen(search_str));
            size_t temp_len = strlen(temp);
            size_t copy_len = temp_len < (size_t)(MAX_EDLIN_LENGTH - 1) ? temp_len : (size_t)(MAX_EDLIN_LENGTH - 1);
            memcpy(edlin_buffer[i], temp, copy_len);
            edlin_buffer[i][copy_len] = '\0';
            edlin_print("%d: %s\n", i + 1, edlin_buffer[i]);
            replaced++;
        }
    }
    edlin_print("%d lines updated.\n", replaced);
}

static void transfer_edlin_file(void)
{
    int dest = edlin_get_int_prompt("Insert before line: ") - 1;
    char filename[MAX_EDLIN_LENGTH];
    char input[MAX_EDLIN_LENGTH];

    edlin_get_string_prompt("Filename: ", filename);
    if (dest < 0) dest = 0;
    if (dest > edlin_line_count) dest = edlin_line_count;

    FILE *f = fopen(filename, "r");
    if (f == NULL) {
        edlin_print("Error: Cannot open %s\n", filename);
        return;
    }

    while (edlin_line_count < MAX_EDLIN_LINES &&
           fgets(input, MAX_EDLIN_LENGTH, f) != NULL) {
        
        input[strcspn(input, "\n")] = '\0';
        if (input[strcspn(input, "\r")] == '\r') input[strcspn(input, "\r")] = '\0';
        
        for (int i = edlin_line_count; i > dest; i--) {
            memmove(edlin_buffer[i], edlin_buffer[i - 1], strlen(edlin_buffer[i - 1]) + 1);
        }
        snprintf(edlin_buffer[dest], MAX_EDLIN_LENGTH, "%s", input);
        edlin_line_count++;
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
        fprintf(file, "%s\n", edlin_buffer[i]);
    }
    fclose(file);

    for (int i = count; i < edlin_line_count; i++) {
        memmove(edlin_buffer[i - count], edlin_buffer[i], strlen(edlin_buffer[i]) + 1);
    }
    edlin_line_count -= count;
    edlin_print("%d lines written to disk and cleared from memory.\n", count);
}

static void append_edlin_lines(void)
{
    char input[MAX_EDLIN_LENGTH];
    int appended = 0;

    if (edlin_line_count >= MAX_EDLIN_LINES) {
        edlin_print("Error: Buffer is full.\n");
        return;
    }

    FILE *file = fopen(edlin_filename, "r");
    if (file == NULL) {
        edlin_print("Error: Cannot read file.\n");
        return;
    }

    int skip = edlin_line_count;
    while (skip > 0 && fgets(input, MAX_EDLIN_LENGTH, file) != NULL) {
        sanitize_ascii(input);
        
        skip--;
    }

    while (edlin_line_count < MAX_EDLIN_LINES &&
           fgets(input, MAX_EDLIN_LENGTH, file) != NULL) {
        
        input[strcspn(input, "\n")] = '\0';
        if (input[strcspn(input, "\r")] == '\r') input[strcspn(input, "\r")] = '\0';
        snprintf(edlin_buffer[edlin_line_count], MAX_EDLIN_LENGTH, "%s", input);
        edlin_line_count++;
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

    char command[MAX_EDLIN_LENGTH];

    edlin_line_count = 0;
    edlin_page_pos   = 0;
    memset(edlin_filename, 0, sizeof(edlin_filename));
    memset(edlin_buffer, 0, sizeof(edlin_buffer));

    if (argc > 1) {
        load_edlin_file(argv[1]);
    }

    edlin_print("Type '?' or 'h' for a list of available commands.\n");

    for (;;) {
        edlin_print("*");
        if (edlin_read_line(command, MAX_EDLIN_LENGTH) == NULL) {
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
            color_index = (color_index + 1) % NUM_BRIGHT_COLORS;
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
    edlin_print("\x1b[0m\x1b[2J\x1b[H");
    return 0;
}