import io, re, sys

def bal(p):
    s = io.open(p, encoding='utf-8-sig').read()
    s = re.sub(r'//[^\n]*', '', s)
    s = re.sub(r'/\*.*?\*/', '', s, flags=re.S)
    s = re.sub(r'"(?:\\.|[^"\\])*"', '""', s)
    s = re.sub(r"'(?:\\.|[^'\\])*'", "''", s)
    d = 0
    bad = False
    for ch in s:
        if ch == '[':
            d += 1
        elif ch == ']':
            d -= 1
            if d < 0:
                bad = True
    print(('UNBALANCED' if (d != 0 or bad) else 'ok'), p.split('/')[-1], 'depth', d)

for p in sys.argv[1:]:
    bal(p)