# -*- coding: utf-8 -*-
"""Rebuild JS_Common_GZ in html.h from common_fixed.js (already patched)."""
import sys, io, gzip, os, re

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')
sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding='utf-8', errors='replace')

base      = os.path.dirname(os.path.abspath(__file__))
js_path   = os.path.join(base, 'common_fixed.js')
html_path = os.path.join(base, 'USBKeylogger', 'html.h')

with open(js_path, 'r', encoding='utf-8') as f:
    js = f.read()
print('common_fixed.js loaded, len=%d chars' % len(js))

# Quick sanity: zh block must end with },' followed by ru
if '},\nru: {' not in js and '},\r\nru: {' not in js:
    print('WARNING: zh block may still be missing trailing comma!')
else:
    print('OK: zh block has trailing comma before ru')

# Gzip compress
buf = io.BytesIO()
with gzip.GzipFile(fileobj=buf, mode='wb', compresslevel=9, mtime=0) as gz:
    gz.write(js.encode('utf-8'))
data = buf.getvalue()
print('Compressed size: %d bytes' % len(data))

# Build C hex array
hex_lines = []
for i in range(0, len(data), 16):
    chunk = data[i:i+16]
    hex_lines.append('  ' + ', '.join('0x%02x' % b for b in chunk) + ',')
array_body = '\n'.join(hex_lines)
new_array = 'const uint8_t JS_Common_GZ[] PROGMEM = {\n' + array_body + '\n};'

# Patch html.h
with open(html_path, 'r', encoding='utf-8', errors='replace') as f:
    html_h = f.read()

patched, n = re.subn(
    r'const uint8_t JS_Common_GZ\[\] PROGMEM = \{[^}]*\};',
    new_array,
    html_h,
    flags=re.DOTALL
)
print('Replacements in html.h: %d' % n)

if n == 0:
    print('ERROR: pattern not found in html.h — no changes written.')
    sys.exit(1)

with open(html_path, 'w', encoding='utf-8') as f:
    f.write(patched)
print('html.h written OK')

# Verify first bytes are valid gzip magic
if data[0] == 0x1f and data[1] == 0x8b:
    print('Gzip magic OK (0x1f 0x8b)')
else:
    print('ERROR: gzip magic wrong: 0x%02x 0x%02x' % (data[0], data[1]))

print('ALL DONE')
