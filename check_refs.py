import re

with open('editors/vi/vi.c', 'r') as f:
    lines = f.readlines()

for i, line in enumerate(lines):
    if 'text_buffer[' in line:
        if 'text_buffer[row].text' in line or 'text_buffer[row].length' in line or 'text_buffer[row].capacity' in line or 'text_buffer[row].disk_offset' in line:
            # Inside the paging functions, we allow it. But wait, I added these paging functions.
            if i < 400: # Assuming paging functions are defined early
                pass
        
        # Check for length reads
        if '.length' in line:
            if not any(op in line for op in ['=', '++', '--', '+=', '-=']):
                if 'get_line_info' not in line:
                    if 'if (cursor_c > len) cursor_c = len;' not in line and 'int len = ' not in line:
                        pass # Actually checking visually
        
        if '.text' in line:
            if not any(op in line for op in ['=', 'malloc', 'realloc', 'free', '&']):
                pass
