with open('editors/vi/vi.c', 'r') as f:
    content = f.read()

content = content.replace('get_line_info(row].text', 'text_buffer[row].text')
content = content.replace('text_buffer[row).length', 'text_buffer[row].length')
content = content.replace('free(get_line_text(row));', 'free(text_buffer[row].text);')
content = content.replace('get_line_text(cursor_r)[i] =', 'text_buffer[cursor_r].text[i] =')
content = content.replace('get_line_text(cursor_r + 1].text', 'text_buffer[cursor_r + 1].text')
content = content.replace('&text_buffer[cursor_r)[cursor_c]', '&text_buffer[cursor_r].text[cursor_c]')
content = content.replace('get_line_text(cursor_r)[i - 1] =', 'text_buffer[cursor_r].text[i - 1] =')
content = content.replace('get_line_text(cursor_r - 1].text', 'text_buffer[cursor_r - 1].text')
content = content.replace('text_buffer[cursor_r));', 'text_buffer[cursor_r].text);')
content = content.replace('get_line_text(cursor_r)[i + 1] =', 'text_buffer[cursor_r].text[i + 1] =')

with open('editors/vi/vi.c', 'w') as f:
    f.write(content)
