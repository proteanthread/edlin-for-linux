import re

with open('editors/vi/vi.c', 'r') as f:
    content = f.read()

# Fix int rem_len = text_buffer[cursor_r].length - cursor_c;
content = content.replace('int rem_len = text_buffer[cursor_r].length - cursor_c;', 'int rem_len = get_line_info(cursor_r).length - cursor_c;')

# Let's check text_buffer[cursor_r].length in handle_normal
content = content.replace('cursor_c = (int)text_buffer[cursor_r].length;', 'cursor_c = (int)get_line_info(cursor_r).length;')
content = content.replace('int len = text_buffer[cursor_r].length;', 'int len = get_line_info(cursor_r).length;')
content = content.replace('int next_len = text_buffer[cursor_r + 1].length;', 'int next_len = get_line_info(cursor_r + 1).length;')
content = content.replace('int prev_len = text_buffer[cursor_r - 1].length;', 'int prev_len = get_line_info(cursor_r - 1).length;')
content = content.replace('int cur_len = text_buffer[cursor_r].length;', 'int cur_len = get_line_info(cursor_r).length;')

with open('editors/vi/vi.c', 'w') as f:
    f.write(content)
