import re

def process(file_path):
    with open(file_path, "r") as f:
        content = f.read()

    content = content.replace(
        'fprintf(file, "%s\\n", text_buffer[i].text);',
        'fprintf(file, "%s\\n", get_line_text(i));'
    )
    
    content = content.replace(
        'strcpy(rem, text_buffer[r2].text + c2);',
        'strcpy(rem, get_line_text(r2) + c2);'
    )
    
    content = content.replace(
        'memmove(text_buffer[cy + 1].text, text_buffer[cy].text + cx, remaining_len + 1);',
        'memmove(text_buffer[cy + 1].text, get_line_text(cy) + cx, remaining_len + 1);'
    )
    
    content = content.replace(
        'memmove(&text_buffer[cy - 1].text[prev_len], text_buffer[cy].text, cur_len + 1);',
        'memmove(&text_buffer[cy - 1].text[prev_len], get_line_text(cy), cur_len + 1);'
    )
    
    content = content.replace(
        'memmove(&text_buffer[cy].text[curl], text_buffer[cy + 1].text, nl + 1);',
        'memmove(&text_buffer[cy].text[curl], get_line_text(cy + 1), nl + 1);'
    )
        
    with open(file_path, "w") as f:
        f.write(content)

process("c:/Users/rtdos/GitHub/bpp-text-editors/editors/ws/ws.c")
