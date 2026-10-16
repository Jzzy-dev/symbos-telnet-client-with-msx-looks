/* Host simulation of SymTerm.
 *
 * Compiles the real symterm.c against stubbed SymbOS calls.  Banked memory,
 * Bank_Copy, Mem_Reserve and the desktop's control redraws are emulated, so
 * the screenshot this produces is built exactly the way SymbOS would build
 * it: by reading each C_IMAGE_EXT control's extended graphics header out of
 * banked memory, and only at the moments the app asks for a redraw.
 *
 * usage: hostsim session.bin out.ppm [chunk]
 *   session.bin: bytes the "server" sends; the byte sequence 0xFF 0x00 k
 *   (IAC NUL k, never valid telnet) injects keypress k instead.
 */
#include <stdio.h>
#include <stdint.h>
#include "symbos.h"
#include "network.h"

#define main symterm_main
#include "../symterm.c"
#undef main

unsigned char _symbank = 1, _sympid = 5, _neterr = 0;
char _symmsg[14];

static uint8_t bankmem[16][65536];
static uint8_t* sess; static size_t sesslen, sesspos;
static size_t chunk = 300;
static uint8_t pend[4096]; static size_t pendlen, pendpos;
static uint8_t disp[256][480];      /* what the desktop has drawn */
static int redraws, areacalls, bankcopies;
static unsigned long bankbytes;
static const char* outpath;
static FILE* txlog;

/* ---- memory ------------------------------------------------------- */
unsigned char Mem_Reserve(unsigned char bank, unsigned char type, unsigned short len,
                          unsigned char* bv, char** av) {
    static unsigned char nb = 2; static unsigned next = 0x4000;
    (void)bank; (void)type;
    if ((next & 0x3FFF) + len > 0x4000) next = (next | 0x3FFF) + 1;
    if (next + len > 0x10000) { nb++; next = 0x4000; }
    if (nb > 15) return 1;
    *bv = nb; *av = (char*)(uintptr_t)next; next += len;
    return 0;
}
void Mem_Release(unsigned char b, char* a, unsigned short l) { (void)b; (void)a; (void)l; }
static uint8_t* bptr(unsigned char bank, char* a) {
    if (bank == _symbank) return (uint8_t*)a;
    if ((uintptr_t)a > 0xFFFF) { fprintf(stderr, "bad banked address\n"); exit(2); }
    return &bankmem[bank][(uintptr_t)a];
}
void Bank_Copy(unsigned char bd, char* ad, unsigned char bs, char* as, unsigned short len) {
    if (bd != _symbank) {
        uintptr_t a = (uintptr_t)ad;
        if ((a >> 14) != ((a + len - 1) >> 14)) { fprintf(stderr, "copy crosses 16K\n"); exit(2); }
    }
    memmove(bptr(bd, ad), bptr(bs, as), len);
    bankcopies++; bankbytes += len;
}

/* ---- desktop ------------------------------------------------------ */
static void draw_ctrl(int i, int ax, int ay, int aw, int ah) {
    Ctrl* c = &ctrls[i];
    int x, y;
    if (c->type == C_AREA) {
        for (y = c->y; y < 256 && y < c->y + c->h; ++y)
            for (x = c->x; x < 480 && x < c->x + c->w; ++x)
                if (x >= ax && x < ax + aw && y >= ay && y < ay + ah) disp[y][x] = c->param & 15;
        return;
    }
    if (c->type == C_IMAGE_EXT) {
        uint8_t* h = bptr((unsigned char)c->bank, (char*)(uintptr_t)c->param);
        int bytew = h[0], w = h[1], hh = h[2];
        unsigned data = h[3] | h[4] << 8, enc = h[5] | h[6] << 8;
        uint8_t* d = &bankmem[(unsigned char)c->bank][data];
        if (bankmem[(unsigned char)c->bank][enc] != 5) { fprintf(stderr, "bad encoding byte\n"); exit(2); }
        if (w != c->w || hh != c->h) { fprintf(stderr, "ctrl %d size mismatch\n", i); exit(2); }
        for (y = 0; y < hh; ++y)
            for (x = 0; x < w; ++x) {
                int sx = c->x + x, sy = c->y + y;
                uint8_t b = d[y * bytew + x / 2];
                if (sx >= ax && sx < ax + aw && sy >= ay && sy < ay + ah && sx < 480)
                    disp[sy][sx] = (x & 1) ? (b & 15) : (b >> 4);
            }
    }
}
static void redraw(signed char what, unsigned char first, int ax, int ay, int aw, int ah) {
    int i, n;
    redraws++;
    if (what == -1) { first = 0; n = cgroup.controls; }
    else if (what >= 0) { first = what; n = 1; }
    else n = -what;
    for (i = first; i < first + n && i < cgroup.controls; ++i) draw_ctrl(i, ax, ay, aw, ah);
}
signed char Win_Open(unsigned char b, void* a) { (void)b; (void)a; redraw(-1, 0, 0, 0, 480, 256); return 3; }
void Win_Close(unsigned char id) { (void)id; }
void Win_Redraw(unsigned char id, signed char w, unsigned char f) { (void)id; redraw(w, f, 0, 0, 480, 256); }
void Win_Redraw_Ext(unsigned char id, signed char w, unsigned char f) { (void)id; redraw(w, f, 0, 0, 480, 256); }
void Win_Redraw_Area(unsigned char id, unsigned char w, unsigned char f, unsigned short x,
                     unsigned short y, unsigned short ww, unsigned short hh) {
    (void)id; areacalls++; redraw((signed char)w, f, x, y, ww, hh);
}
void Win_Redraw_Title(unsigned char id) { (void)id; }
void Win_ContentY(unsigned char id, unsigned short y) { (void)id; form.yscroll = y; }
unsigned short Win_Height(Window* w) { (void)w; return 200; }
unsigned char Screen_Colors(void) { return 16; }
unsigned short Screen_Width(void) { return 512; }
unsigned short Screen_Height(void) { return 212; }
unsigned short Color_Get(char c) { (void)c; return 0; }
void Color_Set(char c, unsigned short v) { (void)c; (void)v; }
unsigned char MsgBox(char* a, char* b, char* c, unsigned int p, unsigned char t, char* i, char* j, void* m) {
    (void)b; (void)c; (void)p; (void)t; (void)i; (void)j; (void)m; fprintf(stderr, "MsgBox: %s\n", a); return 0;
}
char* itoa(int v, char* buf, int radix) { (void)radix; sprintf(buf, "%d", v); return buf; }

/* ---- network ------------------------------------------------------ */
signed char Net_Init(void) { return 0; }
void Net_ErrMsg(void* w) { (void)w; }
signed char DNS_Resolve(unsigned char b, char* a, char* ip) { (void)b; (void)a; ip[0]=10; ip[1]=0; ip[2]=0; ip[3]=7; return 0; }
signed char TCP_OpenClient(char* ip, signed short l, unsigned short r) { (void)ip; (void)l; (void)r; return 1; }
void TCP_Event(char* msg, NetStat* o) { (void)msg; memset(o, 0, sizeof *o); o->socket = 1; o->status = TCP_OPENED; o->bytesrec = pendlen - pendpos; }
signed char TCP_Status(unsigned char h, NetStat* o) { TCP_Event(0, o); (void)h; return 0; }
signed char TCP_Send(unsigned char h, unsigned char b, char* a, unsigned short l) {
    (void)h; (void)b; fprintf(txlog, "TX:"); for (int i = 0; i < l; ++i) fprintf(txlog, " %02X", (uint8_t)a[i]); fprintf(txlog, "\n"); return 0;
}
signed char TCP_Receive(unsigned char h, unsigned char b, char* a, unsigned short len, TCP_Trans* o) {
    size_t n = pendlen - pendpos; (void)h; (void)b;
    if (n > len) n = len;
    memcpy(a, pend + pendpos, n); pendpos += n;
    o->transferred = n; o->remaining = pendlen - pendpos;
    return 0;
}
signed char TCP_Disconnect(unsigned char h) { (void)h; return 0; }
signed char TCP_Close(unsigned char h) { (void)h; return 0; }
char* iptoa(char* ip, char* d) { sprintf(d, "%d.%d.%d.%d", (uint8_t)ip[0], (uint8_t)ip[1], (uint8_t)ip[2], (uint8_t)ip[3]); return d; }

/* ---- tr_render reference (same contract as render.s) --------------- */
void tr_render(char* dst, char* text, char* attr, unsigned char n) {
    unsigned i; int l;
    for (i = 0; i < n; ++i) {
        uint8_t ch = text[i], at = attr[i];
        for (l = 0; l < 8; ++l) {
            uint8_t v = tr_fontdata[ch * 8 + l];
            uint8_t* d = (uint8_t*)dst + l * 256 + i * 3;
            d[0] = ctab[at * 4 + ((v >> 4) & 3)];
            d[1] = ctab[at * 4 + ((v >> 2) & 3)];
            d[2] = ctab[at * 4 + (v & 3)];
        }
    }
}

/* ---- events & output ---------------------------------------------- */
static const uint32_t rgb[16] = { 0xF7F790, 0x060606, 0xF79006, 0x900606, 0x06F7F7, 0x060690, 0x9090F7,
    0x0606F7, 0xF7F7F7, 0x069006, 0x06F706, 0xF706F7, 0xF7F706, 0x909090, 0xF79090, 0xF70606 };

static void finish(void) {
    FILE* f = fopen(outpath, "wb");
    int x, y, bad = 0, badrows = 0;
    int H = rows * 8;
    /* compare what the desktop shows with an ideal render of the text buffer */
    for (y = 0; y < H; ++y) {
        int r = y / 8, l = y % 8, rowbad = 0;
        for (x = 0; x < 480; ++x) {
            int c = x / 6, px = x % 6;
            uint8_t ch = tchar[r * 80 + c], at = tattr[r * 80 + c];
            uint8_t v = tr_fontdata[ch * 8 + l];
            int on = (v >> (5 - px)) & 1;
            uint8_t col = on ? pal[at & 15] : pal[at >> 4];
            if (r == cy && c == cx && curvis && l >= 6) col = pal[at & 15];
            if (disp[y][x] != col) { bad++; rowbad = 1; }
        }
        if (rowbad && l == 7) { badrows++; fprintf(stderr, "row %d differs\n", r); }
    }
    fprintf(f, "P6\n%d %d\n255\n", 480 * 2, H * 2);
    for (y = 0; y < H * 2; ++y)
        for (x = 0; x < 960; ++x) {
            uint32_t c = rgb[disp[y / 2][x / 2]];
            fputc(c >> 16, f); fputc((c >> 8) & 255, f); fputc(c & 255, f);
        }
    fclose(f);
    printf("redraw calls: %d (area: %d), bank copies: %d (%lu bytes), mismatched pixels: %d in %d rows\n",
           redraws, areacalls, bankcopies, bankbytes, bad, badrows);
    exit(bad ? 1 : 0);
}

static int next_event(char* msg) {
    memset(msg, 0, 14);
    if (sesspos >= sesslen) {
        if (pendpos < pendlen) return 0;
        flush();
        finish();
    }
    if (sesspos + 2 < sesslen && sess[sesspos] == 0xFF && sess[sesspos + 1] == 0x00) {
        msg[0] = (char)MSR_DSK_WCLICK; msg[1] = 3; msg[2] = DSK_ACT_KEY; msg[4] = sess[sesspos + 2];
        sesspos += 3;
        return 1;
    }
    {
        size_t n = chunk, i;
        for (i = 0; i < n && sesspos + i < sesslen; ++i)
            if (sess[sesspos + i] == 0xFF && sesspos + i + 1 < sesslen && sess[sesspos + i + 1] == 0x00) break;
        n = i;
        if (pendpos == pendlen) pendpos = pendlen = 0;
        memcpy(pend + pendlen, sess + sesspos, n); pendlen += n; sesspos += n;
        msg[0] = (char)NET_TCPEVT;
        return 1;
    }
}
unsigned short Msg_Receive(char r, char s, char* m) { (void)r; (void)s; (void)m; return 0; }
unsigned short Msg_Sleep(char r, char s, char* m) { (void)r; (void)s; return next_event(m) ? 1 : 0; }

int main(int argc, char** argv) {
    FILE* f = fopen(argv[1], "rb");
    static uint8_t buf[1 << 20];
    char* av[3] = { "symterm", "bbs.example.org", 0 };
    sesslen = fread(buf, 1, sizeof buf, f); fclose(f); sess = buf;
    outpath = argv[2];
    if (argc > 3) chunk = atoi(argv[3]);
    txlog = fopen("/dev/stderr", "w");
    symterm_main(2, av);
    return 0;
}
