#ifndef HOST_NETWORK_H
#define HOST_NETWORK_H
#define TCP_OPENED 2
#define TCP_CLOSING 3
#define TCP_CLOSED 4
#define NET_TCPEVT 159
typedef struct {
    unsigned char socket, status, ip[4]; unsigned short rport;
    unsigned char datarec; unsigned short bytesrec;
} NetStat;
typedef struct { unsigned short transferred, remaining; } TCP_Trans;
extern unsigned char _neterr;
signed char Net_Init(void);
void Net_ErrMsg(void* w);
signed char DNS_Resolve(unsigned char bank, char* addr, char* ip);
signed char TCP_OpenClient(char* ip, signed short lport, unsigned short rport);
void TCP_Event(char* msg, NetStat* obj);
signed char TCP_Status(unsigned char handle, NetStat* obj);
signed char TCP_Send(unsigned char handle, unsigned char bank, char* addr, unsigned short len);
signed char TCP_Receive(unsigned char handle, unsigned char bank, char* addr,
                        unsigned short len, TCP_Trans* obj);
signed char TCP_Disconnect(unsigned char handle);
signed char TCP_Close(unsigned char handle);
char* iptoa(char* ip, char* dest);
#endif
