/* Host-side stand-in for SymbOS headers, used only by tests/hostsim.c */
#ifndef HOST_SYMBOS_H
#define HOST_SYMBOS_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define _transfer
#define _data
#define ALIGNPAGE(p) ((char*)((((uintptr_t)(p)) + 255) & ~(uintptr_t)255))

typedef struct {
    unsigned short value; unsigned char type; char bank; unsigned short param;
    unsigned short x, y, w, h, unused;
} Ctrl;
typedef struct {
    unsigned char controls, pid; void* first; void* calcrule; unsigned short unused1;
    unsigned char retctrl, escctrl; char reserved1[4]; unsigned char focusctrl, reserved2;
} Ctrl_Group;
typedef struct {
    unsigned char state; unsigned short flags; unsigned char pid;
    unsigned short x, y, w, h, xscroll, yscroll, wfull, hfull, wmin, hmin, wmax, hmax;
    char* icon; char* title; char* status; void* menu; void* controls; void* toolbar;
    unsigned short toolheight; char reserved1[9]; unsigned char modal; char reserved2[140];
} Window;

#define WIN_NORMAL 1
#define WIN_MAXIMIZED 2
#define WIN_ICON 1
#define WIN_RESIZABLE 2
#define WIN_CLOSE 4
#define WIN_TITLE 16
#define C_AREA 0
#define C_IMAGE_EXT 10
#define AREA_16COLOR 128
#define COLOR_BLACK 1
#define BUTTON_OK 1
#define MSR_DSK_WCLICK 0x9A
#define MSR_DSK_WFOCUS 0x9B
#define DSK_ACT_CLOSE 5
#define DSK_ACT_KEY 16

#define KEY_UP 136
#define KEY_DOWN 137
#define KEY_LEFT 138
#define KEY_RIGHT 139
#define KEY_F0 140
#define KEY_F1 141
#define KEY_F2 142
#define KEY_F3 143
#define KEY_F4 144
#define KEY_F5 145
#define KEY_F6 146
#define KEY_F7 147
#define KEY_F8 148
#define KEY_F9 149
#define KEY_ALT_E 156
#define KEY_ALT_P 167
#define KEY_ALT_U 172
#define KEY_ALT_X 175
#define KEY_INS 208
#define KEY_HOME 254
#define KEY_END 255

extern unsigned char _symbank, _sympid;
extern char _symmsg[14];

unsigned short Msg_Receive(char rec, char snd, char* msg);
unsigned short Msg_Sleep(char rec, char snd, char* msg);
unsigned char Mem_Reserve(unsigned char bank, unsigned char type, unsigned short len,
                          unsigned char* bankVar, char** addrVar);
void Mem_Release(unsigned char bank, char* addr, unsigned short len);
void Bank_Copy(unsigned char bd, char* ad, unsigned char bs, char* as, unsigned short len);
signed char Win_Open(unsigned char bank, void* addr);
void Win_Close(unsigned char id);
void Win_Redraw(unsigned char id, signed char what, unsigned char first);
void Win_Redraw_Ext(unsigned char id, signed char what, unsigned char first);
void Win_Redraw_Area(unsigned char id, unsigned char what, unsigned char first,
                     unsigned short x, unsigned short y, unsigned short w, unsigned short h);
void Win_Redraw_Title(unsigned char id);
void Win_ContentY(unsigned char id, unsigned short y);
unsigned short Win_Height(Window* w);
unsigned char Screen_Colors(void);
unsigned short Screen_Width(void);
unsigned short Screen_Height(void);
unsigned short Color_Get(char c);
void Color_Set(char c, unsigned short v);
unsigned char MsgBox(char* a, char* b, char* c, unsigned int pen, unsigned char type,
                     char* i4, char* i16, void* modal);
char* itoa(int v, char* buf, int radix);
#endif
