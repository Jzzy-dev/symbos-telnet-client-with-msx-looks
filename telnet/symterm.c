/*
 * SymTerm - ANSI/BBS telnet client for SymbOS (MSX2 512x212, 16 colours)
 *
 * 80x25 text terminal, 6x8 CP437 font, full 16-colour ANSI, drawn into a
 * 480x200 pixel 4bpp bitmap that lives in banked memory (one 1944-byte
 * canvas per text row, two C_IMAGE_EXT controls per row).  Scrolling is done
 * by rotating which canvas each row's controls point at, so no pixels move.
 *
 * Build:  cc symterm.c render.s -lnet -o symterm.exe -N "SymTerm"
 * Usage:  symterm [host[:port]] [port] [-r] [-u] [-p] [-lNN]
 *           -r  raw TCP (no telnet negotiation)
 *           -u  start in UTF-8 mode
 *           -p  use exact ANSI palette (reprograms the palette while focused)
 *           -lNN number of rows (10-25, default 25)
 *
 * Hotkeys while online:  Alt+X disconnect   Alt+U toggle UTF-8/CP437
 *                        Alt+P toggle exact ANSI palette   Alt+E local echo
 */

#include <symbos.h>
#include <string.h>
#include <stdlib.h>
#include <network.h>

#include "font.h"

#ifndef ALIGNPAGE
#define ALIGNPAGE(p) ((char*)(((unsigned short)(p) + 255) & 0xFF00))
#endif

/* ------------------------------------------------------------------ */
/* Geometry                                                            */

#define COLS     80
#define MAXROWS  25
#define CW       6                       /* cell width in pixels  */
#define CH       8                       /* cell height in pixels */
#define BYTEW    (COLS * CW / 2)         /* 240 bytes per pixel line */
#define ROWBYTES (BYTEW * CH)            /* 1920 bytes of pixels per text row */
#define CANVAS   (ROWBYTES + 24)         /* plus extended graphics headers */
#define LEFTW    252                     /* C_IMAGE_EXT max block width */

unsigned char rows = MAXROWS;

/* ------------------------------------------------------------------ */
/* Assembly renderer interface                                         */

extern void tr_render(char* dst, char* text, char* attr, unsigned char n);
char* tr_font3;
char* tr_ctab;
unsigned char tr_curpage;

unsigned char font3[256 * 24];      /* expanded font, see render.s */
unsigned char ctab[1024];
unsigned char curbuf[260];
#define SSTRIDE 256                 /* scratch line stride */
_data char scratchraw[SSTRIDE * CH + 256];
char* scratch;                      /* 256-aligned inside scratchraw */

/* ------------------------------------------------------------------ */
/* Screen state                                                        */

unsigned char tchar[MAXROWS * COLS];
unsigned char tattr[MAXROWS * COLS];

unsigned char rbank[MAXROWS];        /* banked canvas for each buffer   */
char*         raddr[MAXROWS];
unsigned char rowmap[MAXROWS];       /* screen row -> canvas buffer     */
unsigned char nbuf = 0;              /* canvases successfully reserved  */

unsigned char dlo[MAXROWS], dhi[MAXROWS];   /* dirty column span per row */
unsigned char anydirty = 0;
unsigned char mv_lo = 255, mv_hi = 0;       /* rows whose canvas changed */

unsigned char cx = 0, cy = 0;        /* cursor */
unsigned char wrapnext = 0;
unsigned char autowrap = 1;
unsigned char curvis = 1;
unsigned char dcx = 0, dcy = 255;    /* where the cursor is currently drawn */
unsigned char dcvis = 0;
unsigned char stop = 0, sbot = MAXROWS - 1;  /* scroll region */
unsigned char scx = 0, scy = 0;      /* saved cursor */
unsigned char sattr_fg = 7, sattr_bg = 0, sattr_bold = 0;

/* SGR state */
unsigned char fg = 7, bg = 0, bold = 0, blink = 0, rev = 0, conceal = 0;
unsigned char attr = 0x07;           /* effective: (bg << 4) | fg, ANSI indices */

/* ------------------------------------------------------------------ */
/* Window                                                              */

#define NCTRL (2 + 2 * MAXROWS)
#define CID(r) (2 + 2 * (r))

_transfer char icon[19] = {0x02, 0x08, 0x08, 0xF0, 0xF0, 0x87, 0x1E, 0xB4, 0xD2, 0xB6,
                           0xD2, 0xB5, 0xDA, 0x87, 0x1E, 0xF0, 0xF0, 0x10, 0x80};
_transfer Ctrl ctrls[NCTRL];
_transfer Ctrl_Group cgroup;
_transfer Window form;
_transfer char wtitle[72];
_transfer char txbuf[256];
_transfer char rxbuf[1024];

signed char winID = -1;
unsigned char focused = 1;

/* ------------------------------------------------------------------ */
/* Palette                                                             */

/* ANSI colour -> nearest default SymbOS palette entry */
unsigned char pal_near[16] = { 1, 3, 9, 2, 5, 11, 4, 13, 13, 15, 10, 12, 6, 11, 4, 8 };
/* ANSI colour -> distinct entry, reprogrammed to the real ANSI RGB value */
unsigned char pal_true[16] = { 1, 3, 9, 2, 5, 14, 0, 13, 7, 15, 10, 12, 6, 11, 4, 8 };
unsigned short ansi_rgb[16] = {
    0x000, 0xA00, 0x0A0, 0xA50, 0x00A, 0xA0A, 0x0AA, 0xAAA,
    0x555, 0xF55, 0x5F5, 0xFF5, 0x55F, 0xF5F, 0x5FF, 0xFFF };
unsigned short saved_rgb[16];
unsigned char truepal = 0;
unsigned char pal_applied = 0;
unsigned char* pal = pal_near;

/* ------------------------------------------------------------------ */
/* Connection                                                          */

#define ST_PROMPT  0
#define ST_ONLINE  1
unsigned char state = ST_PROMPT;
signed char sock = -1;
unsigned char telnet = 1;
unsigned char rawflag = 0;
unsigned char utf8 = 0;
unsigned char localecho = 0;
unsigned char remote_echo = 0;
unsigned char bin_mode = 0;
char host[66];
char inputline[66];
unsigned char inlen = 0;
unsigned short port = 23;
unsigned short txlen = 0;
unsigned char closing = 0;

void tx_flush(void);
void tx_byte(unsigned char b);
void tx_str(char* s);
void term_puts(char* s);
void term_write(unsigned char c);
void flush(void);
void update_title(void);

/* ================================================================== */
/* Colour table                                                        */

void build_ctab(void) {
    unsigned short a;
    unsigned char f, b;
    unsigned char* p = ctab;
    for (a = 0; a < 256; ++a) {
        f = pal[a & 15];
        b = pal[a >> 4];
        *p++ = (b << 4) | b;
        *p++ = (b << 4) | f;
        *p++ = (f << 4) | b;
        *p++ = (f << 4) | f;
    }
}

void palette_apply(void) {
    unsigned char i;
    if (pal_applied || !truepal) return;
    for (i = 0; i < 16; ++i)
        saved_rgb[i] = Color_Get(i);
    for (i = 0; i < 16; ++i)
        Color_Set(pal_true[i], ansi_rgb[i]);
    pal_applied = 1;
}

void palette_restore(void) {
    unsigned char i;
    if (!pal_applied) return;
    for (i = 0; i < 16; ++i)
        Color_Set(i, saved_rgb[i]);
    pal_applied = 0;
}

void mark_all(void);

void set_truepal(unsigned char on) {
    if (on == truepal) return;
    if (!on) palette_restore();
    truepal = on;
    pal = on ? pal_true : pal_near;
    build_ctab();
    if (on && focused) palette_apply();
    mark_all();
}

/* ================================================================== */
/* Banked canvases                                                     */

void canvas_header(unsigned char r) {
    /* Build the two 10-byte extended-graphics headers + trailer for one row,
       using addresses in the canvas's own bank, then copy it there. */
    unsigned char h[24];
    char* base = raddr[r];
    unsigned short data = (unsigned short)base + 24;
    unsigned short enc = (unsigned short)base + 23;
    memset(h, 0, 24);
    /* left block */
    h[0] = BYTEW; h[1] = LEFTW; h[2] = CH;
    h[3] = data & 255; h[4] = data >> 8;
    h[5] = enc & 255;  h[6] = enc >> 8;
    h[7] = ROWBYTES & 255; h[8] = ROWBYTES >> 8;
    /* right block */
    data += LEFTW / 2;
    h[10] = BYTEW; h[11] = COLS * CW - LEFTW; h[12] = CH;
    h[13] = data & 255; h[14] = data >> 8;
    h[15] = enc & 255;  h[16] = enc >> 8;
    h[17] = ROWBYTES & 255; h[18] = ROWBYTES >> 8;
    h[20] = (COLS * CW - 1) & 255; h[21] = (COLS * CW - 1) >> 8;
    h[22] = CH - 1;
    h[23] = 5;          /* encoding: 16 colours */
    Bank_Copy(rbank[r], base, _symbank, (char*)h, 24);
}

void ctrl_point(unsigned char r) {
    unsigned char b = rowmap[r];
    Ctrl* c = &ctrls[CID(r)];
    c->bank = rbank[b];
    c->param = (unsigned short)raddr[b];
    c++;
    c->bank = rbank[b];
    c->param = (unsigned short)raddr[b] + 10;
}

void release_canvases(void) {
    while (nbuf) {
        --nbuf;
        Mem_Release(rbank[nbuf], raddr[nbuf], CANVAS);
    }
}

unsigned char alloc_canvases(void) {
    unsigned char r;
    for (r = 0; r < rows; ++r) {
        if (Mem_Reserve(0, 1, CANVAS, &rbank[r], &raddr[r]))
            return 1;
        ++nbuf;
        canvas_header(r);
        rowmap[r] = r;
    }
    return 0;
}

/* ================================================================== */
/* Window setup                                                        */

void setup_window(void) {
    unsigned char r;
    unsigned short sw, sh, ph;
    Ctrl* c;

    memset(ctrls, 0, sizeof(ctrls));
    /* backgrounds for any area outside the 480 x rows*8 terminal */
    c = &ctrls[0];
    c->value = 0; c->type = C_AREA; c->bank = -1; c->param = COLOR_BLACK | AREA_16COLOR;
    c->x = COLS * CW; c->y = 0; c->w = 10000; c->h = 10000;
    c = &ctrls[1];
    c->value = 1; c->type = C_AREA; c->bank = -1; c->param = COLOR_BLACK | AREA_16COLOR;
    c->x = 0; c->y = rows * CH; c->w = COLS * CW; c->h = 10000;
    for (r = 0; r < rows; ++r) {
        c = &ctrls[CID(r)];
        c->value = CID(r); c->type = C_IMAGE_EXT;
        c->x = 0; c->y = r * CH; c->w = LEFTW; c->h = CH;
        c++;
        c->value = CID(r) + 1; c->type = C_IMAGE_EXT;
        c->x = LEFTW; c->y = r * CH; c->w = COLS * CW - LEFTW; c->h = CH;
        ctrl_point(r);
    }

    memset(&cgroup, 0, sizeof(cgroup));
    cgroup.controls = 2 + 2 * rows;
    cgroup.pid = _sympid;
    cgroup.first = &ctrls[0];

    sw = Screen_Width();
    sh = Screen_Height();
    ph = rows * CH;

    memset(&form, 0, sizeof(form));
    form.state = WIN_MAXIMIZED;
    form.flags = WIN_CLOSE | WIN_TITLE | WIN_ICON | WIN_RESIZABLE;
    form.pid = _sympid;
    form.x = 0; form.y = 0;
    form.w = COLS * CW;
    form.h = (sh > 40 && ph > sh - 40) ? sh - 40 : ph;
    if (sw < COLS * CW + 16) form.w = sw - 16;
    form.wfull = COLS * CW; form.hfull = ph;
    form.wmin = 96; form.hmin = 48;
    form.wmax = COLS * CW; form.hmax = ph;
    form.icon = icon;
    form.title = wtitle;
    form.controls = &cgroup;
}

/* ================================================================== */
/* Dirty tracking & display                                            */

void mark(unsigned char r, unsigned char c0, unsigned char c1) {
    if (dlo[r] > c0) dlo[r] = c0;
    if (dhi[r] < c1) dhi[r] = c1;
    anydirty = 1;
}

void mark_all(void) {
    unsigned char r;
    for (r = 0; r < rows; ++r) { dlo[r] = 0; dhi[r] = COLS - 1; }
    mv_lo = 0; mv_hi = rows - 1;
    anydirty = 1;
}

void clear_dirty(void) {
    memset(dlo, 255, sizeof(dlo));
    memset(dhi, 0, sizeof(dhi));
}

void render_row(unsigned char r) {
    unsigned char c0 = dlo[r], c1 = dhi[r], n, l, b;
    unsigned short off;
    char* src;
    char* dst;

    n = c1 - c0 + 1;
    off = (unsigned short)r * COLS + c0;
    tr_render(scratch + c0 * 3, (char*)tchar + off, (char*)tattr + off, n);

    /* cursor: 2-pixel underline in the cell's foreground colour */
    if (r == cy && curvis && cx >= c0 && cx <= c1) {
        unsigned char v = ctab[(unsigned short)tattr[(unsigned short)r * COLS + cx] * 4 + 3];
        dst = scratch + 6 * SSTRIDE + cx * 3;
        dst[0] = v; dst[1] = v; dst[2] = v;
        dst += SSTRIDE;
        dst[0] = v; dst[1] = v; dst[2] = v;
        dcx = cx; dcy = cy; dcvis = 1;
    }

    /* copy the changed span of each pixel line into the row's canvas */
    b = rowmap[r];
    dst = raddr[b] + 24 + c0 * 3;
    src = scratch + c0 * 3;
    for (l = 0; l < CH; ++l) {
        Bank_Copy(rbank[b], dst, _symbank, src, n * 3);
        dst += BYTEW;
        src += SSTRIDE;
    }
}

void build_font3(void) {
    unsigned short ch;
    unsigned char l, v;
    unsigned char* s = tr_fontdata;
    unsigned char* d = font3;
    for (ch = 0; ch < 256; ++ch) {
        for (l = 0; l < CH; ++l) {
            v = *s++;
            if (l & 1) {
                *d++ = v & 3; *d++ = (v >> 2) & 3; *d++ = (v >> 4) & 3;
            } else {
                *d++ = (v >> 4) & 3; *d++ = (v >> 2) & 3; *d++ = v & 3;
            }
        }
    }
}

void redraw_rows(unsigned char r0, unsigned char r1, unsigned char c0, unsigned char c1) {
    /* redraw screen rows r0..r1 (<= 8 rows), columns c0..c1 */
    unsigned char n = (r1 - r0 + 1) * 2;
    if (focused && form.xscroll == 0 && form.yscroll == 0) {
        Win_Redraw_Area(winID, (unsigned char)(-(signed char)n), CID(r0),
                        c0 * CW, r0 * CH, (c1 - c0 + 1) * CW, (r1 - r0 + 1) * CH);
    } else if (focused) {
        Win_Redraw(winID, -(signed char)n, CID(r0));
    } else {
        Win_Redraw_Ext(winID, -(signed char)n, CID(r0));
    }
}

void flush(void) {
    unsigned char r, r0, c0, c1, cnt, total;

    if (winID < 0) return;

    /* cursor movement */
    if (curvis != dcvis || cx != dcx || cy != dcy) {
        if (dcvis && dcy < rows) mark(dcy, dcx, dcx);
        dcvis = 0;
        if (curvis) mark(cy, cx, cx);
    }
    if (!anydirty) return;

    total = 0;
    for (r = 0; r < rows; ++r) {
        if (dlo[r] != 255) {
            render_row(r);
            ++total;
        } else if (r >= mv_lo && r <= mv_hi) {
            ++total;
        }
    }

    if (total > 8) {
        if (focused) Win_Redraw(winID, -1, 0);
        else Win_Redraw_Ext(winID, -1, 0);
    } else {
        /* moved rows: redraw whole rows, in groups of up to 8 */
        if (mv_lo <= mv_hi) {
            for (r = mv_lo; r <= mv_hi; r += 8) {
                r0 = (mv_hi - r >= 8) ? r + 7 : mv_hi;
                redraw_rows(r, r0, 0, COLS - 1);
                dlo[r] = 255;
            }
            for (r = mv_lo; r <= mv_hi; ++r) dlo[r] = 255;
        }
        /* dirty rows: merge consecutive rows into one area redraw */
        r = 0;
        while (r < rows) {
            if (dlo[r] == 255) { ++r; continue; }
            r0 = r; c0 = dlo[r]; c1 = dhi[r]; cnt = 1;
            ++r;
            while (r < rows && dlo[r] != 255 && cnt < 8) {
                if (dlo[r] < c0) c0 = dlo[r];
                if (dhi[r] > c1) c1 = dhi[r];
                ++r; ++cnt;
            }
            redraw_rows(r0, r - 1, c0, c1);
        }
    }

    clear_dirty();
    mv_lo = 255; mv_hi = 0;
    anydirty = 0;

    /* keep the cursor row visible if the window is shorter than the screen */
    {
        unsigned short vh = Win_Height(&form);
        unsigned short y = (unsigned short)cy * CH;
        if (vh >= CH && vh < (unsigned short)rows * CH) {
            if (y < form.yscroll)
                Win_ContentY(winID, y);
            else if (y + CH > form.yscroll + vh)
                Win_ContentY(winID, y + CH - vh);
        }
    }
}

/* ================================================================== */
/* Terminal operations                                                 */

void calc_attr(void) {
    unsigned char f = fg, b = bg;
    if (bold && f < 8) f += 8;
    if (rev) { unsigned char t = f; f = b; b = t; }
    if (conceal) f = b;
    attr = (b << 4) | f;
}

void clear_cells(unsigned char r, unsigned char c0, unsigned char c1) {
    unsigned short off = (unsigned short)r * COLS + c0;
    unsigned char n = c1 - c0 + 1;
    memset(tchar + off, ' ', n);
    memset(tattr + off, bg << 4 | 7, n);
    mark(r, c0, c1);
}

void scroll_up(unsigned char top, unsigned char bot, unsigned char n) {
    unsigned char r, b;
    if (n == 0) return;
    if (n > bot - top + 1) n = bot - top + 1;
    while (n--) {
        b = rowmap[top];
        for (r = top; r < bot; ++r) {
            rowmap[r] = rowmap[r + 1];
            dlo[r] = dlo[r + 1];
            dhi[r] = dhi[r + 1];
        }
        rowmap[bot] = b;
        memmove(tchar + (unsigned short)top * COLS, tchar + (unsigned short)(top + 1) * COLS,
                (unsigned short)(bot - top) * COLS);
        memmove(tattr + (unsigned short)top * COLS, tattr + (unsigned short)(top + 1) * COLS,
                (unsigned short)(bot - top) * COLS);
        dlo[bot] = 255; dhi[bot] = 0;
        clear_cells(bot, 0, COLS - 1);
        if (dcy != 255 && dcy >= top && dcy <= bot) {
            if (dcy == top) dcy = 255; else --dcy;
        }
    }
    for (r = top; r <= bot; ++r) ctrl_point(r);
    if (top < mv_lo) mv_lo = top;
    if (bot > mv_hi) mv_hi = bot;
    anydirty = 1;
}

void scroll_down(unsigned char top, unsigned char bot, unsigned char n) {
    unsigned char r, b;
    if (n == 0) return;
    if (n > bot - top + 1) n = bot - top + 1;
    while (n--) {
        b = rowmap[bot];
        for (r = bot; r > top; --r) {
            rowmap[r] = rowmap[r - 1];
            dlo[r] = dlo[r - 1];
            dhi[r] = dhi[r - 1];
        }
        rowmap[top] = b;
        memmove(tchar + (unsigned short)(top + 1) * COLS, tchar + (unsigned short)top * COLS,
                (unsigned short)(bot - top) * COLS);
        memmove(tattr + (unsigned short)(top + 1) * COLS, tattr + (unsigned short)top * COLS,
                (unsigned short)(bot - top) * COLS);
        dlo[top] = 255; dhi[top] = 0;
        clear_cells(top, 0, COLS - 1);
        if (dcy != 255 && dcy >= top && dcy <= bot) {
            if (dcy == bot) dcy = 255; else ++dcy;
        }
    }
    for (r = top; r <= bot; ++r) ctrl_point(r);
    if (top < mv_lo) mv_lo = top;
    if (bot > mv_hi) mv_hi = bot;
    anydirty = 1;
}

void linefeed(void) {
    if (cy == sbot) scroll_up(stop, sbot, 1);
    else if (cy < rows - 1) ++cy;
}

void clear_screen(void) {
    unsigned char r;
    for (r = 0; r < rows; ++r) clear_cells(r, 0, COLS - 1);
}

void term_reset(void) {
    fg = 7; bg = 0; bold = 0; blink = 0; rev = 0; conceal = 0;
    calc_attr();
    stop = 0; sbot = rows - 1;
    cx = 0; cy = 0; wrapnext = 0; autowrap = 1; curvis = 1;
    clear_screen();
}

void put_glyph(unsigned char c) {
    unsigned short off;
    if (wrapnext) {
        wrapnext = 0;
        if (autowrap) { cx = 0; linefeed(); }
    }
    off = (unsigned short)cy * COLS + cx;
    tchar[off] = c;
    tattr[off] = attr;
    mark(cy, cx, cx);
    if (cx < COLS - 1) ++cx;
    else wrapnext = 1;
}

void goto_xy(int x, int y) {
    if (x < 0) x = 0;
    if (x >= COLS) x = COLS - 1;
    if (y < 0) y = 0;
    if (y >= rows) y = rows - 1;
    cx = x; cy = y; wrapnext = 0;
}

/* ------------------------------------------------------------------ */
/* ANSI escape parser                                                  */

#define P_NORMAL 0
#define P_ESC    1
#define P_CSI    2
#define P_SKIP1  3
#define P_OSC    4
#define P_OSCESC 5

#define MAXPAR 10
unsigned char pstate = P_NORMAL;
unsigned short par[MAXPAR];
unsigned char npar = 0;
unsigned char pprivate = 0;
unsigned char lastglyph = ' ';

/* UTF-8 decoder */
unsigned short u8cp = 0;
unsigned char u8need = 0;

void reply(char* s) {
    if (state == ST_ONLINE) { tx_str(s); tx_flush(); }
}

void reply_num(unsigned short n) {
    char b[8];
    itoa(n, b, 10);
    tx_str(b);
}

unsigned char map256(unsigned short n) {
    /* xterm 256-colour index -> nearest ANSI 16 */
    unsigned char r, g, b, hi;
    if (n < 16) return n;
    if (n >= 232) {
        n -= 232;
        if (n < 6) return 0;
        if (n < 12) return 8;
        if (n < 20) return 7;
        return 15;
    }
    n -= 16;
    r = n / 36; g = (n / 6) % 6; b = n % 6;
    hi = (r > 3 || g > 3 || b > 3);
    r = r >= 2; g = g >= 2; b = b >= 2;
    n = (b << 2) | (g << 1) | r;
    if (n == 0 && hi) return 8;
    return n + (hi ? 8 : 0);
}

void do_sgr(void) {
    unsigned char i;
    unsigned short p;
    if (npar == 0) { par[0] = 0; npar = 1; }
    for (i = 0; i < npar; ++i) {
        p = par[i];
        if (p == 0) { fg = 7; bg = 0; bold = 0; blink = 0; rev = 0; conceal = 0; }
        else if (p == 1) bold = 1;
        else if (p == 2 || p == 22) bold = 0;
        else if (p == 5 || p == 6) blink = 1;
        else if (p == 25) blink = 0;
        else if (p == 7) rev = 1;
        else if (p == 27) rev = 0;
        else if (p == 8) conceal = 1;
        else if (p == 28) conceal = 0;
        else if (p >= 30 && p <= 37) fg = p - 30;
        else if (p == 39) fg = 7;
        else if (p >= 40 && p <= 47) bg = p - 40;
        else if (p == 49) bg = 0;
        else if (p >= 90 && p <= 97) fg = p - 90 + 8;
        else if (p >= 100 && p <= 107) bg = p - 100 + 8;
        else if (p == 38 || p == 48) {
            unsigned char col = 255;
            if (i + 2 < npar && par[i + 1] == 5) {
                col = map256(par[i + 2]); i += 2;
            } else if (i + 4 < npar && par[i + 1] == 2) {
                unsigned char r = par[i + 2] > 127, g = par[i + 3] > 127, b = par[i + 4] > 127;
                unsigned char hi = par[i + 2] > 191 || par[i + 3] > 191 || par[i + 4] > 191;
                col = (b << 2) | (g << 1) | r;
                if (hi) col += 8;
                i += 4;
            }
            if (col != 255) {
                if (p == 38) { fg = col; if (col >= 8) bold = 0; }
                else bg = col;
            }
        }
    }
    calc_attr();
}

void ins_chars(unsigned char n) {
    unsigned short off = (unsigned short)cy * COLS;
    if (n > COLS - cx) n = COLS - cx;
    memmove(tchar + off + cx + n, tchar + off + cx, COLS - cx - n);
    memmove(tattr + off + cx + n, tattr + off + cx, COLS - cx - n);
    clear_cells(cy, cx, cx + n - 1);
    mark(cy, cx, COLS - 1);
}

void del_chars(unsigned char n) {
    unsigned short off = (unsigned short)cy * COLS;
    if (n > COLS - cx) n = COLS - cx;
    memmove(tchar + off + cx, tchar + off + cx + n, COLS - cx - n);
    memmove(tattr + off + cx, tattr + off + cx + n, COLS - cx - n);
    clear_cells(cy, COLS - n, COLS - 1);
    mark(cy, cx, COLS - 1);
}

void do_csi(unsigned char f) {
    unsigned short p1 = npar > 0 ? par[0] : 0;
    unsigned short n = p1 ? p1 : 1;
    unsigned char r;

    if (pprivate) {
        if ((f == 'h' || f == 'l') && npar) {
            for (r = 0; r < npar; ++r) {
                if (par[r] == 25) curvis = (f == 'h');
                else if (par[r] == 7) autowrap = (f == 'h');
            }
        }
        return;
    }
    switch (f) {
    case 'A': goto_xy(cx, (int)cy - (int)n); break;
    case 'B': case 'e': goto_xy(cx, cy + n); break;
    case 'C': case 'a': goto_xy(cx + n, cy); break;
    case 'D': goto_xy((int)cx - (int)n, cy); break;
    case 'E': goto_xy(0, cy + n); break;
    case 'F': goto_xy(0, (int)cy - (int)n); break;
    case 'G': case '`': goto_xy(n - 1, cy); break;
    case 'd': goto_xy(cx, n - 1); break;
    case 'H': case 'f':
        goto_xy((npar > 1 && par[1]) ? par[1] - 1 : 0, p1 ? p1 - 1 : 0);
        break;
    case 'J':
        if (p1 == 0) {
            clear_cells(cy, cx, COLS - 1);
            for (r = cy + 1; r < rows; ++r) clear_cells(r, 0, COLS - 1);
        } else if (p1 == 1) {
            for (r = 0; r < cy; ++r) clear_cells(r, 0, COLS - 1);
            clear_cells(cy, 0, cx);
        } else {
            clear_screen();
            goto_xy(0, 0);       /* ANSI.SYS homes the cursor on ESC[2J */
        }
        break;
    case 'K':
        if (p1 == 0) clear_cells(cy, cx, COLS - 1);
        else if (p1 == 1) clear_cells(cy, 0, cx);
        else clear_cells(cy, 0, COLS - 1);
        break;
    case 'L': if (cy >= stop && cy <= sbot) scroll_down(cy, sbot, n); break;
    case 'M': if (cy >= stop && cy <= sbot) scroll_up(cy, sbot, n); break;
    case 'S': scroll_up(stop, sbot, n); break;
    case 'T': scroll_down(stop, sbot, n); break;
    case '@': ins_chars(n); break;
    case 'P': del_chars(n); break;
    case 'X':
        if (n > COLS - cx) n = COLS - cx;
        clear_cells(cy, cx, cx + n - 1);
        break;
    case 'b':
        while (n--) put_glyph(lastglyph);
        break;
    case 'm': do_sgr(); break;
    case 'r': {
        unsigned char t = p1 ? p1 - 1 : 0;
        unsigned char b = (npar > 1 && par[1]) ? par[1] - 1 : rows - 1;
        if (b >= rows) b = rows - 1;
        if (t < b) { stop = t; sbot = b; goto_xy(0, 0); }
        break;
    }
    case 's': scx = cx; scy = cy; break;
    case 'u': goto_xy(scx, scy); break;
    case 'n':
        if (p1 == 6) {
            tx_str("\033["); reply_num(cy + 1); tx_byte(';'); reply_num(cx + 1); tx_byte('R');
            tx_flush();
        } else if (p1 == 5) reply("\033[0n");
        break;
    case 'c': reply("\033[?1;2c"); break;
    }
}

void term_write(unsigned char c) {
    switch (pstate) {
    case P_NORMAL:
        if (c >= 32) {
            if (c >= 128 && utf8) {
                /* UTF-8 decoding: map to CP437 */
                if ((c & 0xC0) == 0x80) {
                    if (u8need) {
                        u8cp = (u8cp << 6) | (c & 0x3F);
                        if (--u8need == 0) {
                            unsigned short i;
                            unsigned char g = '?';
                            if (u8cp < 128) g = u8cp;
                            else if (u8cp == 0x2018 || u8cp == 0x2019) g = '\'';
                            else if (u8cp == 0x201C || u8cp == 0x201D) g = '"';
                            else if (u8cp == 0x2013 || u8cp == 0x2014) g = '-';
                            else if (u8cp == 0x2026) g = '.';
                            else for (i = 1; i < 256; ++i)
                                if (tr_uni[i] == u8cp) { g = i; break; }
                            lastglyph = g;
                            put_glyph(g);
                        }
                    }
                    return;
                }
                if ((c & 0xE0) == 0xC0) { u8cp = c & 0x1F; u8need = 1; return; }
                if ((c & 0xF0) == 0xE0) { u8cp = c & 0x0F; u8need = 2; return; }
                if ((c & 0xF8) == 0xF0) { u8cp = 0; u8need = 3; return; }
                u8need = 0;
                return;
            }
            u8need = 0;
            lastglyph = c;
            put_glyph(c);
            return;
        }
        u8need = 0;
        switch (c) {
        case 27: pstate = P_ESC; break;
        case 13: cx = 0; wrapnext = 0; break;
        case 10: case 11: wrapnext = 0; linefeed(); break;
        case 8: if (cx) --cx; wrapnext = 0; break;
        case 9:
            cx = (cx + 8) & ~7;
            if (cx >= COLS) cx = COLS - 1;
            wrapnext = 0;
            break;
        case 12: clear_screen(); goto_xy(0, 0); break;
        case 7: case 0: case 14: case 15: break;
        default: put_glyph(c); break;   /* CP437 control-range glyphs */
        }
        break;

    case P_ESC:
        pstate = P_NORMAL;
        switch (c) {
        case '[': pstate = P_CSI; npar = 0; pprivate = 0; par[0] = 0; break;
        case ']': pstate = P_OSC; break;
        case '(': case ')': case '#': pstate = P_SKIP1; break;
        case '7': scx = cx; scy = cy; sattr_fg = fg; sattr_bg = bg; sattr_bold = bold; break;
        case '8': goto_xy(scx, scy); fg = sattr_fg; bg = sattr_bg; bold = sattr_bold; calc_attr(); break;
        case 'D': linefeed(); break;
        case 'E': cx = 0; linefeed(); break;
        case 'M':
            if (cy == stop) scroll_down(stop, sbot, 1);
            else if (cy) --cy;
            break;
        case 'c': term_reset(); break;
        }
        break;

    case P_CSI:
        if (c >= '0' && c <= '9') {
            if (npar == 0) npar = 1;
            if (npar <= MAXPAR) par[npar - 1] = par[npar - 1] * 10 + (c - '0');
        } else if (c == ';') {
            if (npar == 0) npar = 1;
            if (npar < MAXPAR) par[npar] = 0;
            ++npar;
        } else if (c == '?' || c == '=' || c == '>' || c == '<') {
            pprivate = 1;
        } else if (c >= 0x40 && c <= 0x7E) {
            if (npar > MAXPAR) npar = MAXPAR;
            pstate = P_NORMAL;
            do_csi(c);
        } else if (c == 27) {
            pstate = P_ESC;
        } else if (c < 32) {
            /* control chars inside CSI are executed */
            pstate = P_NORMAL; term_write(c); pstate = P_CSI;
        }
        break;

    case P_SKIP1:
        pstate = P_NORMAL;
        break;

    case P_OSC:
        if (c == 7) pstate = P_NORMAL;
        else if (c == 27) pstate = P_OSCESC;
        break;
    case P_OSCESC:
        pstate = (c == '\\') ? P_NORMAL : P_OSC;
        break;
    }
}

void term_puts(char* s) {
    while (*s) term_write((unsigned char)*s++);
}

/* ================================================================== */
/* Telnet                                                              */

#define IAC  255
#define DONT 254
#define DO   253
#define WONT 252
#define WILL 251
#define SB   250
#define SE   240
#define OPT_BINARY 0
#define OPT_ECHO   1
#define OPT_SGA    3
#define OPT_TTYPE  24
#define OPT_NAWS   31

#define T_DATA 0
#define T_IAC  1
#define T_OPT  2
#define T_SB   3
#define T_SBIAC 4
unsigned char tstate = T_DATA;
unsigned char tcmd = 0;
unsigned char sbbuf[8];
unsigned char sblen = 0;
unsigned char we_naws = 0, we_ttype = 0, we_sga = 0, we_bin = 0;
unsigned char they_sga = 0;

void tx_flush(void) {
    if (txlen && sock >= 0) TCP_Send(sock, _symbank, txbuf, txlen);
    txlen = 0;
}

void tx_byte(unsigned char b) {
    if (txlen >= sizeof(txbuf)) tx_flush();
    txbuf[txlen++] = b;
}

void tx_str(char* s) {
    while (*s) tx_byte((unsigned char)*s++);
}

void tx_data(unsigned char b) {
    /* data byte from the user: escape IAC */
    tx_byte(b);
    if (b == IAC && telnet) tx_byte(IAC);
}

void tel_cmd(unsigned char cmd, unsigned char opt) {
    tx_byte(IAC); tx_byte(cmd); tx_byte(opt);
}

void send_naws(void) {
    tx_byte(IAC); tx_byte(SB); tx_byte(OPT_NAWS);
    tx_byte(0); tx_byte(COLS);
    tx_byte(0); tx_byte(rows);
    tx_byte(IAC); tx_byte(SE);
}

void tel_option(unsigned char cmd, unsigned char opt) {
    switch (cmd) {
    case DO:
        if (opt == OPT_NAWS) {
            if (!we_naws) { we_naws = 1; tel_cmd(WILL, opt); }
            send_naws();
        } else if (opt == OPT_TTYPE) {
            if (!we_ttype) { we_ttype = 1; tel_cmd(WILL, opt); }
        } else if (opt == OPT_SGA) {
            if (!we_sga) { we_sga = 1; tel_cmd(WILL, opt); }
        } else if (opt == OPT_BINARY) {
            if (!we_bin) { we_bin = 1; bin_mode = 1; tel_cmd(WILL, opt); }
        } else {
            tel_cmd(WONT, opt);
        }
        break;
    case DONT:
        if (opt == OPT_NAWS && we_naws) { we_naws = 0; tel_cmd(WONT, opt); }
        else if (opt == OPT_TTYPE && we_ttype) { we_ttype = 0; tel_cmd(WONT, opt); }
        else if (opt == OPT_SGA && we_sga) { we_sga = 0; tel_cmd(WONT, opt); }
        else if (opt == OPT_BINARY && we_bin) { we_bin = 0; bin_mode = 0; tel_cmd(WONT, opt); }
        break;
    case WILL:
        if (opt == OPT_ECHO) {
            if (!remote_echo) { remote_echo = 1; tel_cmd(DO, opt); }
        } else if (opt == OPT_SGA) {
            if (!they_sga) { they_sga = 1; tel_cmd(DO, opt); }
        } else if (opt == OPT_BINARY) {
            tel_cmd(DO, opt);
        } else {
            tel_cmd(DONT, opt);
        }
        break;
    case WONT:
        if (opt == OPT_ECHO && remote_echo) { remote_echo = 0; tel_cmd(DONT, opt); }
        else if (opt == OPT_SGA && they_sga) { they_sga = 0; tel_cmd(DONT, opt); }
        break;
    }
    tx_flush();
}

void tel_subneg(void) {
    if (sblen >= 2 && sbbuf[0] == OPT_TTYPE && sbbuf[1] == 1) {
        tx_byte(IAC); tx_byte(SB); tx_byte(OPT_TTYPE); tx_byte(0);
        tx_str("ANSI");
        tx_byte(IAC); tx_byte(SE);
        tx_flush();
    }
}

void net_byte(unsigned char c) {
    if (!telnet) { term_write(c); return; }
    switch (tstate) {
    case T_DATA:
        if (c == IAC) tstate = T_IAC;
        else term_write(c);
        break;
    case T_IAC:
        if (c == IAC) { tstate = T_DATA; term_write(c); }
        else if (c >= WILL) { tcmd = c; tstate = T_OPT; }
        else if (c == SB) { tstate = T_SB; sblen = 0; }
        else tstate = T_DATA;
        break;
    case T_OPT:
        tel_option(tcmd, c);
        tstate = T_DATA;
        break;
    case T_SB:
        if (c == IAC) tstate = T_SBIAC;
        else if (sblen < sizeof(sbbuf)) sbbuf[sblen++] = c;
        break;
    case T_SBIAC:
        if (c == SE) { tel_subneg(); tstate = T_DATA; }
        else if (c == IAC) { if (sblen < sizeof(sbbuf)) sbbuf[sblen++] = c; tstate = T_SB; }
        else tstate = T_SB;
        break;
    }
}

/* ================================================================== */
/* UI                                                                  */

void color_str(unsigned char f) {
    char b[12];
    strcpy(b, "\033[");
    if (f >= 8) { strcat(b, "1;"); f -= 8; } else strcat(b, "0;");
    itoa(30 + f, b + strlen(b), 10);
    strcat(b, "m");
    term_puts(b);
}

void update_title(void) {
    strcpy(wtitle, "SymTerm");
    if (state == ST_ONLINE) {
        strcat(wtitle, " - ");
        strcat(wtitle, host);
    }
    strcat(wtitle, utf8 ? "  [UTF-8]" : "  [CP437]");
    if (truepal) strcat(wtitle, " [PAL]");
    if (localecho) strcat(wtitle, " [ECHO]");
    if (winID >= 0) Win_Redraw_Title(winID);
}

void banner(void) {
    unsigned char i;
    term_puts("\033[0m\033[2J");
    term_puts("\033[1;36m\xC9");
    for (i = 0; i < 46; ++i) term_write(0xCD);
    term_puts("\xBB\r\n\xBA \033[1;33mSymTerm\033[0;37m  ANSI/BBS Telnet for SymbOS        \033[1;36m\xBA\r\n\xBA \033[0;37m80x");
    {
        char b[4]; itoa(rows, b, 10); term_puts(b);
    }
    term_puts(" text, 512x212 16-colour, CP437 font   \033[1;36m\xBA\r\n\xC8");
    for (i = 0; i < 46; ++i) term_write(0xCD);
    term_puts("\xBC\r\n\r\n ");
    for (i = 0; i < 16; ++i) {
        term_puts("\033[0m");
        color_str(i);
        term_write(0xDB); term_write(0xDB);
    }
    term_puts("\033[0m  \xB0\xB1\xB2\xDB\xDF\xDC\xDD\xDE\r\n\r\n");
    term_puts("\033[0;37m Keys: \033[1;37mAlt+X\033[0;37m hang up  \033[1;37mAlt+U\033[0;37m UTF-8/CP437  "
              "\033[1;37mAlt+P\033[0;37m exact palette  \033[1;37mAlt+E\033[0;37m echo\r\n\r\n");
}

void prompt(void) {
    term_puts("\033[0m\r\n\033[1;32mHost[:port]\033[0;37m> \033[1;37m");
    if (host[0]) {
        strcpy(inputline, host);
        if (port != 23) {
            strcat(inputline, ":");
            itoa(port, inputline + strlen(inputline), 10);
        }
        inlen = strlen(inputline);
        term_puts(inputline);
    } else {
        inlen = 0;
        inputline[0] = 0;
    }
    flush();
}

void neterr_msg(char* what) {
    char b[8];
    term_puts("\033[1;31m*** ");
    term_puts(what);
    term_puts(" failed (error ");
    itoa(_neterr, b, 10);
    term_puts(b);
    term_puts(")\033[0m\r\n");
}

void disconnect(unsigned char by_us) {
    if (sock >= 0) {
        tx_flush();
        if (by_us) TCP_Disconnect(sock);
        else TCP_Close(sock);
    }
    sock = -1;
    txlen = 0;
    state = ST_PROMPT;
    term_puts("\033[0m\r\n\033[1;33m*** Disconnected\033[0m\r\n");
    curvis = 1;
    update_title();
    prompt();
}

void do_connect(void) {
    char ip[4];
    char ipstr[16];
    char portstr[8];
    char* p;

    /* parse "host", "host:port" or "host port" */
    strcpy(host, inputline);
    port = 23;
    p = strchr(host, ':');
    if (!p) p = strchr(host, ' ');
    if (p) {
        *p++ = 0;
        while (*p == ' ') ++p;
        if (*p) port = atoi(p);
        if (!port) port = 23;
    }
    while (host[0] && host[strlen(host) - 1] == ' ') host[strlen(host) - 1] = 0;
    if (!host[0]) { prompt(); return; }

    term_puts("\033[0m\r\n\033[0;37mResolving \033[1;37m");
    term_puts(host);
    term_puts("\033[0;37m...\r\n");
    flush();
    if (DNS_Resolve(_symbank, host, ip)) {
        neterr_msg("DNS lookup");
        prompt();
        return;
    }
    iptoa(ip, ipstr);
    itoa(port, portstr, 10);
    term_puts("Connecting to ");
    term_puts(ipstr);
    term_puts(":");
    term_puts(portstr);
    term_puts("...\r\n");
    flush();

    sock = TCP_OpenClient(ip, -1, port);
    if (sock < 0) {
        neterr_msg("Connect");
        prompt();
        return;
    }
    term_puts("\033[1;32mConnected.\033[0m\r\n");
    state = ST_ONLINE;
    telnet = !rawflag;
    tstate = T_DATA; pstate = P_NORMAL;
    we_naws = we_ttype = we_sga = we_bin = 0; they_sga = 0;
    remote_echo = 0; bin_mode = 0; closing = 0;
    if (telnet) {
        /* offer the usual BBS options up front */
        we_naws = 1; tel_cmd(WILL, OPT_NAWS);
        they_sga = 1; tel_cmd(DO, OPT_SGA);
        tx_flush();
    }
    update_title();
    flush();
}

void prompt_key(unsigned char k) {
    if (k == 13) {
        inputline[inlen] = 0;
        do_connect();
    } else if (k == 8 || k == 127) {
        if (inlen) {
            --inlen;
            term_puts("\b \b");
        }
    } else if (k == 27) {
        while (inlen) { --inlen; term_puts("\b \b"); }
    } else if (k >= 32 && k < 127 && inlen < sizeof(inputline) - 1) {
        inputline[inlen++] = k;
        term_write(k);
    }
    flush();
}

void online_key(unsigned char k) {
    char* seq = 0;
    switch (k) {
    case KEY_UP:    seq = "\033[A"; break;
    case KEY_DOWN:  seq = "\033[B"; break;
    case KEY_RIGHT: seq = "\033[C"; break;
    case KEY_LEFT:  seq = "\033[D"; break;
    case KEY_HOME:  seq = "\033[H"; break;
    case KEY_END:   seq = "\033[K"; break;
    case KEY_INS:   seq = "\033[@"; break;
    case KEY_F1:    seq = "\033OP"; break;
    case KEY_F2:    seq = "\033OQ"; break;
    case KEY_F3:    seq = "\033OR"; break;
    case KEY_F4:    seq = "\033OS"; break;
    case KEY_F5:    seq = "\033[15~"; break;
    case KEY_F6:    seq = "\033[17~"; break;
    case KEY_F7:    seq = "\033[18~"; break;
    case KEY_F8:    seq = "\033[19~"; break;
    case KEY_F9:    seq = "\033[20~"; break;
    case KEY_F0:    seq = "\033[21~"; break;
    }
    if (seq) {
        tx_str(seq);
    } else if (k == 13) {
        tx_byte(13);
        if (telnet && !bin_mode) tx_byte(0);     /* NVT: CR NUL */
        if (localecho) { term_write(13); term_write(10); }
    } else if (k < 128) {
        tx_data(k);
        if (localecho) term_write(k);
    } else {
        return;
    }
    tx_flush();
    if (localecho) flush();
}

void handle_key(unsigned char k) {
    /* global hotkeys */
    if (k == KEY_ALT_U) { utf8 = !utf8; u8need = 0; update_title(); return; }
    if (k == KEY_ALT_P) { set_truepal(!truepal); update_title(); flush(); return; }
    if (k == KEY_ALT_E) { localecho = !localecho; update_title(); return; }
    if (state == ST_ONLINE) {
        if (k == KEY_ALT_X) { disconnect(1); return; }
        online_key(k);
    } else {
        prompt_key(k);
    }
}

/* ================================================================== */
/* Network receive                                                     */

void net_drain(void) {
    TCP_Trans tt;
    unsigned short i, since = 0;
    for (;;) {
        if (sock < 0) return;
        if (TCP_Receive(sock, _symbank, rxbuf, sizeof(rxbuf), &tt)) {
            closing = 1;
            return;
        }
        for (i = 0; i < tt.transferred; ++i)
            net_byte((unsigned char)rxbuf[i]);
        since += tt.transferred;
        if (since >= 2048) { flush(); since = 0; }
        if (tt.remaining == 0) break;
    }
}

void net_event(void) {
    NetStat ns;
    TCP_Event(_symmsg, &ns);
    if (sock < 0 || ns.socket != (unsigned char)sock) return;
    if (ns.bytesrec) net_drain();
    if (ns.status == TCP_CLOSED || ns.status == TCP_CLOSING) closing = 1;
}

void net_poll(void) {
    NetStat ns;
    if (sock < 0) return;
    if (TCP_Status(sock, &ns)) { closing = 1; return; }
    if (ns.bytesrec) net_drain();
    if (ns.status == TCP_CLOSED || ns.status == TCP_CLOSING) closing = 1;
}

/* ================================================================== */
/* Main                                                                */

void quit(void) {
    palette_restore();
    if (sock >= 0) TCP_Disconnect(sock);
    if (winID >= 0) Win_Close(winID);
    release_canvases();
    exit(0);
}

int main(int argc, char* argv[]) {
    int i;
    unsigned char autoconnect = 0;

    for (i = 1; i < argc; ++i) {
        char* a = argv[i];
        if (a[0] == '-' || a[0] == '/') {
            switch (a[1] | 0x20) {
            case 'r': rawflag = 1; break;
            case 'u': utf8 = 1; break;
            case 'p': truepal = 1; break;
            case 'l': {
                int n = atoi(a + 2);
                if (n >= 10 && n <= MAXROWS) rows = n;
                break;
            }
            }
        } else if (!host[0]) {
            strncpy(host, a, sizeof(host) - 1);
            autoconnect = 1;
        } else {
            port = atoi(a);
        }
    }

    if (Screen_Colors() != 16) {
        MsgBox("SymTerm needs a 16-colour screen mode", "(MSX2: 512x212, 16 colours).", "",
               COLOR_BLACK, BUTTON_OK, 0, 0, 0);
        exit(0);
    }

    if (Net_Init()) {
        Net_ErrMsg(0);
        exit(0);
    }

    /* renderer setup */
    build_font3();
    tr_font3 = (char*)font3;
    tr_ctab = (char*)ctab;
    tr_curpage = (unsigned short)ALIGNPAGE(curbuf) >> 8;
    scratch = ALIGNPAGE(scratchraw);
    pal = truepal ? pal_true : pal_near;
    build_ctab();

    if (alloc_canvases()) {
        release_canvases();
        MsgBox("SymTerm: not enough memory", "(needs about 48KB free).", "",
               COLOR_BLACK, BUTTON_OK, 0, 0, 0);
        exit(0);
    }

    clear_dirty();
    sbot = rows - 1;
    term_reset();
    banner();
    update_title();

    /* draw the whole screen into the canvases before the window opens */
    {
        unsigned char r;
        for (r = 0; r < rows; ++r) { dlo[r] = 0; dhi[r] = COLS - 1; render_row(r); }
        clear_dirty();
        anydirty = 0;
        mv_lo = 255; mv_hi = 0;
    }

    setup_window();
    winID = Win_Open(_symbank, &form);
    if (winID < 0) quit();
    if (truepal) palette_apply();

    if (autoconnect) {
        strcpy(inputline, host);
        if (port != 23) {
            strcat(inputline, ":");
            itoa(port, inputline + strlen(inputline), 10);
        }
        do_connect();
        if (state != ST_ONLINE) { /* prompt already shown */ }
    } else {
        prompt();
    }

    for (;;) {
        unsigned short rec;

        if (closing && state == ST_ONLINE) {
            net_poll();           /* pick up any final bytes */
            flush();
            disconnect(0);
            closing = 0;
        }

        _symmsg[0] = 0;
        rec = Msg_Receive(_sympid, -1, _symmsg);
        if (!(rec & 0xFF)) {
            if (anydirty || curvis != dcvis || cx != dcx || cy != dcy) {
                flush();
                continue;
            }
            if (state == ST_ONLINE) {
                net_poll();
                if (anydirty || closing) continue;
            }
            _symmsg[0] = 0;
            Msg_Sleep(_sympid, -1, _symmsg);
            if (_symmsg[0] == 0) continue;
        }

        switch ((unsigned char)_symmsg[0]) {
        case NET_TCPEVT:
            net_event();
            break;
        case MSR_DSK_WCLICK:
            if (_symmsg[1] != winID) break;
            if (_symmsg[2] == DSK_ACT_CLOSE) quit();
            if (_symmsg[2] == DSK_ACT_KEY) handle_key((unsigned char)_symmsg[4]);
            break;
        case MSR_DSK_WFOCUS:
            if (_symmsg[1] != winID) break;
            focused = _symmsg[2];
            if (focused) palette_apply(); else palette_restore();
            break;
        }
    }
    return 0;
}
