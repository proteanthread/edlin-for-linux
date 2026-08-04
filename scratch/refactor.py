import re
import sys

def process(content):
    # 1. Update version
    content = content.replace("VERSION: 3.1.0", "VERSION: 4.1.0")

    # 2. Add 64-bit and new struct
    struct_old = """typedef struct {
    char *text;
    size_t length;
    size_t capacity;
} Line;
static Line *edlin_buffer = NULL;
static int edlin_buffer_capacity = 0;
static int  edlin_line_count = 0;
static int  edlin_page_pos   = 0;
static char edlin_filename[4096] = "";"""
    
    struct_new = """#if defined(_WIN32) || defined(WIN32)
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
        fseek_64(index_file, (int64_t)row * sizeof(Line), SEEK_SET);
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
    if (to_read > 0) fread(view_buf, 1, to_read, orig_file);
    view_buf[to_read] = '\0';
    return view_buf;
}

static void ensure_line_in_memory(int row) {
    if (is_read_only) return;
    if (is_fully_loaded) return;
    if (row < 0 || row >= edlin_line_count) return;
    if (!edlin_buffer[row].text) {
        edlin_buffer[row].capacity = get_line_info(row).length + 128;
        edlin_buffer[row].text = malloc(edlin_buffer[row].capacity);
        if (!edlin_buffer[row].text) { fprintf(stderr, "OOM\\n"); exit(1); }
        if (get_line_info(row).length > 0 && orig_file) {
            fseek_64(orig_file, get_line_info(row).disk_offset, SEEK_SET);
            fread(edlin_buffer[row].text, 1, get_line_info(row).length, orig_file);
        }
        edlin_buffer[row].text[get_line_info(row).length] = '\\0';
    }
}
"""
    content = content.replace(struct_old, struct_new)
    
    # 3. Update ensure_line_capacity
    ensure_cap_old = """static void ensure_line_capacity(int row, size_t needed) {
    if (needed > edlin_buffer[row].capacity) {"""
    ensure_cap_new = """static void ensure_line_capacity(int row, size_t needed) {
    ensure_line_in_memory(row);
    if (needed > (size_t)edlin_buffer[row].capacity) {"""
    content = content.replace(ensure_cap_old, ensure_cap_new)
    
    # 4. Update insert_empty_line_at
    insert_old = """static void insert_empty_line_at(int row) {
    ensure_buffer_capacity(edlin_line_count + 1);
    for (int i = edlin_line_count; i > row; i--) {
        edlin_buffer[i] = edlin_buffer[i - 1];
    }
    edlin_buffer[row].text = malloc(128);
    if (!edlin_buffer[row].text) oom();
    edlin_buffer[row].text[0] = '\\0';
    edlin_buffer[row].length = 0;
    edlin_buffer[row].capacity = 128;
    edlin_line_count++;
}"""
    insert_new = """static void insert_empty_line_at(int row) {
    ensure_buffer_capacity(edlin_line_count + 1);
    for (int i = edlin_line_count; i > row; i--) {
        edlin_buffer[i] = edlin_buffer[i - 1];
    }
    edlin_buffer[row].text = malloc(128);
    if (!edlin_buffer[row].text) oom();
    edlin_buffer[row].text[0] = '\\0';
    edlin_buffer[row].disk_offset = 0;
    edlin_buffer[row].length = 0;
    edlin_buffer[row].capacity = 128;
    edlin_line_count++;
}"""
    content = content.replace(insert_old, insert_new)
    
    # 5. Hybrid load_edlin_file
    load_old = """static void load_edlin_file(const char *filename)
{
    if (edlin_buffer) {
        for (int i = 0; i < edlin_line_count; i++) free_line(i);
    }
    edlin_line_count = 0;
    FILE *file = fopen(filename, "r");
    if (file != NULL) {
        char line_buf[4096];
        while (fgets(line_buf, sizeof(line_buf), file) != NULL) {
            size_t len = strlen(line_buf);
            if (len > 0 && line_buf[len - 1] == '\\n') {
                line_buf[len - 1] = '\\0'; len--;
            }
            if (len > 0 && line_buf[len - 1] == '\\r') {
                line_buf[len - 1] = '\\0'; len--;
            }
            insert_empty_line_at(edlin_line_count);
            ensure_line_capacity(edlin_line_count - 1, len + 1);
            strcpy(edlin_buffer[edlin_line_count - 1].text, line_buf);
            edlin_buffer[edlin_line_count - 1].length = len;
        }
        fclose(file);
        edlin_print("End of input file\\n");
    } else {
        edlin_print("New file\\n");
    }
    snprintf(edlin_filename, 4096, "%s", filename);
}"""
    load_new = """static void load_edlin_file(const char *filename)
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
        edlin_print("New file\\n");
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
        while (fgets(line_buf, sizeof(line_buf), file)) {
            sanitize_ascii(line_buf);
            size_t len = strlen(line_buf);
            while (len > 0 && (line_buf[len - 1] == '\\n' || line_buf[len - 1] == '\\r')) {
                line_buf[len - 1] = '\\0'; len--;
            }
            insert_empty_line_at(edlin_line_count);
            ensure_line_capacity(edlin_line_count - 1, len + 1);
            strcpy(edlin_buffer[edlin_line_count - 1].text, line_buf);
            edlin_buffer[edlin_line_count - 1].length = len;
        }
        fclose(file);
        edlin_print("End of input file\\n");
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
            if (!is_read_only && (edlin_line_count + 1) * sizeof(Line) > 256 * 1024 * 1024) {
                is_read_only = true;
                index_file = tmpfile();
                if (index_file && edlin_buffer) {
                    fwrite(edlin_buffer, sizeof(Line), edlin_line_count, index_file);
                }
                if (edlin_buffer) { free(edlin_buffer); edlin_buffer = NULL; }
            }
            
            if (!is_read_only) {
                ensure_buffer_capacity(edlin_line_count + 1);
                edlin_buffer[edlin_line_count].text = NULL;
                edlin_buffer[edlin_line_count].disk_offset = current_offset;
                edlin_buffer[edlin_line_count].length = len;
                edlin_buffer[edlin_line_count].capacity = 0;
            } else if (index_file) {
                Line l;
                memset(&l, 0, sizeof(Line));
                l.disk_offset = current_offset;
                l.length = len;
                fwrite(&l, sizeof(Line), 1, index_file);
            }
            edlin_line_count++;
            current_offset = ftell_64(orig_file);
        }
        edlin_print("End of input file\\n");
    }
    snprintf(edlin_filename, 4096, "%s", filename);
}"""
    content = content.replace(load_old, load_new)

    # 6. save_edlin_file
    save_old = """static void save_edlin_file(void)
{
    FILE *file = fopen(edlin_filename, "w");
    if (file == NULL) {
        edlin_print("Error: Cannot save file.\\n");
        return;
    }
    for (int i = 0; i < edlin_line_count; i++) {
        fprintf(file, "%s\\n", edlin_buffer[i].text);
    }
    fclose(file);
}"""
    save_new = """static void save_edlin_file(void)
{
    if (is_read_only) {
        edlin_print("Error: File is too large, opened in read-only mode.\\n");
        return;
    }
    if (edlin_filename[0] == '\\0') {
        edlin_print("Error: No filename specified.\\n");
        return;
    }
    FILE *file = fopen(edlin_filename, "w");
    if (file == NULL) {
        edlin_print("Error: Cannot save file.\\n");
        return;
    }
    for (int i = 0; i < edlin_line_count; i++) {
        fprintf(file, "%s\\n", get_line_text(i));
    }
    fclose(file);
}"""
    content = content.replace(save_old, save_new)
    
    # 7. Replace read accesses of edlin_buffer[x].text with get_line_text(x)
    # Be careful not to replace assignment statements or ensure_line_capacity calls.
    # List of functions where replacements happen:
    # list_edlin_lines
    # edit_edlin_line
    # search_edlin_text
    # replace_edlin_text
    # page_edlin_display
    # append_edlin_lines (not needed)
    # write_edlin_lines
    
    content = re.sub(r'edlin_buffer\[(i)\].text\)', r'get_line_text(\1))', content)
    content = re.sub(r'edlin_buffer\[(i \+ 1)\].text\)', r'get_line_text(\1))', content)
    
    # Specific replacements
    content = content.replace('edlin_print("%d: %s\\n", i + 1, edlin_buffer[i].text);', 'edlin_print("%d: %s\\n", i + 1, get_line_text(i));')
    content = content.replace('edlin_print("%d: %s\\n", index + 1, edlin_buffer[index].text);', 'edlin_print("%d: %s\\n", index + 1, get_line_text(index));')
    content = content.replace('strstr(edlin_buffer[i].text, search_str)', 'strstr(get_line_text(i), search_str)')
    
    # in replace_edlin_text:
    # char *pos = strstr(edlin_buffer[i].text, search_str);
    # prefix_len = (int)(pos - edlin_buffer[i].text);
    # snprintf(temp, sizeof(temp), "%.*s%s%s", prefix_len, edlin_buffer[i].text,
    
    rep_text_old = """        char *pos = strstr(edlin_buffer[i].text, search_str);
        if (pos != NULL) {
            char temp[8192];
            int prefix_len = (int)(pos - edlin_buffer[i].text);
            snprintf(temp, sizeof(temp), "%.*s%s%s",
                     prefix_len, edlin_buffer[i].text,
                     replace_str,
                     pos + strlen(search_str));"""
    rep_text_new = """        ensure_line_in_memory(i);
        char *pos = strstr(edlin_buffer[i].text, search_str);
        if (pos != NULL) {
            char temp[8192];
            int prefix_len = (int)(pos - edlin_buffer[i].text);
            snprintf(temp, sizeof(temp), "%.*s%s%s",
                     prefix_len, edlin_buffer[i].text,
                     replace_str,
                     pos + strlen(search_str));"""
    content = content.replace(rep_text_old, rep_text_new)
    
    # in copy_edlin_lines:
    # strcpy(edlin_buffer[dest + j].text, edlin_buffer[start + src_offset + j].text);
    # -> ensure_line_in_memory(start + src_offset + j)
    # -> strcpy(edlin_buffer[dest + j].text, get_line_text(start + src_offset + j));
    
    copy_old = """    int src_offset = (dest < start) ? count : 0;
    for (int j = 0; j < count; j++) {
        size_t len = edlin_buffer[start + src_offset + j].length;
        edlin_buffer[dest + j].text = malloc(len + 1);
        if (!edlin_buffer[dest + j].text) oom();
        strcpy(edlin_buffer[dest + j].text, edlin_buffer[start + src_offset + j].text);
        edlin_buffer[dest + j].length = len;
        edlin_buffer[dest + j].capacity = len + 1;
    }"""
    copy_new = """    int src_offset = (dest < start) ? count : 0;
    for (int j = 0; j < count; j++) {
        int src_idx = start + src_offset + j;
        size_t len = get_line_info(src_idx).length;
        edlin_buffer[dest + j].text = malloc(len + 1);
        if (!edlin_buffer[dest + j].text) oom();
        strcpy(edlin_buffer[dest + j].text, get_line_text(src_idx));
        edlin_buffer[dest + j].length = len;
        edlin_buffer[dest + j].capacity = len + 1;
    }"""
    content = content.replace(copy_old, copy_new)
    
    # write_edlin_lines
    # fprintf(file, "%s\\n", edlin_buffer[i].text);
    content = content.replace('fprintf(file, "%s\\n", edlin_buffer[i].text);', 'fprintf(file, "%s\\n", get_line_text(i));')
    
    # read accesses of .length in edlin.c? I don't think there are many. Let's find out.
    # We handled it in copy_edlin_lines.
    
    return content

if __name__ == "__main__":
    with open("editors/edlin/edlin.c", "r") as f:
        content = f.read()
    content = process(content)
    with open("editors/edlin/edlin.c", "w") as f:
        f.write(content)
