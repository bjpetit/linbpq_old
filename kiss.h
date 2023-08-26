

#define KISSMAXBLOCK        512

// KISS over TCP Slave now allows multiple connections
// so need a struct to keep track of them

typedef struct _KISSTCPSess
{
	struct _KISSTCPSesssion * Next;
	SOCKET Socket;
	UCHAR RXBuffer[KISSMAXBLOCK];
	int RXLen;

	time_t Timeout;

} KISSTCPSess;

typedef struct tagASYINFO
{
	HANDLE  idComDev ;
	BYTE    bPort;
	DWORD   dwBaudRate ;
	SOCKET	sock;			// for KISS over UDP/TCP
	BOOL	Connecting;		// Kiss over TCP
	BOOL	Connected;		// Kiss over TCP
	BOOL	Listening;		// Kiss over TCP
	BOOL	Alerted;		// Connect Fail Reported

	struct sockaddr_in destaddr;
	struct PORTCONTROL * Portvector;
	UCHAR	RXMSG[512];				// Msg being built
	UCHAR	RXBUFFER[KISSMAXBLOCK];	// Raw chars from Comms
	int		RXBCOUNT;				// chars in RXBUFFER
	UCHAR * RXBPTR;					// get pointer for RXBUFFER (put ptr is RXBCOUNT) 
	UCHAR * RXMPTR;					// put pointer for RXMSG
	BOOL	MSGREADY;				// Complete msg in RXMSG
	BOOL	ESCFLAG;				// FESC/DLE received
	BOOL	NEEDCRC;				// ETX received, waiting for CRC (NETROM)
	int		ReopenTimer;			// for failed com ports

	// We now allow multiple connections to KISS Slave

	struct _KISSTCPSess * slaveSessions;

} ASYINFO, *NPASYINFO ;

extern NPASYINFO KISSInfo[MAXBPQPORTS];

#define _fmemset   memset
#define _fmemmove  memmove

// function prototypes (private)

NPASYINFO CreateKISSINFO( int port, int speed );


BOOL DestroyKISSINFO(NPASYINFO npKISSINFO) ;
int ReadCommBlock(NPASYINFO npKISSINFO, char * lpszBlock, int nMaxLength);
static BOOL WriteCommBlock(NPASYINFO npKISSINFO, char * lpByte, DWORD dwBytesToWrite);
HANDLE OpenConnection(struct PORTCONTROL * PortVector);
BOOL SetupConnection(NPASYINFO npKISSINFO);
BOOL CloseConnection(NPASYINFO npKISSINFO);
