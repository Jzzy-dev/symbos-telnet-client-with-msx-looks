#!/usr/bin/env python3
"""Generate a scripted BBS session for tests/hostsim.c.

Mimics the kind of screens in the reference screenshots: telnet option
negotiation, a scrolling login, a menu with big block-letter title, and a
candlestick price chart drawn with CP437 half-blocks and box drawing."""
import random, sys

IAC, DO, WILL, SB, SE = 255, 253, 251, 250, 240
out = bytearray()
def raw(b): out.extend(b)
def s(t): out.extend(t.encode('latin-1') if isinstance(t, str) else t)
def key(k): out.extend(bytes([0xFF, 0x00, k]))   # injected key press
E = '\x1b['

# telnet negotiation
raw(bytes([IAC, DO, 24, IAC, DO, 31, IAC, WILL, 1, IAC, WILL, 3]))
raw(bytes([IAC, SB, 24, 1, IAC, SE]))
s(E + '6n')                                   # cursor position probe

# scrolling login text (forces many scrolls, batched)
for i in range(60):
    s(E + '0;3%dmLine %02d of connection log ... ' % (1 + i % 7, i) + 'x' * (i % 30) + '\r\n')
s(E + '1;37mLogin: ')
for ch in b'guest':
    key(ch)
    s(bytes([ch]))                            # remote echo
key(13)
s('\r\n')

# --- menu screen like screenshot 1 ---
s(E + '0m' + E + '2J')
big = [
    "##### #   # #   # ####   ###   #####",
    "#      # #  ## ## #   # #   # #     ",
    "#####   #   # # # ####  #   # ##### ",
    "    #   #   #   # #   # #   #     # ",
    "#####   #   #   # ####   ###  ##### ",
]
for r, line in enumerate(big):
    s(E + '%d;5H' % (2 + r))
    for c in line:
        s(E + ('0;34m\xdb' if c == '#' else '0m '))
    s(E + '0;37m')
s(E + '4;50H' + E + '1;37m60 ' + E + '0;34mmins')
items = [('B', 'Tabela Periodica'), ('M', 'Mercado Livre'), ('H', 'Day in History'),
         ('C', 'ChatGPT'), ('W', 'Wikipedia'), ('O', 'Tabela Cripto'),
         ('Y', 'Crypto Ticker'), ('S', 'Stock Ticker'), ('P', 'Phys Org'),
         ('A', 'ANSI TV'), ('T', 'Previsao do Tempo'), ('V', 'Painel de Voos')]
for i, (k, name) in enumerate(items):
    r, c = 9 + i // 3, 4 + (i % 3) * 25
    s(E + '%d;%dH' % (r, c) + E + '0;36m(' + E + '1;32m' + k + E + '0;36m) ' + name)
s(E + '15;4H' + E + '0;36m(' + E + '1;32mQ' + E + '0;36m) Quit to Main Menu')
s(E + '17;4H' + E + '1;34mSelection: ' + E + '0;36m')
key(ord('Y'))

# --- candlestick chart like screenshot 2 ---
s(E + '2J' + E + 'H')
s(E + '1;31m.' + E + '1;37mdPR Tick' + E + '1;31m.' + E + '0;37m' + ' ' * 10 +
  'Latest: ' + E + '1;33m$' + E + '1;32m81874.14 ' + E + '0;37m24hr Change: ' + E + '1;32m1.78%' +
  E + '1;1H' + E + '1;72H' + E + '1;31m.' + E + '0;37m? Help' + E + '1;31m.')
s(E + '2;1H' + E + '0;37m\xda' + '\xc4' * 78 + '\xbf')
for r in range(3, 22):
    s(E + '%d;1H\xb3' % r + E + '%d;80H\xb3' % r)
s(E + '22;1H\xc0' + '\xc4' * 78 + '\xd9')
s(E + '2;3H' + E + '1;37mBTC/USDT' + E + '2;30H' + E + '0;31m[' + E + '0;37mInterval: ' +
  E + '1;31m1 Hour' + E + '0;31m]')
random.seed(7)
price = 12.0
for col in range(3, 79):
    o = price; price += random.uniform(-1.6, 1.6); price = max(1, min(37, price))
    c = price
    hi, lo = max(o, c) + random.uniform(0, 1.5), min(o, c) - random.uniform(0, 1.5)
    color = '1;32' if c >= o else '1;31'
    for y2 in range(int(lo), int(hi) + 1):         # y2 in half-rows, 0..37
        row = 21 - y2 // 2
        if row < 3 or row > 21: continue
        body = min(o, c) <= y2 <= max(o, c)
        ch = '\xdd' if body else '\xb3'
        s(E + '%d;%dH' % (row, col) + E + color + 'm' + ch)
s(E + '3;2H' + E + '0;31m[' + E + '1;33m$' + E + '1;32m84118.39' + E + '0;31m]')
s(E + '21;2H' + E + '0;31m[' + E + '1;33m$' + E + '1;32m80025.00' + E + '0;31m]')
labels = ['07:00', '16:00', '01:00', '10:00', '19:00', '04:00', '13:00', '22:00', '07:00']
for i, lab in enumerate(labels):
    s(E + '23;%dH' % (2 + i * 9) + E + '0;37m' + lab)
    s(E + '24;%dH' % (1 + i * 9) + '10/%02d/26' % (13 + (i * 9) // 24))
# status line inside a scroll region (rows 25 only), then reset
s(E + '25;1H' + E + '0;44;1;33m' + ' SymTerm test - 16 colours: ')
for c in range(16):
    s(E + '0;%d;%dm\xdb' % (1 if c >= 8 else 22, 30 + c % 8))
s(E + '0;44;37m \xb0\xb1\xb2\xdb ' + E + 'K' + E + '0m')
s(E + '1;80H')

open(sys.argv[1], 'wb').write(out)
print(len(out), 'bytes')
