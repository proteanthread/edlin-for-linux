import re
with open('editors/vi/vi.c', 'r') as f:
    code = f.read()

# Replace .length reads
code = re.sub(r'text_buffer\[(.*?)\]\.length(?!\s*(=|\+|-))', r'get_line_info(\1).length', code)

# Fix .text reads for print and similar safe places
code = re.sub(r'text_buffer\[(.*?)\]\.text(?!(\s*=|\s*\[|\s*,))', r'get_line_text(\1)', code)
code = re.sub(r'text_buffer\[(.*?)\]\.text\[(.*?)\](?!(\s*=|\s*\+))', r'get_line_text(\1)[\2]', code)

# Let's restore memmove and strcat and strcpy
code = code.replace('get_line_text(current_lines - 1), line_buf', 'text_buffer[current_lines - 1].text, line_buf')
code = code.replace('memmove(&get_line_text(cursor_r)[len]', 'memmove(&text_buffer[cursor_r].text[len]')
code = code.replace('memmove(get_line_text(cursor_r + 1)', 'memmove(text_buffer[cursor_r + 1].text')
code = code.replace('strcat(get_line_text(cursor_r - 1)', 'strcat(text_buffer[cursor_r - 1].text')
code = code.replace('get_line_text(cursor_r + 1)', 'text_buffer[cursor_r + 1].text')

with open('editors/vi/vi.c', 'w') as f:
    f.write(code)
