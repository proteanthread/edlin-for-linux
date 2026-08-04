import re

def process(file_path):
    with open(file_path, "r") as f:
        content = f.read()

    content = content.replace(
        "if (cx == 0 && cy > 0) update_sel_end(cy - 1, (int)text_buffer[cy-1].length);",
        "if (cx == 0 && cy > 0) update_sel_end(cy - 1, (int)get_line_info(cy-1).length);"
    )
    
    content = content.replace(
        "int nl = text_buffer[cy + 1].length;",
        "int nl = get_line_info(cy + 1).length;"
    )

    if "strcpy(rem, text_buffer[r2].text + c2);" in content and "ensure_line_in_memory(r2);" not in content:
        content = content.replace("strcpy(rem, text_buffer[r2].text + c2);", "ensure_line_in_memory(r2);\n    strcpy(rem, text_buffer[r2].text + c2);")

    old_del = """                    } else if (cy < num_lines - 1) {
                        int nl = get_line_info(cy + 1).length;
                        ensure_line_capacity(cy, curl + nl + 1);
                        memmove(&text_buffer[cy].text[curl], text_buffer[cy + 1].text, nl + 1);"""
    
    new_del = """                    } else if (cy < num_lines - 1) {
                        int nl = get_line_info(cy + 1).length;
                        ensure_line_capacity(cy, curl + nl + 1);
                        ensure_line_in_memory(cy + 1);
                        memmove(&text_buffer[cy].text[curl], text_buffer[cy + 1].text, nl + 1);"""
    
    content = content.replace(old_del, new_del)
        
    with open(file_path, "w") as f:
        f.write(content)

process("c:/Users/rtdos/GitHub/bpp-text-editors/editors/ws/ws.c")
