import re
import os

with open(r'c:\Users\rtdos\GitHub\bpp-text-editors\editors\ed\ed.c', 'r', encoding='utf-8') as f:
    code = f.read()

# 1. Version to 4.1.0
code = code.replace('VERSION: 3.1.0', 'VERSION: 4.1.0')

# 2. Add globals and Line struct
struct_target = """typedef struct {
    char *text;
    size_t length;
    size_t capacity;
} Line;
static Line *text_buffer = NULL;
static int text_buffer_capacity = 0;
static int num_lines = 0;
static char current_filename[4096] = "";
static char clipboard_line[4096] = "";
static char search_term[4096] = "";"""

struct_replacement = """#if defined(_WIN32) || defined(WIN32)
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
} Line;
static Line *text_buffer = NULL;
static int text_buffer_capacity = 0;
static int num_lines = 0;
static char current_filename[4096] = "";
static char clipboard_line[4096] = "";
static char search_term[4096] = "";

static FILE *orig_file = NULL;
static FILE *index_file = NULL;
static char view_buf[8192];
static bool is_fully_loaded = false;
static bool is_read_only = false;"""

code = code.replace(struct_target, struct_replacement)

# 3. Add get_line_info, get_line_text, ensure_line_in_memory right after oom
oom_target = """static void oom(void) {
    fprintf(stderr, "\\n\\nOut of memory!\\n");
    exit(1);
}"""

oom_replacement = """static void oom(void) {
    fprintf(stderr, "\\n\\nOut of memory!\\n");
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
    int clines = num_lines;
    if (row < 0 || row >= clines) return "";
    if (text_buffer && text_buffer[row].text) return text_buffer[row].text;
    Line l = get_line_info(row);
    if (l.text) return l.text;
    if (!orig_file) return "";
    fseek_64(orig_file, l.disk_offset, SEEK_SET);
    int to_read = l.length;
    if (to_read >= (int)sizeof(view_buf)) to_read = sizeof(view_buf) - 1;
    if (to_read > 0) fread(view_buf, 1, to_read, orig_file);
    view_buf[to_read] = '\\0';
    return view_buf;
}

static void ensure_line_in_memory(int row) {
    if (is_read_only) return;
    if (is_fully_loaded) return;
    if (row < 0 || row >= num_lines) return;
    if (!text_buffer[row].text) {
        text_buffer[row].capacity = get_line_info(row).length + 128;
        text_buffer[row].text = malloc(text_buffer[row].capacity);
        if (!text_buffer[row].text) oom();
        if (get_line_info(row).length > 0 && orig_file) {
            fseek_64(orig_file, get_line_info(row).disk_offset, SEEK_SET);
            fread(text_buffer[row].text, 1, get_line_info(row).length, orig_file);
        }
        text_buffer[row].text[get_line_info(row).length] = '\\0';
    }
}"""
code = code.replace(oom_target, oom_replacement)

# 4. Fix ensure_line_capacity
ensure_cap_target = """static void ensure_line_capacity(int row, size_t needed) {
    if (needed > text_buffer[row].capacity) {"""
ensure_cap_replacement = """static void ensure_line_capacity(int row, size_t needed) {
    ensure_line_in_memory(row);
    if (needed > (size_t)text_buffer[row].capacity) {"""
code = code.replace(ensure_cap_target, ensure_cap_replacement)

# 5. Fix insert_empty_line
insert_target = """    text_buffer[row].text = malloc(128);
    if (!text_buffer[row].text) oom();
    text_buffer[row].text[0] = '\\0';
    text_buffer[row].length = 0;
    text_buffer[row].capacity = 128;"""
insert_replacement = """    text_buffer[row].text = malloc(128);
    if (!text_buffer[row].text) oom();
    text_buffer[row].text[0] = '\\0';
    text_buffer[row].disk_offset = 0;
    text_buffer[row].length = 0;
    text_buffer[row].capacity = 128;"""
code = code.replace(insert_target, insert_replacement)

# 6. Replace load_file_edit
load_target = """static void load_file_edit(const char *filename, bool is_initial) {
    if (text_buffer) {
        for (int i = 0; i < num_lines; i++) free_line(i);
    }
    num_lines = 0;
    ensure_buffer_capacity(1);
    FILE *file = fopen(filename, "r");
    if (!file) {
        if (!is_initial) {
            insert_empty_line(0);
            return;
        } else {
            strncpy(current_filename, filename, 4095);
            current_filename[4095] = '\\0';
            insert_empty_line(0);
            cx = 0; cy = 0; row_off = 0; col_off = 0;
            return;
        }
    }
    char line_buf[4096];
    while (fgets(line_buf, sizeof(line_buf), file)) {
        sanitize_ascii(line_buf);
        size_t len = strlen(line_buf);
        while (len > 0 && (line_buf[len - 1] == '\\n' || line_buf[len - 1] == '\\r')) {
            line_buf[len - 1] = '\\0';
            len--;
        }
        insert_empty_line(num_lines);
        ensure_line_capacity(num_lines - 1, len + 1);
        strcpy(text_buffer[num_lines - 1].text, line_buf);
        text_buffer[num_lines - 1].length = len;
    }
    fclose(file);
    if (num_lines == 0) {
        insert_empty_line(0);
    }
    strncpy(current_filename, filename, 4095);
    current_filename[4095] = '\\0';
    cx = 0; cy = 0; row_off = 0; col_off = 0;
}"""

load_replacement = """static void load_file_edit(const char *filename, bool is_initial) {
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
    if (!file) {
        if (!is_initial) {
            is_fully_loaded = true;
            insert_empty_line(0);
            return;
        } else {
            strncpy(current_filename, filename, 4095);
            current_filename[4095] = '\\0';
            is_fully_loaded = true;
            insert_empty_line(0);
            cx = 0; cy = 0; row_off = 0; col_off = 0;
            return;
        }
    }

    fseek_64(file, 0, SEEK_END);
    int64_t file_size = ftell_64(file);
    fseek_64(file, 0, SEEK_SET);

    if (file_size < 64 * 1024 * 1024) {
        is_fully_loaded = true;
        ensure_buffer_capacity(1);
        char line_buf[4096];
        while (fgets(line_buf, sizeof(line_buf), file)) {
            sanitize_ascii(line_buf);
            size_t len = strlen(line_buf);
            while (len > 0 && (line_buf[len - 1] == '\\n' || line_buf[len - 1] == '\\r')) {
                line_buf[len - 1] = '\\0'; len--;
            }
            insert_empty_line(num_lines);
            ensure_line_capacity(num_lines - 1, len + 1);
            strcpy(text_buffer[num_lines - 1].text, line_buf);
            text_buffer[num_lines - 1].length = len;
        }
        fclose(file);
    } else {
        orig_file = file;
        char line_buf[4096];
        int64_t current_offset = 0;
        while (fgets(line_buf, sizeof(line_buf), orig_file)) {
            sanitize_ascii(line_buf);
            size_t len = strlen(line_buf);
            while (len > 0 && (line_buf[len - 1] == '\\n' || line_buf[len - 1] == '\\r')) {
                line_buf[len - 1] = '\\0'; len--;
            }
            if (!is_read_only && (num_lines + 1) * sizeof(Line) > 256 * 1024 * 1024) {
                is_read_only = true;
                index_file = tmpfile();
                if (index_file && text_buffer) {
                    fwrite(text_buffer, sizeof(Line), num_lines, index_file);
                }
                if (text_buffer) { free(text_buffer); text_buffer = NULL; }
            }
            
            if (!is_read_only) {
                ensure_buffer_capacity(num_lines + 1);
                text_buffer[num_lines].text = NULL;
                text_buffer[num_lines].disk_offset = current_offset;
                text_buffer[num_lines].length = len;
                text_buffer[num_lines].capacity = 0;
            } else if (index_file) {
                Line l;
                memset(&l, 0, sizeof(Line));
                l.disk_offset = current_offset;
                l.length = len;
                fwrite(&l, sizeof(Line), 1, index_file);
            }
            num_lines++;
            current_offset = ftell_64(orig_file);
        }
    }
    
    strncpy(current_filename, filename, 4095);
    current_filename[4095] = '\\0';
    if (num_lines == 0) { is_read_only = false; insert_empty_line(0); }
    cx = 0; cy = 0; row_off = 0; col_off = 0;
}"""
code = code.replace(load_target, load_replacement)

# 7. save_file_edit
save_target = """static void save_file_edit(void) {
    if (current_filename[0] == '\\0') return;
    FILE *file = fopen(current_filename, "w");
    if (!file) return;
    for (int i = 0; i < num_lines; i++) {
        fprintf(file, "%s\\n", text_buffer[i].text);
    }
    fclose(file);
}"""

save_replacement = """static void save_file_edit(void) {
    if (is_read_only) return;
    if (current_filename[0] == '\\0') return;
    FILE *file = fopen(current_filename, "w");
    if (!file) return;
    for (int i = 0; i < num_lines; i++) {
        fprintf(file, "%s\\n", get_line_text(i));
    }
    fclose(file);
}"""
code = code.replace(save_target, save_replacement)

def rx_replace(old, new):
    global code
    code = code.replace(old, new)

rx_replace("""static void insert_char(int c) {
    int len = text_buffer[cy].length;
    ensure_line_capacity(cy, len + 2);
    memmove(&text_buffer[cy].text[cx + 1], &text_buffer[cy].text[cx], len - cx + 1);
    text_buffer[cy].text[cx] = (char)c;
    text_buffer[cy].length++;
    cx++;
}""", """static void insert_char(int c) {
    if (is_read_only) return;
    ensure_line_in_memory(cy);
    int len = get_line_info(cy).length;
    ensure_line_capacity(cy, len + 2);
    memmove(&text_buffer[cy].text[cx + 1], &text_buffer[cy].text[cx], len - cx + 1);
    text_buffer[cy].text[cx] = (char)c;
    text_buffer[cy].length++;
    cx++;
}""")

rx_replace("""static void insert_newline(void) {
    insert_empty_line(cy + 1);
    int remaining_len = text_buffer[cy].length - cx;
    ensure_line_capacity(cy + 1, remaining_len + 1);
    memmove(text_buffer[cy + 1].text, text_buffer[cy].text + cx, remaining_len + 1);
    text_buffer[cy + 1].length = remaining_len;
    text_buffer[cy].text[cx] = '\\0';
    text_buffer[cy].length = cx;
    cy++;
    cx = 0;
}""", """static void insert_newline(void) {
    if (is_read_only) return;
    ensure_line_in_memory(cy);
    ensure_line_in_memory(cy + 1);
    insert_empty_line(cy + 1);
    int remaining_len = get_line_info(cy).length - cx;
    ensure_line_capacity(cy + 1, remaining_len + 1);
    memmove(text_buffer[cy + 1].text, get_line_text(cy) + cx, remaining_len + 1);
    text_buffer[cy + 1].length = remaining_len;
    text_buffer[cy].text[cx] = '\\0';
    text_buffer[cy].length = cx;
    cy++;
    cx = 0;
}""")

rx_replace("""static void handle_backspace(void) {
    if (cx > 0) {
        int len = text_buffer[cy].length;
        memmove(&text_buffer[cy].text[cx - 1], &text_buffer[cy].text[cx], len - cx + 1);
        text_buffer[cy].length--;
        cx--;
    } else if (cy > 0) {
        int prev_len = text_buffer[cy - 1].length;
        int cur_len = text_buffer[cy].length;
        ensure_line_capacity(cy - 1, prev_len + cur_len + 1);
        memmove(&text_buffer[cy - 1].text[prev_len], text_buffer[cy].text, cur_len + 1);
        text_buffer[cy - 1].length += cur_len;
        free_line(cy);
        for (int i = cy; i < num_lines - 1; i++) {
            text_buffer[i] = text_buffer[i + 1];
        }
        num_lines--; cy--; cx = prev_len;
    }
}""", """static void handle_backspace(void) {
    if (is_read_only) return;
    ensure_line_in_memory(cy);
    if (cy > 0) ensure_line_in_memory(cy - 1);
    if (cx > 0) {
        int len = get_line_info(cy).length;
        memmove(&text_buffer[cy].text[cx - 1], &text_buffer[cy].text[cx], len - cx + 1);
        text_buffer[cy].length--;
        cx--;
    } else if (cy > 0) {
        int prev_len = get_line_info(cy - 1).length;
        int cur_len = get_line_info(cy).length;
        ensure_line_capacity(cy - 1, prev_len + cur_len + 1);
        memmove(&text_buffer[cy - 1].text[prev_len], get_line_text(cy), cur_len + 1);
        text_buffer[cy - 1].length += cur_len;
        free_line(cy);
        for (int i = cy; i < num_lines - 1; i++) {
            text_buffer[i] = text_buffer[i + 1];
        }
        num_lines--; cy--; cx = prev_len;
    }
}""")

rx_replace("""static void handle_delete(void) {
    int cur_len = text_buffer[cy].length;
    if (cx < cur_len) {
        memmove(&text_buffer[cy].text[cx], &text_buffer[cy].text[cx + 1], cur_len - cx);
        text_buffer[cy].length--;
    } else if (cy < num_lines - 1) {
        int next_len = text_buffer[cy + 1].length;
        ensure_line_capacity(cy, cur_len + next_len + 1);
        memmove(&text_buffer[cy].text[cur_len], text_buffer[cy + 1].text, next_len + 1);
        text_buffer[cy].length += next_len;
        free_line(cy + 1);
        for (int i = cy + 1; i < num_lines - 1; i++) {
            text_buffer[i] = text_buffer[i + 1];
        }
        num_lines--;
    }
}""", """static void handle_delete(void) {
    if (is_read_only) return;
    ensure_line_in_memory(cy);
    int cur_len = get_line_info(cy).length;
    if (cx < cur_len) {
        memmove(&text_buffer[cy].text[cx], &text_buffer[cy].text[cx + 1], cur_len - cx);
        text_buffer[cy].length--;
    } else if (cy < num_lines - 1) {
        ensure_line_in_memory(cy + 1);
        int next_len = get_line_info(cy + 1).length;
        ensure_line_capacity(cy, cur_len + next_len + 1);
        memmove(&text_buffer[cy].text[cur_len], get_line_text(cy + 1), next_len + 1);
        text_buffer[cy].length += next_len;
        free_line(cy + 1);
        for (int i = cy + 1; i < num_lines - 1; i++) {
            text_buffer[i] = text_buffer[i + 1];
        }
        num_lines--;
    }
}""")

rx_replace("""static void delete_current_line(void) {
    if (num_lines > 1) {
        free_line(cy);
        for (int i = cy; i < num_lines - 1; i++) text_buffer[i] = text_buffer[i + 1];
        num_lines--;
        if (cy >= num_lines) cy = num_lines - 1;
        if (cx > (int)text_buffer[cy].length) cx = text_buffer[cy].length;
    } else {
        text_buffer[0].text[0] = '\\0';
        text_buffer[0].length = 0;
        cx = 0;
    }
}""", """static void delete_current_line(void) {
    if (is_read_only) return;
    if (num_lines > 1) {
        free_line(cy);
        for (int i = cy; i < num_lines - 1; i++) text_buffer[i] = text_buffer[i + 1];
        num_lines--;
        if (cy >= num_lines) cy = num_lines - 1;
        if (cx > (int)get_line_info(cy).length) cx = get_line_info(cy).length;
    } else {
        ensure_line_in_memory(0);
        text_buffer[0].text[0] = '\\0';
        text_buffer[0].length = 0;
        cx = 0;
    }
}""")

rx_replace("""static int get_render_x(int row, int physical_x) {
    int rx = 0;
    for (int j = 0; j < physical_x && text_buffer[row].text[j] != '\\0'; j++) {
        if (text_buffer[row].text[j] == '\\t') rx += (TAB_STOP - 1) - (rx % TAB_STOP);
        rx++;
    }
    return rx;
}""", """static int get_render_x(int row, int physical_x) {
    int rx = 0;
    for (int j = 0; j < physical_x && get_line_text(row)[j] != '\\0'; j++) {
        if (get_line_text(row)[j] == '\\t') rx += (TAB_STOP - 1) - (rx % TAB_STOP);
        rx++;
    }
    return rx;
}""")

rx_replace("""static int get_physical_x(int row, int target_x) {
    int rx = 0, j;
    for (j = 0; text_buffer[row].text[j] != '\\0'; j++) {
        int next_rx = rx;
        if (text_buffer[row].text[j] == '\\t') next_rx += (TAB_STOP - 1) - (rx % TAB_STOP);
        next_rx++;
        if (next_rx > target_x) return j;
        rx = next_rx;
    }
    return j;
}""", """static int get_physical_x(int row, int target_x) {
    int rx = 0, j;
    for (j = 0; get_line_text(row)[j] != '\\0'; j++) {
        int next_rx = rx;
        if (get_line_text(row)[j] == '\\t') next_rx += (TAB_STOP - 1) - (rx % TAB_STOP);
        next_rx++;
        if (next_rx > target_x) return j;
        rx = next_rx;
    }
    return j;
}""")

rx_replace("""static void render_row(int row, char *out_buf) {
    int j = 0, idx = 0;
    while (text_buffer[row].text[j] != '\\0' && idx < (MAX_RENDER_BUF - 1)) {
        if (text_buffer[row].text[j] == '\\t') {
            out_buf[idx++] = ' ';
            while (idx % TAB_STOP != 0 && idx < (MAX_RENDER_BUF - 1)) out_buf[idx++] = ' ';
        } else {
            out_buf[idx++] = text_buffer[row].text[j];
        }
        j++;
    }
    out_buf[idx] = '\\0';
}""", """static void render_row(int row, char *out_buf) {
    int j = 0, idx = 0;
    while (get_line_text(row)[j] != '\\0' && idx < (MAX_RENDER_BUF - 1)) {
        if (get_line_text(row)[j] == '\\t') {
            out_buf[idx++] = ' ';
            while (idx % TAB_STOP != 0 && idx < (MAX_RENDER_BUF - 1)) out_buf[idx++] = ' ';
        } else {
            out_buf[idx++] = get_line_text(row)[j];
        }
        j++;
    }
    out_buf[idx] = '\\0';
}""")

rx_replace("""static void do_find(void) {
    char *p;
    for (int r = cy; r < num_lines; r++) {
        int start_x = (r == cy) ? cx + 1 : 0;
        if (start_x < (int)text_buffer[r].length) {
            p = strstr(text_buffer[r].text + start_x, search_term);
            if (p) {
                cy = r;
                cx = (int)(p - text_buffer[r].text);
                target_rx = get_render_x(cy, cx);
                return;
            }
        }
    }
    show_message(" Find ", "Search term not found.");
}""", """static void do_find(void) {
    char *p;
    for (int r = cy; r < num_lines; r++) {
        int start_x = (r == cy) ? cx + 1 : 0;
        if (start_x < (int)get_line_info(r).length) {
            p = strstr(get_line_text(r) + start_x, search_term);
            if (p) {
                cy = r;
                cx = (int)(p - get_line_text(r));
                target_rx = get_render_x(cy, cx);
                return;
            }
        }
    }
    show_message(" Find ", "Search term not found.");
}""")


rx_replace("""static char* get_selected_text_edit(void) {
    if (!sel_active) return NULL;
    int r1, c1, r2, c2;
    get_sel_bounds(&r1, &c1, &r2, &c2);
    char *buf = malloc(65536);
    if (!buf) return NULL;
    buf[0] = '\\0';
    int pos = 0;
    for (int r = r1; r <= r2; r++) {
        int start = (r == r1) ? c1 : 0;
        int end = (r == r2) ? c2 : (int)text_buffer[r].length;
        for (int i = start; i < end; i++) {
            buf[pos++] = text_buffer[r].text[i];
        }
        if (r < r2) {
            buf[pos++] = '\\n';
        }
    }
    buf[pos] = '\\0';
    return buf;
}""", """static char* get_selected_text_edit(void) {
    if (!sel_active) return NULL;
    int r1, c1, r2, c2;
    get_sel_bounds(&r1, &c1, &r2, &c2);
    char *buf = malloc(65536);
    if (!buf) return NULL;
    buf[0] = '\\0';
    int pos = 0;
    for (int r = r1; r <= r2; r++) {
        int start = (r == r1) ? c1 : 0;
        int end = (r == r2) ? c2 : (int)get_line_info(r).length;
        for (int i = start; i < end; i++) {
            buf[pos++] = get_line_text(r)[i];
        }
        if (r < r2) {
            buf[pos++] = '\\n';
        }
    }
    buf[pos] = '\\0';
    return buf;
}""")

rx_replace("""static void delete_selected_text_edit(void) {
    if (!sel_active) return;
    int r1, c1, r2, c2;
    get_sel_bounds(&r1, &c1, &r2, &c2);
    
    int rem_len = text_buffer[r2].length - c2;
    char *rem = malloc(rem_len + 1);
    if (!rem) oom();
    strcpy(rem, text_buffer[r2].text + c2);
    
    text_buffer[r1].text[c1] = '\\0';
    text_buffer[r1].length = c1;
    ensure_line_capacity(r1, c1 + rem_len + 1);
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
    }
    cy = r1; cx = c1;
    sel_active = false;
}""", """static void delete_selected_text_edit(void) {
    if (is_read_only) return;
    if (!sel_active) return;
    int r1, c1, r2, c2;
    get_sel_bounds(&r1, &c1, &r2, &c2);
    
    int rem_len = get_line_info(r2).length - c2;
    char *rem = malloc(rem_len + 1);
    if (!rem) oom();
    ensure_line_in_memory(r2);
    ensure_line_in_memory(r1);
    strcpy(rem, get_line_text(r2) + c2);
    
    text_buffer[r1].text[c1] = '\\0';
    text_buffer[r1].length = c1;
    ensure_line_capacity(r1, c1 + rem_len + 1);
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
    }
    cy = r1; cx = c1;
    sel_active = false;
}""")

rx_replace("""static void insert_text_at_cursor_edit(const char *text) {
    if (sel_active) delete_selected_text_edit();
    const char *p = text;
    while (*p) {
        if (*p == '\\n' || *p == '\\r') {
            if (*p == '\\r' && *(p+1) == '\\n') p++; 
            insert_newline();
            p++;
        } else {
            int len = text_buffer[cy].length;
            ensure_line_capacity(cy, len + 2);
            memmove(&text_buffer[cy].text[cx + 1], &text_buffer[cy].text[cx], len - cx + 1);
            text_buffer[cy].text[cx] = *p;
            text_buffer[cy].length++;
            cx++;
            p++;
        }
    }
}""", """static void insert_text_at_cursor_edit(const char *text) {
    if (is_read_only) return;
    ensure_line_in_memory(cy);
    if (sel_active) delete_selected_text_edit();
    const char *p = text;
    while (*p) {
        if (*p == '\\n' || *p == '\\r') {
            if (*p == '\\r' && *(p+1) == '\\n') p++; 
            insert_newline();
            p++;
        } else {
            int len = get_line_info(cy).length;
            ensure_line_capacity(cy, len + 2);
            memmove(&text_buffer[cy].text[cx + 1], &text_buffer[cy].text[cx], len - cx + 1);
            text_buffer[cy].text[cx] = *p;
            text_buffer[cy].length++;
            cx++;
            p++;
        }
    }
}""")

rx_replace("""            else { strcpy(clipboard_line, text_buffer[cy].text); delete_current_line(); }""", """            else { strcpy(clipboard_line, get_line_text(cy)); delete_current_line(); }""")
rx_replace("""            else { strcpy(clipboard_line, text_buffer[cy].text); }""", """            else { strcpy(clipboard_line, get_line_text(cy)); }""")

rx_replace("""                if (cx == 0 && cy > 0) update_sel_end(cy - 1, (int)text_buffer[cy-1].length);""", """                if (cx == 0 && cy > 0) update_sel_end(cy - 1, (int)get_line_info(cy-1).length);""")
rx_replace("""                update_sel_end(cy, cx < (int)text_buffer[cy].length ? cx + 1 : cx);""", """                update_sel_end(cy, cx < (int)get_line_info(cy).length ? cx + 1 : cx);""")
rx_replace("""                if (cx == (int)text_buffer[cy].length && cy < num_lines - 1) update_sel_end(cy + 1, 0);""", """                if (cx == (int)get_line_info(cy).length && cy < num_lines - 1) update_sel_end(cy + 1, 0);""")

rx_replace("""                case ARROW_LEFT: 
                    if (cx > 0) cx--; 
                    else if (cy > 0) { cy--; cx = (int)text_buffer[cy].length; }
                    break;
                case ARROW_RIGHT:
                    if (cx < (int)text_buffer[cy].length) cx++;
                    else if (cy < num_lines - 1) { cy++; cx = 0; }
                    break;""", """                case ARROW_LEFT: 
                    if (cx > 0) cx--; 
                    else if (cy > 0) { cy--; cx = (int)get_line_info(cy).length; }
                    break;
                case ARROW_RIGHT:
                    if (cx < (int)get_line_info(cy).length) cx++;
                    else if (cy < num_lines - 1) { cy++; cx = 0; }
                    break;""")

rx_replace("""                case END_KEY: cx = (int)text_buffer[cy].length; break;""", """                case END_KEY: cx = (int)get_line_info(cy).length; break;""")
rx_replace("""                case CTRL_END: cy = num_lines - 1; cx = (int)text_buffer[cy].length; moved_vertically = true; break;""", """                case CTRL_END: cy = num_lines - 1; cx = (int)get_line_info(cy).length; moved_vertically = true; break;""")

rx_replace("""            if (moved_vertically) cx = get_physical_x(cy, target_rx);
            else {
                if (cx > (int)text_buffer[cy].length) cx = (int)text_buffer[cy].length;
                target_rx = get_render_x(cy, cx);
            }""", """            if (moved_vertically) cx = get_physical_x(cy, target_rx);
            else {
                if (cx > (int)get_line_info(cy).length) cx = (int)get_line_info(cy).length;
                target_rx = get_render_x(cy, cx);
            }""")

with open(r'c:\Users\rtdos\GitHub\bpp-text-editors\editors\ed\ed.c', 'w', encoding='utf-8') as f:
    f.write(code)

print("done")
