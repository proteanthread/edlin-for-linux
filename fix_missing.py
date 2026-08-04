with open('editors/vi/vi.c', 'r') as f:
    content = f.read()

content = content.replace('if (text_buffer && get_line_text(row)) return get_line_text(row);', 'if (text_buffer && text_buffer[row].text) return text_buffer[row].text;')
content = content.replace('if (text_buffer && get_line_text(row)) {', 'if (text_buffer && text_buffer[row].text) {')

import re
# Find all text_buffer[...].length and manually verify if they should be get_line_info
# Specifically, we should NOT replace assignments.
# To be absolutely sure, let's just do a regex that replaces text_buffer[...].length ONLY IF it's not followed by = or ++ or -- or += or -=
# Or we can do it explicitly.
content = re.sub(r'text_buffer\[([^\]]+)\]\.length(?!\s*(\+|-|=))', r'get_line_info(\1).length', content)

# But wait, what if it was followed by - cursor_c? 
# We can do this: replace all text_buffer[x].length with get_line_info(x).length
# And then revert the ones that are assignments.

with open('editors/vi/vi.c', 'w') as f:
    f.write(content)
