/*
Copyright 2001-2022 John Wiseman G8BPQ

This file is part of LinBPQ/BPQ32.

LinBPQ/BPQ32 is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

LinBPQ/BPQ32 is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with LinBPQ/BPQ32.  If not, see http://www.gnu.org/licenses
*/	

//
//	DLL to inteface DED Host Mode TNCs to BPQ32 switch 
//
//	Uses BPQ EXTERNAL interface

#define _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_DEPRECATE

#include <stdio.h>
#include <stdlib.h>
#include "time.h"

#define MaxStreams 1	

#include "CHeaders.h"


extern int (WINAPI FAR *GetModuleFileNameExPtr)();
extern int (WINAPI FAR *EnumProcessesPtr)();

#include "tncinfo.h"


#define SD_RECEIVE      0x00
#define SD_SEND         0x01
#define SD_BOTH         0x02


//#include "bpq32.h"

static char ClassName[]="TRACKERSTATUS";
static char WindowTitle[] = "WinRPR";
static int RigControlRow = 140;

#define NARROWMODE 30
#define WIDEMODE 30			// Robust

extern UCHAR LogDirectory[];

extern APPLCALLS APPLCALLTABLE[];

extern char LOC[];

static RECT Rect;

VOID __cdecl Debugprintf(const char * format, ...);
char * strlop(char * buf, char delim);

char NodeCall[11];		// Nodecall, Null Terminated

struct TNCINFO * CreateTTYInfo(int port, int speed);
BOOL OpenConnection(int);
BOOL SetupConnection(int);
BOOL CloseConnection(struct TNCINFO * conn);
static BOOL WriteCommBlock(struct TNCINFO * TNC);
BOOL DestroyTTYInfo(int port);
static void DEDCheckRX(struct TNCINFO * TNC);
VOID DEDPoll(int Port);
VOID StuffAndSend(struct TNCINFO * TNC, UCHAR * Msg, int Len);
unsigned short int compute_crc(unsigned char *buf,int len);
int Unstuff(UCHAR * MsgIn, UCHAR * MsgOut, int len);
VOID TrkProcessDEDFrame(struct TNCINFO * TNC);
VOID TrkProcessTermModeResponse(struct TNCINFO * TNC);
static VOID ExitHost(struct TNCINFO * TNC);
VOID DoTNCReinit(struct TNCINFO * TNC);
VOID DoTermModeTimeout(struct TNCINFO * TNC);
VOID DoMonitorHddr(struct TNCINFO * TNC, UCHAR * Msg, int Len, int Type);
VOID DoMonitorData(struct TNCINFO * TNC, UCHAR * Msg, int Len);
int Switchmode(struct TNCINFO * TNC, int Mode);
VOID SwitchToRPacket(struct TNCINFO * TNC, char * Baud);
VOID SwitchToNormPacket(struct TNCINFO * TNC, char * Baud);
VOID SendRPBeacon(struct TNCINFO * TNC);
BOOL APIENTRY Send_AX(PMESSAGE Block, DWORD Len, UCHAR Port);
int DoScanLine(struct TNCINFO * TNC, char * Buff, int Len);
VOID SuspendOtherPorts(struct TNCINFO * ThisTNC);
VOID ReleaseOtherPorts(struct TNCINFO * ThisTNC);
VOID WritetoTrace(struct TNCINFO * TNC, char * Msg, int Len);
BOOL KAMStartPort(struct PORTCONTROL * PORT);
BOOL KAMStopPort(struct PORTCONTROL * PORT);


extern VOID TRKSuspendPort(struct TNCINFO * TNC, struct TNCINFO * ThisTNC);
extern VOID TRKReleasePort(struct TNCINFO * TNC);
extern VOID CloseDebugLogFile(int Port);
extern BOOL OpenDebugLogFile(int Port);
extern void WriteDebugLogLine(int Port, char Dirn, char * Msg, int MsgLen);
extern VOID TrkTidyClose(struct TNCINFO * TNC, int Stream);
extern VOID TrkForcedClose(struct TNCINFO * TNC, int Stream);
extern VOID TrkCloseComplete(struct TNCINFO * TNC, int Stream);
VOID TrkExitHost(struct TNCINFO * TNC);
int ConnecttoWinRPR(int port);

BOOL KillOldTNC(char * Path);
int KillTNC(struct TNCINFO * TNC);

static BOOL RestartTNC(struct TNCINFO * TNC)
{
	if (TNC->ProgramPath == NULL)
		return 0;

	if (_memicmp(TNC->ProgramPath, "REMOTE:", 7) == 0)
	{
		int n;
		
		// Try to start TNC on a remote host

		SOCKET sock = socket(AF_INET,SOCK_DGRAM,0);
		struct sockaddr_in destaddr;

		Debugprintf("trying to restart TNC %s", TNC->ProgramPath);

		if (sock == INVALID_SOCKET)
			return 0;

		destaddr.sin_family = AF_INET;
		destaddr.sin_addr.s_addr = inet_addr(TNC->HostName);
		destaddr.sin_port = htons(8500);

		if (destaddr.sin_addr.s_addr == INADDR_NONE)
		{
			//	Resolve name to address

			struct hostent * HostEnt = gethostbyname (TNC->HostName);
		 
			if (!HostEnt)
				return 0;			// Resolve failed

			memcpy(&destaddr.sin_addr.s_addr,HostEnt->h_addr,4);
		}

		n = sendto(sock, TNC->ProgramPath, (int)strlen(TNC->ProgramPath), 0, (struct sockaddr *)&destaddr, sizeof(destaddr));
	
		Debugprintf("Restart TNC - sendto returned %d", n);

		Sleep(100);
		closesocket(sock);

		return 1;				// Cant tell if it worked, but assume ok
	}

	// Not Remote

	// Extract any parameters from command string

#ifndef WIN32
	{
		char * arg_list[] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
		pid_t child_pid;
		char * Copy, * Context;
		signal(SIGCHLD, SIG_IGN); // Silently (and portably) reap children. 

		Copy = _strdup(TNC->ProgramPath);	// Save as strtok mangles it

		arg_list[0] = strtok_s(Copy, " \n\r", &Context);
		if (arg_list[0])
			arg_list[1] = strtok_s(NULL, " \n\r", &Context);
		if (arg_list[1])
			arg_list[2] = strtok_s(NULL, " \n\r", &Context);
		if (arg_list[2])
			arg_list[3] = strtok_s(NULL, " \n\r", &Context);
		if (arg_list[3])
			arg_list[4] = strtok_s(NULL, " \n\r", &Context);
		if (arg_list[4])
			arg_list[5] = strtok_s(NULL, " \n\r", &Context);
		if (arg_list[5])
			arg_list[6] = strtok_s(NULL, " \n\r", &Context);
		if (arg_list[6])
			arg_list[7] = strtok_s(NULL, " \n\r", &Context);

		//	Fork and Exec TNC

		printf("Trying to start %s\n", TNC->ProgramPath);

		/* Duplicate this process. */ 

		child_pid = fork (); 

		if (child_pid == -1) 
 		{    				
			printf ("StartTNC fork() Failed\n"); 
			free(Copy);
			return 0;
		}

		if (child_pid == 0) 
 		{    				
			execvp (arg_list[0], arg_list); 
        
			/* The execvp  function returns only if an error occurs.  */ 

			printf ("Failed to start TNC\n"); 
			exit(0);			// Kill the new process
		}
		printf("Started TNC\n");
		free(Copy);
		return TRUE;
	}								 
#else

	{
		int n = 0;
		
		STARTUPINFO  SInfo;			// pointer to STARTUPINFO 
	    PROCESS_INFORMATION PInfo; 	// pointer to PROCESS_INFORMATION 
		char workingDirectory[256];
		int i = strlen(TNC->ProgramPath);

		SInfo.cb=sizeof(SInfo);
		SInfo.lpReserved=NULL; 
		SInfo.lpDesktop=NULL; 
		SInfo.lpTitle=NULL; 
		SInfo.dwFlags=0; 
		SInfo.cbReserved2=0; 
	  	SInfo.lpReserved2=NULL; 

		Debugprintf("RestartTNC Called for %s", TNC->ProgramPath);

		strcpy(workingDirectory, TNC->ProgramPath);

		while (i--)
		{
			if (workingDirectory[i] == '\\' || workingDirectory[i] == '/')
			{
				workingDirectory[i] = 0; 
				break;
			}
		}

		while (KillOldTNC(TNC->ProgramPath) && n++ < 100)
		{
			Sleep(100);
		}

		if (CreateProcess(NULL, TNC->ProgramPath, NULL, NULL, FALSE,0, NULL, workingDirectory, &SInfo, &PInfo))
		{
			Debugprintf("Restart TNC OK");
			TNC->PID = PInfo.dwProcessId;
			return TRUE;
		}
		else
		{
			Debugprintf("Restart TNC Failed %d ", GetLastError());
			return FALSE;
		}
	}
#endif
	return 0;
}

static int ProcessLine(char * buf, int Port)
{
	UCHAR * ptr,* p_cmd;
	char * p_port = 0;
	int BPQport;
	int len=510;
	struct TNCINFO * TNC;
	char errbuf[256];

	char * p_ipad = 0;
	unsigned short WINMORport = 0;

	strcpy(errbuf, buf);

	ptr = strtok(buf, " \t\n\r");

	if (ptr == NULL) return (TRUE);

	if (*ptr =='#') return (TRUE);			// comment

	if (*ptr ==';') return (TRUE);			// comment

	if (_stricmp(buf, "ADDR"))
		return FALSE;						// Must start with ADDR

	ptr = strtok(NULL, " \t\n\r");

	BPQport = Port;
	p_ipad = ptr;

	TNC = TNCInfo[BPQport] = malloc(sizeof(struct TNCINFO));
	memset(TNC, 0, sizeof(struct TNCINFO));

	strcpy(TNC->NormSpeed, "300");		// HF Packet
	strcpy(TNC->RobustSpeed, "R600");
	TNC->DefaultMode = TNC->WL2KMode = 0;	// Packet 1200

	TNC->InitScript = malloc(1000);
	TNC->InitScript[0] = 0;
	
	if (p_ipad == NULL)
		p_ipad = strtok(NULL, " \t\n\r");

	if (p_ipad == NULL) return (FALSE);
	
	p_port = strtok(NULL, " \t\n\r");
			
	if (p_port == NULL) return (FALSE);

	WINMORport = atoi(p_port);

	TNC->destaddr.sin_family = AF_INET;
	TNC->destaddr.sin_port = htons(WINMORport);
	TNC->Datadestaddr.sin_family = AF_INET;
	TNC->Datadestaddr.sin_port = htons(WINMORport+1);

	TNC->HostName = malloc(strlen(p_ipad)+1);

	if (TNC->HostName == NULL) return TRUE;

	strcpy(TNC->HostName,p_ipad);

	ptr = strtok(NULL, " \t\n\r");

	if (ptr)
	{
		if (_stricmp(ptr, "PTT") == 0)
		{
			ptr = strtok(NULL, " \t\n\r");

			if (ptr)
			{			
				DecodePTTString(TNC, ptr);
				ptr = strtok(NULL, " \t\n\r");
			}
		}
	}
		
	if (ptr)
	{
		if (_memicmp(ptr, "PATH", 4) == 0)
		{
			p_cmd = strtok(NULL, "\n\r");
			if (p_cmd) TNC->ProgramPath = _strdup(p_cmd);
		}
	}



	while(TRUE)
	{
		if (GetLine(buf) == 0)
			return TRUE;

		strcpy(errbuf, buf);

		if (memcmp(buf, "****", 4) == 0)
			return TRUE;

		ptr = strchr(buf, ';');

		if (ptr)
		{
			*ptr++ = 13;
			*ptr = 0;
		}

		if (_memicmp(buf, "APPL", 4) == 0)
		{
			p_cmd = strtok(&buf[4], " \t\n\r");

			if (p_cmd && p_cmd[0] != ';' && p_cmd[0] != '#')
				TNC->ApplCmd=_strdup(_strupr(p_cmd));
		}
		else
		
//		if (_mem`icmp(buf, "PACKETCHANNELS", 14) == 0)


//			// Packet Channels

//			TNC->PacketChannels = atoi(&buf[14]);
//		else

		if (_memicmp(buf, "SWITCHMODES", 11) == 0)
		{
			// Switch between Normal and Robust Packet 

			double Robust = atof(&buf[12]);
			#pragma warning(push)
			#pragma warning(disable : 4244)
			TNC->RobustTime = Robust * 10;
			#pragma warning(pop)
		}
		if (_memicmp(buf, "DEBUGLOG", 8) == 0)	// Write Debug Log
			TNC->WRITELOG = atoi(&buf[9]);
		else if (_memicmp(buf, "BEACONAFTERSESSION", 18) == 0) // Send Beacon after each session 
			TNC->RPBEACON = TRUE;
		else
		if (_memicmp(buf, "USEAPPLCALLS", 12) == 0)
			TNC->UseAPPLCalls = TRUE;
		else
		if (_memicmp(buf, "DEFAULT ROBUST", 14) == 0)
			TNC->RobustDefault = TRUE;
		else
		if (_memicmp(buf, "FORCE ROBUST", 12) == 0)
			TNC->ForceRobust = TNC->RobustDefault = TRUE;
		else
		if (_memicmp(buf, "UPDATEMAP", 9) == 0)
			TNC->PktUpdateMap = TRUE;
		else
		if (_memicmp(buf, "WL2KREPORT", 10) == 0)
			TNC->WL2K = DecodeWL2KReportLine(buf);
		else if (standardParams(TNC, buf) == FALSE)
		{
			strcat (TNC->InitScript, buf);

			// If %B param,and not R300 or R600 extract speed

			if (_memicmp(buf, "%B", 2) == 0)
			{
				ptr = strchr(buf, '\r');
				if (ptr) *ptr = 0;
				if (strchr(buf, 'R') == 0)
					strcpy(TNC->NormSpeed, &buf[3]);
				else
					strcpy(TNC->RobustSpeed, &buf[3]);

			}
		}
	}
	return (TRUE);

}

static size_t ExtProc(int fn, int port, PDATAMESSAGE buff)
{
	PMSGWITHLEN buffptr;
	int txlen = 0;
	struct TNCINFO * TNC = TNCInfo[port];
	int Param;
	int Stream = 0;
	struct STREAMINFO * STREAM;
	int TNCOK;
	struct ScanEntry * Scan;
	int NewMode;

	if (TNC == NULL)
		return 0;
		
	if (TNC->CONNECTED == 0 && TNC->CONNECTING == 0)
	{
		// Clear anything from UI_Q

		while (TNC->PortRecord->UI_Q)
		{
			buffptr = Q_REM(&TNC->PortRecord->UI_Q);
			ReleaseBuffer(buffptr);
		}

		if (fn > 3  && fn < 6)
			goto ok;

		// Try to reopen every 15 secs

		if (fn > 3  && fn < 7)
			goto ok;

		TNC->ReopenTimer++;

		if (TNC->ReopenTimer < 150)
			return 0;

		TNC->ReopenTimer = 0;

		ConnecttoWinRPR(TNC->Port);
		return 0;
	}
ok:

	if (TNC->CONNECTED == 0)
		return 0;

	switch (fn)
	{
	case 7:			

		// 100 mS Timer. 

		// G7TAJ's code to record activity for stats display
			
		if ( TNC->BusyFlags && CDBusy )
			TNC->PortRecord->PORTCONTROL.ACTIVE += 2;

		if ( TNC->PTTState )
			TNC->PortRecord->PORTCONTROL.SENDING += 2;

		//	See if waiting for connect after changing MYCALL

		if (TNC->SlowTimer)
		{
			TNC->SlowTimer--;
			if (TNC->SlowTimer == 0)
			{
				// Not connected in 45 secs, set back to Port Call

				Debugprintf("RP No response after changing call - setting MYCALL back to %s", TNC->NodeCall);
				TRKReleasePort(TNC);
			}
		}				

		return 0;

	case 1:				// poll

		for (Stream = 0; Stream <= MaxStreams; Stream++)
		{
			if (TNC->Streams[Stream].ReportDISC)
			{
				TNC->Streams[Stream].ReportDISC = FALSE;
				buff->PORT = Stream;
				return -1;
			}
		}

		DEDCheckRX(TNC);
		DEDPoll(port);
		DEDCheckRX(TNC);

		for (Stream = 0; Stream <= MaxStreams; Stream++)
		{
			if (TNC->Streams[Stream].PACTORtoBPQ_Q !=0)
			{
				size_t datalen;

				buffptr = Q_REM(&TNC->Streams[Stream].PACTORtoBPQ_Q);

				datalen = buffptr->Len;

				buff->PORT = Stream;						// Compatibility with Kam Driver
				buff->PID = 0xf0;
				memcpy(&buff->L2DATA, &buffptr->Data[0], datalen);		// Data goes to + 7, but we have an extra byte
				datalen += sizeof(void *) + 4;

				PutLengthinBuffer(buff, (int)datalen);
		
				ReleaseBuffer(buffptr);
	
				return (1);
			}
		}
	
			
		return 0;

	case 2:				// send

		buffptr = GetBuff();

		if (buffptr == 0) return (0);			// No buffers, so ignore

		Stream = buff->PORT;

		if (!TNC->TNCOK)
		{
			// Send Error Response

			buffptr->Len = 22;
			memcpy(buffptr->Data, "No Connection to TNC\r", 22);

			C_Q_ADD(&TNC->Streams[Stream].PACTORtoBPQ_Q, buffptr);
			
			return 0;
		}

		txlen = GetLengthfromBuffer(buff) - (sizeof(void *) + 4);

		buffptr->Len = txlen;
		memcpy(&buffptr->Data[0], &buff->L2DATA[0], txlen);

		C_Q_ADD(&TNC->Streams[Stream].BPQtoPACTOR_Q, buffptr);

		TNC->Streams[Stream].FramesOutstanding++;
		
		return (0);


	case 3:				// CHECK IF OK TO SEND. Also used to check if TNC is responding

		Stream = (int)(size_t)buff;

		TNCOK = (TNC->HostMode == 1 && TNC->ReinitState != 10);

		STREAM = &TNC->Streams[Stream];

		if (Stream == 0)
		{
			if (STREAM->FramesOutstanding  > 4)
				return (1 | TNCOK << 8 | STREAM->Disconnecting << 15);
		}
		else
		{
			if (STREAM->FramesOutstanding > 3 || TNC->Buffers < 200)	
				return (1 | TNCOK << 8 | STREAM->Disconnecting << 15);		}

		return TNCOK << 8 | STREAM->Disconnecting << 15;		// OK, but lock attach if disconnecting


	case 4:				// reinit

		TrkExitHost(TNC);
		Sleep(50);
		TNC->ReopenTimer = 250;
		TNC->HostMode = FALSE;

		shutdown(TNC->TCPSock, SD_BOTH);
		Sleep(100);
		closesocket(TNC->TCPSock);

		if (TNC->WeStartedTNC)
		{
			KillTNC(TNC);
			RestartTNC(TNC);
		}
		return 0;

	case 5:				// Close

		// Ensure in Pactor

		TrkExitHost(TNC);

		Sleep(25);

		shutdown(TNC->TCPSock, SD_BOTH);
		Sleep(100);
		closesocket(TNC->TCPSock);
		if (TNC->WeStartedTNC)
			KillTNC(TNC);
	
	

		return (0);

	case 6:				// Scan Interface

		Param = (int)(size_t)buff;

		switch (Param)
		{
		case 1:		// Request Permission

			if (TNC->TNCOK)
			{
				TNC->WantToChangeFreq = TRUE;
				TNC->OKToChangeFreq = TRUE;
				return 0;
			}
			return 0;		// Don't lock scan if TNC isn't reponding
		

		case 2:		// Check  Permission
			return TNC->OKToChangeFreq;

		case 3:		// Release  Permission
		
			TNC->WantToChangeFreq = FALSE;
			TNC->DontWantToChangeFreq = TRUE;
			return 0;
		

		default: // Change Mode. Param is Address of a struct ScanEntry

			Scan = (struct ScanEntry *)buff;

			// If no change, just return

			NewMode = Scan->RPacketMode | (Scan->HFPacketMode << 8);

			if (TNC->CurrentMode == NewMode)
				return 0;

			TNC->CurrentMode = NewMode;

			if (Scan->RPacketMode == '1')
			{
				SwitchToRPacket(TNC, "R300");
				return 0;
			}
			if (Scan->RPacketMode == '2')
			{
				SwitchToRPacket(TNC, "R600");
				return 0;
			}

			if (Scan->HFPacketMode == '1')
			{
				SwitchToNormPacket(TNC, "300");
				return 0;
			}

			if (Scan->HFPacketMode == '2')
			{
				SwitchToNormPacket(TNC, "1200");
				return 0;
			}
			if (Scan->HFPacketMode == '3')
			{
				SwitchToNormPacket(TNC, "9600");
				return 0;
			}
		}
	}
	return 0;
}

int WebProc(struct TNCINFO * TNC, char * Buff, BOOL LOCAL)
{
	int Interval = 15;
	int Len;

	if (LOCAL)
	{
		if (TNC->WEB_CHANGED)
			Interval = 1;
		else
			Interval = 4;
	}
	else
	{
		if (TNC->WEB_CHANGED)
			Interval = 4;
		else
			Interval = 15;
	}

	if (TNC->WEB_CHANGED)
	{
		TNC->WEB_CHANGED -= Interval;
		if (TNC->WEB_CHANGED < 0)
			TNC->WEB_CHANGED = 0;
	}

	Len = sprintf(Buff, "<html><meta http-equiv=expires content=0><meta http-equiv=refresh content=%d>"
	"<head><title>WinRPR Status</title></head><body><h2>WinRPR Status</h2>", Interval);

	Len += sprintf(&Buff[Len], "<table style=\"text-align: left; width: 480px; font-family: monospace; align=center \" border=1 cellpadding=2 cellspacing=2>");

	Len += sprintf(&Buff[Len], "<tr><td width=90px>Comms State</td><td>%s</td></tr>", TNC->WEB_COMMSSTATE);
	Len += sprintf(&Buff[Len], "<tr><td>TNC State</td><td>%s</td></tr>", TNC->WEB_TNCSTATE);
	Len += sprintf(&Buff[Len], "<tr><td>Mode</td><td>%s</td></tr>", TNC->WEB_MODE);
	Len += sprintf(&Buff[Len], "<tr><td>Buffers</td><td>%s</td></tr>", TNC->WEB_BUFFERS);
	Len += sprintf(&Buff[Len], "<tr><td>Traffic</td><td>%s</td></tr>", TNC->WEB_TRAFFIC);
	Len += sprintf(&Buff[Len], "</table>");

	Len += sprintf(&Buff[Len], "<textarea rows=10 style=\"width:480px; height:250px;\" id=textarea >%s</textarea>", TNC->WebBuffer);
	Len = DoScanLine(TNC, Buff, Len);

	return Len;
}



void * WinRPRExtInit(EXTPORTDATA *  PortEntry)
{
	char msg[500];
	struct TNCINFO * TNC;
	int port;
	char * ptr;
	int Stream = 0;
	char * TempScript;

	port=PortEntry->PORTCONTROL.PORTNUMBER;

	ReadConfigFile(port, ProcessLine);

	TNC = TNCInfo[port];

	if (TNC == NULL)
	{
		// Not defined in Config file

		sprintf(msg," ** Error - no info in BPQ32.cfg for this port\n");
		WritetoConsoleLocal(msg);

		return ExtProc;
	}

	sprintf(msg,"WinRPR Host %s %d", TNC->HostName, htons(TNC->destaddr.sin_port));
	WritetoConsoleLocal(msg);

	TNC->Port = port;
	TNC->Hardware = H_WINRPR;

	if (TNC->ProgramPath)
		TNC->WeStartedTNC = RestartTNC(TNC);


	// Set up DED addresses for streams
	
	for (Stream = 0; Stream <= MaxStreams; Stream++)
	{
		TNC->Streams[Stream].DEDStream = Stream + 1;	// DED Stream = BPQ Stream + 1
	}

	if (TNC->PacketChannels > MaxStreams)
		TNC->PacketChannels = MaxStreams;

	PortEntry->MAXHOSTMODESESSIONS = 1;				//TNC->PacketChannels + 1;
	PortEntry->PERMITGATEWAY = TRUE;				// Can change ax.25 call on each stream
	PortEntry->SCANCAPABILITIES = NONE;				// Scan Control 3 stage/conlock 

	TNC->PortRecord = PortEntry;

	if (PortEntry->PORTCONTROL.PORTCALL[0] == 0)
		memcpy(TNC->NodeCall, MYNODECALL, 10);
	else
		ConvFromAX25(&PortEntry->PORTCONTROL.PORTCALL[0], TNC->NodeCall);
		
	PortEntry->PORTCONTROL.PROTOCOL = 10;
	PortEntry->PORTCONTROL.UICAPABLE = 1;
	PortEntry->PORTCONTROL.PORTQUALITY = 0;

	if (PortEntry->PORTCONTROL.PORTPACLEN == 0)
		PortEntry->PORTCONTROL.PORTPACLEN = 100;

	if (PortEntry->PORTCONTROL.PORTINTERLOCK && TNC->RXRadio == 0 && TNC->TXRadio == 0)
		TNC->RXRadio = TNC->TXRadio = PortEntry->PORTCONTROL.PORTINTERLOCK;

	TNC->SuspendPortProc = TRKSuspendPort;
	TNC->ReleasePortProc = TRKReleasePort;

//	PortEntry->PORTCONTROL.PORTSTARTCODE = KAMStartPort;
//	PortEntry->PORTCONTROL.PORTSTOPCODE = KAMStopPort;

	TNC->PortRecord->PORTCONTROL.SerialPortName =  _strdup(TNC->HostName);

	ptr=strchr(TNC->NodeCall, ' ');
	if (ptr) *(ptr) = 0;					// Null Terminate

	// get NODECALL for RP tests

	memcpy(NodeCall, MYNODECALL, 10);
		
	ptr=strchr(NodeCall, ' ');
	if (ptr) *(ptr) = 0;					// Null Terminate

	TempScript = malloc(1000);

	strcpy(TempScript, "M UISC\r");
	strcat(TempScript, "F 200\r");			// Sets SABM retry time to about 5 secs
	strcat(TempScript, "%F 1500\r");		// Tones may be changed but I want this as standard

	strcat(TempScript, TNC->InitScript);

	free(TNC->InitScript);
	TNC->InitScript = TempScript;

	// Others go on end so they can't be overriden

	strcat(TNC->InitScript, "Z 0\r");      //  	No Flow Control
	strcat(TNC->InitScript, "Y 1\r");      //  	One Channel
	strcat(TNC->InitScript, "E 1\r");      //  	Echo - Restart process needs echo
	
	sprintf(msg, "I %s\r", TNC->NodeCall);
	strcat(TNC->InitScript, msg);

	strcpy(TNC->Streams[0].MyCall, TNC->NodeCall); // For 1st Connected Test 

	PortEntry->PORTCONTROL.TNC = TNC;

	if (TNC->WL2K == NULL)
		if (PortEntry->PORTCONTROL.WL2KInfo.RMSCall[0])			// Alrerady decoded as part of main config section
			TNC->WL2K = &PortEntry->PORTCONTROL.WL2KInfo;


	TNC->WebWindowProc = WebProc;
	TNC->WebWinX = 520;
	TNC->WebWinY = 500;
	TNC->WebBuffer = zalloc(5000);

	TNC->WEB_COMMSSTATE = zalloc(100);
	TNC->WEB_TNCSTATE = zalloc(100);
	TNC->WEB_MODE = zalloc(20);
	TNC->WEB_BUFFERS = zalloc(100);
	TNC->WEB_TRAFFIC = zalloc(100);

#ifndef LINBPQ

	CreatePactorWindow(TNC, ClassName, WindowTitle, RigControlRow, PacWndProc, 500, 450, TrkForcedClose);

	CreateWindowEx(0, "STATIC", "Comms State", WS_CHILD | WS_VISIBLE, 10,10,120,24, TNC->hDlg, NULL, hInstance, NULL);
	TNC->xIDC_COMMSSTATE = CreateWindowEx(0, "STATIC", "", WS_CHILD | WS_VISIBLE, 116,10,386,26, TNC->hDlg, NULL, hInstance, NULL);
	
	CreateWindowEx(0, "STATIC", "TNC State", WS_CHILD | WS_VISIBLE, 10,36,106,24, TNC->hDlg, NULL, hInstance, NULL);
	TNC->xIDC_TNCSTATE = CreateWindowEx(0, "STATIC", "", WS_CHILD | WS_VISIBLE, 116,36,520,24, TNC->hDlg, NULL, hInstance, NULL);

	CreateWindowEx(0, "STATIC", "Mode", WS_CHILD | WS_VISIBLE, 10,62,80,24, TNC->hDlg, NULL, hInstance, NULL);
	TNC->xIDC_MODE = CreateWindowEx(0, "STATIC", "", WS_CHILD | WS_VISIBLE, 116,62,200,24, TNC->hDlg, NULL, hInstance, NULL);

	CreateWindowEx(0, "STATIC", "Buffers", WS_CHILD | WS_VISIBLE, 10,88,80,24, TNC->hDlg, NULL, hInstance, NULL);
	TNC->xIDC_BUFFERS = CreateWindowEx(0, "STATIC", "0", WS_CHILD | WS_VISIBLE, 116,88,144,24, TNC->hDlg, NULL, hInstance, NULL);

	CreateWindowEx(0, "STATIC", "Traffic", WS_CHILD | WS_VISIBLE,10,114,80,24, TNC->hDlg, NULL, hInstance, NULL);
	TNC->xIDC_TRAFFIC = CreateWindowEx(0, "STATIC", "RX 0 TX 0", WS_CHILD | WS_VISIBLE,116,114,374,24 , TNC->hDlg, NULL, hInstance, NULL);

	TNC->hMonitor= CreateWindowEx(0, "LISTBOX", "", WS_CHILD |  WS_VISIBLE  | LBS_NOINTEGRALHEIGHT | 
            LBS_DISABLENOSCROLL | WS_HSCROLL | WS_VSCROLL,
			0,170,250,300, TNC->hDlg, NULL, hInstance, NULL);

	TNC->ClientHeight = 450;
	TNC->ClientWidth = 500;


	MoveWindows(TNC);
	
#endif

	if (TNC->RobustDefault)
	{
		TNC->Robust = TRUE;
		strcat(TNC->InitScript, "%B R300\r");
		SetWindowText(TNC->xIDC_MODE, "Robust Packet");
		strcpy(TNC->WEB_MODE, "Robust Packet");
		TNC->WEB_CHANGED = TRUE;
	}
	else
	{
		char Cmd[40];
		sprintf(Cmd, "%%B %s\r", TNC->NormSpeed);
		strcat(TNC->InitScript, Cmd);
		SetWindowText(TNC->xIDC_MODE, "HF Packet");
		strcpy(TNC->WEB_MODE, "HF Packet");
		TNC->WEB_CHANGED = TRUE;

	}

	strcpy(TNC->WEB_TNCSTATE, "Idle");
	SetWindowText(TNC->xIDC_TNCSTATE, TNC->WEB_TNCSTATE);

	TNC->InitPtr = TNC->InitScript;

	if (TNC->RIG == &TNC->DummyRig || TNC->RIG == NULL)
		TNC->SwitchToPactor = TNC->RobustTime;		// Don't alternate Modes if using Rig Control

	WritetoConsoleLocal("\n");

	ConnecttoWinRPR(TNC->Port);

	return ExtProc;
}

static void DEDCheckRX(struct TNCINFO * TNC)
{
	int Length, Len;
	UCHAR  * ptr;
	UCHAR character;
	UCHAR * CURSOR;

	Len = ReadCOMBlock(TNC->hDevice, &TNC->RXBuffer[TNC->RXLen], 500 - TNC->RXLen);

	if (Len == 0)
		return;					// Nothing doing
	
	TNC->RXLen += Len;

	Length = TNC->RXLen;
	
	ptr = TNC->RXBuffer;

	CURSOR = &TNC->DEDBuffer[TNC->InputLen];

	if ((TNC->HostMode == 0 || TNC->ReinitState == 10) && Length > 80)
	{
		// Probably Signon Message

		if (TNC->WRITELOG)
			WriteDebugLogLine(TNC->Port, 'R', ptr, Length);

		ptr[Length] = 0;
		Debugprintf("TRK %s", ptr);
		TNC->RXLen = 0;
		return;
	}

	if (TNC->HostMode == 0)
	{
		// If we are just restarting, and TNC is in host mode, we may get "Invalid Channel" Back
		
		if (memcmp(ptr, "\x18\x02INVALID", 9) == 0)
		{
			if (TNC->WRITELOG)
				WriteDebugLogLine(TNC->Port, 'R', ptr, Length);

			TNC->HostMode = TRUE;
			TNC->HOSTSTATE = 0;
			TNC->Timeout = 0;
			TNC->RXLen = 0;
			return;
		}

		// Command is echoed as * command * 

		if (strstr(ptr, "*") || TNC->ReinitState == 5)		// 5 is waiting for reponse to JHOST1
		{
			TrkProcessTermModeResponse(TNC);
			TNC->RXLen = 0;
			TNC->HOSTSTATE = 0;

			return;
		}
	}

	if (TNC->ReinitState == 10)
	{
		if (Length == 1 && *(ptr) == '.')		// 01 echoed as .
		{
			// TNC is in Term Mode

			if (TNC->WRITELOG)
				WriteDebugLogLine(TNC->Port, 'R', ptr, Length);
			TNC->ReinitState = 0;
			TNC->HostMode = 0;

			return;
		}
	}


	while (Length--)
	{
		character = *(ptr++);

		if (TNC->HostMode)
		{
			// n       0        Success (nothing follows)
			// n       1        Success (message follows, null terminated)
			// n       2        Failure (message follows, null terminated)
			// n       3        Link Status (null terminated)
			// n       4        Monitor Header (null terminated)
			// n       5        Monitor Header (null terminated)
			// n       6        Monitor Information (preceeded by length-1)
			// n       7        Connect Information (preceeded by length-1)


			switch(TNC->HOSTSTATE)
			{
			case 0: 	//  SETCHANNEL

				TNC->MSGCHANNEL = character;
				TNC->HOSTSTATE++;
				break;

			case 1:		//	SETMSGTYPE

				TNC->MSGTYPE = character;

				if (character == 0)
				{
					// Success, no more info

					TrkProcessDEDFrame(TNC);
						
					TNC->HOSTSTATE = 0;
					break;
				}

				if (character > 0 && character < 6)
				{
					// Null Terminated Response)
					
					TNC->HOSTSTATE = 5;
					CURSOR = &TNC->DEDBuffer[0];
					break;
				}

				if (character > 5 && character < 8)
				{
					TNC->HOSTSTATE = 2;						// Get Length
					break;
				}

				// Invalid

				Debugprintf("TRK - Invalid MsgType %d %x %x %x", character, *(ptr), *(ptr+1), *(ptr+2));
				break;

			case 2:		//  Get Length

				TNC->MSGCOUNT = character;
				TNC->MSGCOUNT++;						// Param is len - 1
				TNC->MSGLENGTH = TNC->MSGCOUNT;
				CURSOR = &TNC->DEDBuffer[0];
				TNC->HOSTSTATE = 3;						// Get Data

				break;

			case 5:		//  Collecting Null Terminated Response

				*(CURSOR++) = character;
				
				if (character)
					continue;			// MORE TO COME

				TrkProcessDEDFrame(TNC);

				TNC->HOSTSTATE = 0;
				TNC->InputLen = 0;

				break;

			default:

			//	RECEIVING Counted Response

			*(CURSOR++) = character;
			TNC->MSGCOUNT--;

			if (TNC->MSGCOUNT)
				continue;			// MORE TO COME

			TNC->InputLen = (int)(CURSOR - TNC->DEDBuffer);
			TrkProcessDEDFrame(TNC);

			TNC->HOSTSTATE = 0;
			TNC->InputLen = 0;
			}
		}
	}

	// End of Input - Save buffer position

	TNC->InputLen = (int)(CURSOR - TNC->DEDBuffer);
	TNC->RXLen = 0;
}


//#include "Mmsystem.h"


VOID RPRWriteCOMBlock(struct TNCINFO * TNC, UCHAR * Msg, int Len)
{
	int txlen = send(TNC->TCPSock, Msg, Len, 0);
}


//1:fm G8BPQ to KD6PGI-1 ctl I11^ pid F0
//fm KD6PGI-1 to G8BPQ ctl DISC+

//VOID SwitchToRPacket(struct TNCINFO * TNC, char * Baud);
//VOID SwitchToNormPacket(struct TNCINFO * TNC, char * Baud);
//VOID SendRPBeacon(struct TNCINFO * TNC);

VOID WinRPRProcessReceivedPacket(struct TNCINFO * TNC)
{
	int Length, Len;
	UCHAR  * ptr;
	UCHAR character;
	UCHAR * CURSOR;

	Len = recv(TNC->TCPSock, &TNC->RXBuffer[TNC->RXLen], 500 - TNC->RXLen, 0);

	if (Len == 0 || Len == SOCKET_ERROR)
	{
		// Does this mean closed?

		int err = GetLastError();

		closesocket(TNC->TCPSock);

		TNC->TCPSock = 0;

		TNC->CONNECTED = FALSE;
		TNC->Streams[0].ReportDISC = TRUE;

		sprintf(TNC->WEB_COMMSSTATE, "Connection to TNC lost");
		MySetWindowText(TNC->xIDC_COMMSSTATE, TNC->WEB_COMMSSTATE);

		return;					
	}

	TNC->RXLen += Len;

	Length = TNC->RXLen;
	
	ptr = TNC->RXBuffer;

	CURSOR = &TNC->DEDBuffer[TNC->InputLen];

	if ((TNC->HostMode == 0 || TNC->ReinitState == 10) && Length > 80)
	{
		// Probably Signon Message

		if (TNC->WRITELOG)
			WriteDebugLogLine(TNC->Port, 'R', ptr, Length);

		ptr[Length] = 0;
		Debugprintf("TRK %s", ptr);
		TNC->RXLen = 0;
		return;
	}

	if (TNC->HostMode == 0)
	{
		// If we are just restarting, and TNC is in host mode, we may get "Invalid Channel" Back
		
		if (memcmp(ptr, "\x18\x02INVALID", 9) == 0)
		{
			if (TNC->WRITELOG)
				WriteDebugLogLine(TNC->Port, 'R', ptr, Length);

			TNC->HostMode = TRUE;
			TNC->HOSTSTATE = 0;
			TNC->Timeout = 0;
			TNC->RXLen = 0;
			return;
		}

		// Command is echoed as * command * 

		if (strstr(ptr, "*") || TNC->ReinitState == 5)		// 5 is waiting for reponse to JHOST1
		{
			TrkProcessTermModeResponse(TNC);
			TNC->RXLen = 0;
			TNC->HOSTSTATE = 0;

			return;
		}
	}

	if (TNC->ReinitState == 10)
	{
		if (Length == 1 && *(ptr) == '.')		// 01 echoed as .
		{
			// TNC is in Term Mode

			if (TNC->WRITELOG)
				WriteDebugLogLine(TNC->Port, 'R', ptr, Length);
			TNC->ReinitState = 0;
			TNC->HostMode = 0;

			return;
		}
	}


	while (Length--)
	{
		character = *(ptr++);

		if (TNC->HostMode)
		{
			// n       0        Success (nothing follows)
			// n       1        Success (message follows, null terminated)
			// n       2        Failure (message follows, null terminated)
			// n       3        Link Status (null terminated)
			// n       4        Monitor Header (null terminated)
			// n       5        Monitor Header (null terminated)
			// n       6        Monitor Information (preceeded by length-1)
			// n       7        Connect Information (preceeded by length-1)


			switch(TNC->HOSTSTATE)
			{
			case 0: 	//  SETCHANNEL

				TNC->MSGCHANNEL = character;
				TNC->HOSTSTATE++;
				break;

			case 1:		//	SETMSGTYPE

				TNC->MSGTYPE = character;

				if (character == 0)
				{
					// Success, no more info

					TrkProcessDEDFrame(TNC);
						
					TNC->HOSTSTATE = 0;
					break;
				}

				if (character > 0 && character < 6)
				{
					// Null Terminated Response)
					
					TNC->HOSTSTATE = 5;
					CURSOR = &TNC->DEDBuffer[0];
					break;
				}

				if (character > 5 && character < 8)
				{
					TNC->HOSTSTATE = 2;						// Get Length
					break;
				}

				// Invalid

				Debugprintf("TRK - Invalid MsgType %d %x %x %x", character, *(ptr), *(ptr+1), *(ptr+2));
				break;

			case 2:		//  Get Length

				TNC->MSGCOUNT = character;
				TNC->MSGCOUNT++;						// Param is len - 1
				TNC->MSGLENGTH = TNC->MSGCOUNT;
				CURSOR = &TNC->DEDBuffer[0];
				TNC->HOSTSTATE = 3;						// Get Data

				break;

			case 5:		//  Collecting Null Terminated Response

				*(CURSOR++) = character;
				
				if (character)
					continue;			// MORE TO COME

				TrkProcessDEDFrame(TNC);

				TNC->HOSTSTATE = 0;
				TNC->InputLen = 0;

				break;

			default:

			//	RECEIVING Counted Response

			*(CURSOR++) = character;
			TNC->MSGCOUNT--;

			if (TNC->MSGCOUNT)
				continue;			// MORE TO COME

			TNC->InputLen = CURSOR - TNC->DEDBuffer;
			TrkProcessDEDFrame(TNC);

			TNC->HOSTSTATE = 0;
			TNC->InputLen = 0;
			}
		}
	}

	// End of Input - Save buffer position

	TNC->InputLen = CURSOR - TNC->DEDBuffer;
	TNC->RXLen = 0;
}



#ifndef LINBPQ

static BOOL CALLBACK EnumTNCWindowsProc(HWND hwnd, LPARAM  lParam)
{
	char wtext[100];
	struct TNCINFO * TNC = (struct TNCINFO *)lParam; 
	UINT ProcessId;
	char FN[MAX_PATH] = "";
	HANDLE hProc;

	if (TNC->ProgramPath == NULL)
		return FALSE;

	GetWindowText(hwnd,wtext,99);

	if (strstr(wtext,"WinRPR"))
	{
		GetWindowThreadProcessId(hwnd, &ProcessId);

		if (TNC->PID == ProcessId)
		{
			 // Our Process

			hProc =  OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, ProcessId);

			if (hProc && GetModuleFileNameExPtr)
			{
				GetModuleFileNameExPtr(hProc, NULL, FN, MAX_PATH);

				// Make sure this is the right copy

				CloseHandle(hProc);

				if (_stricmp(FN, TNC->ProgramPath))
					return TRUE;					//Wrong Copy
			}

			TNC->PID = ProcessId;

			sprintf (wtext, "WinRPR - BPQ %s", TNC->PortRecord->PORTCONTROL.PORTDESCRIPTION);
			SetWindowText(hwnd, wtext);
			return FALSE;
		}
	}
	
	return (TRUE);
}
#endif





VOID WinRPRThread(void * portptr);

int ConnecttoWinRPR(int port)
{
	_beginthread(WinRPRThread, 0, (void *)(size_t)port);

	return 0;
}

VOID WinRPRThread(void * portptr)
{
	// Opens socket, connects and looks for data
	
	int port = (int)(size_t)portptr;
	char Msg[255];
	int i, ret;
	u_long param=1;
	BOOL bcopt=TRUE;
	struct hostent * HostEnt;
	struct TNCINFO * TNC = TNCInfo[port];
	fd_set readfs;
	fd_set errorfs;
	struct timeval timeout;
	char * ptr1;
	char * ptr2;
	UINT * buffptr;

	if (TNC->HostName == NULL)
		return;

	TNC->BusyFlags = 0;

	TNC->CONNECTING = TRUE;

	Sleep(3000);		// Allow init to complete 

#ifdef WIN32
	if (strcmp(TNC->HostName, "127.0.0.1") == 0)
	{
		// can only check if running on local host
		
		TNC->PID = GetListeningPortsPID(TNC->destaddr.sin_port);
		if (TNC->PID == 0)
		{
			TNC->CONNECTING = FALSE;
			sprintf(TNC->WEB_COMMSSTATE, "Waiting for TNC");
			MySetWindowText(TNC->xIDC_COMMSSTATE, TNC->WEB_COMMSSTATE);

			return;						// Not listening so no point trying to connect
		}

		// Get the File Name in case we want to restart it.

		if (TNC->ProgramPath == NULL)
		{
			if (GetModuleFileNameExPtr)
			{
				HANDLE hProc;
				char ExeName[256] = "";

				hProc =  OpenProcess(PROCESS_QUERY_INFORMATION |PROCESS_VM_READ, FALSE, TNC->PID);
	
				if (hProc)
				{
					GetModuleFileNameExPtr(hProc, 0,  ExeName, 255);
					CloseHandle(hProc);

					TNC->ProgramPath = _strdup(ExeName);
				}
			}
		}
	}
#endif

//	// If we started the TNC make sure it is still running.

//	if (!IsProcess(TNC->PID))
//	{
//		RestartTNC(TNC);
//		Sleep(3000);
//	}


	TNC->destaddr.sin_addr.s_addr = inet_addr(TNC->HostName);
	TNC->Datadestaddr.sin_addr.s_addr = inet_addr(TNC->HostName);

	if (TNC->destaddr.sin_addr.s_addr == INADDR_NONE)
	{
		//	Resolve name to address

		HostEnt = gethostbyname (TNC->HostName);
		 
		 if (!HostEnt)
		 {
		 	TNC->CONNECTING = FALSE;
			return;			// Resolve failed
		 }
		 memcpy(&TNC->destaddr.sin_addr.s_addr,HostEnt->h_addr,4);
		 memcpy(&TNC->Datadestaddr.sin_addr.s_addr,HostEnt->h_addr,4);
	}

//	closesocket(TNC->TCPSock);
//	closesocket(TNC->TCPDataSock);

	TNC->TCPSock = socket(AF_INET,SOCK_STREAM,0);

	if (TNC->TCPSock == INVALID_SOCKET)
	{
		i=sprintf(Msg, "Socket Failed for WinRPR socket - error code = %d\r\n", WSAGetLastError());
		WritetoConsoleLocal(Msg);

	 	TNC->CONNECTING = FALSE;
  	 	return; 
	}

	setsockopt(TNC->TCPSock, SOL_SOCKET, SO_REUSEADDR, (const char FAR *)&bcopt, 4);

//	sinx.sin_family = AF_INET;
//	sinx.sin_addr.s_addr = INADDR_ANY;
//	sinx.sin_port = 0;

	sprintf(TNC->WEB_COMMSSTATE, "Connecting to TNC");
	MySetWindowText(TNC->xIDC_COMMSSTATE, TNC->WEB_COMMSSTATE);



	if (connect(TNC->TCPSock,(LPSOCKADDR) &TNC->destaddr,sizeof(TNC->destaddr)) == 0)
	{
		//
		//	Connected successful
		//

#ifndef LINBPQ
		EnumWindows(EnumTNCWindowsProc, (LPARAM)TNC);
#endif

	}
	else
	{
		if (TNC->Alerted == FALSE)
		{
   			sprintf(Msg, "Connect Failed for WinRPR socket - error code = %d Port %d\n",
				WSAGetLastError(), htons(TNC->destaddr.sin_port));
	
			WritetoConsoleLocal(Msg);
			sprintf(TNC->WEB_COMMSSTATE, "Connection to TNC failed");
			MySetWindowText(TNC->xIDC_COMMSSTATE, TNC->WEB_COMMSSTATE);

			TNC->Alerted = TRUE;
		}
		
		closesocket(TNC->TCPSock);
		TNC->TCPSock = 0;
	 	TNC->CONNECTING = FALSE;
		return;
	}

	Sleep(1000);

	TNC->LastFreq = 0;			//	so V4 display will be updated

 	TNC->CONNECTING = FALSE;
	TNC->CONNECTED = TRUE;
	TNC->BusyFlags = 0;
	TNC->InputLen = 0;

	// Send INIT script

	// VARA needs each command in a separate send

	ptr1 = &TNC->InitScript[0];

	GetSemaphore(&Semaphore, 52);

	while(TNC->BPQtoWINMOR_Q)
	{
		buffptr = Q_REM(&TNC->BPQtoWINMOR_Q);

		if (buffptr)
			ReleaseBuffer(buffptr);
	}


	while (ptr1 && ptr1[0])
	{
		unsigned char c;
		
		ptr2 = strchr(ptr1, 13);
		
		if (ptr2)
		{
			c = *(ptr2 + 1);		// Save next char
			*(ptr2 + 1) = 0;		// Terminate string
		}
//		VARASendCommand(TNC, ptr1, TRUE);

		if (ptr2)
			*(1 + ptr2++) = c;		// Put char back 

		ptr1 = ptr2;
	}
	
	TNC->Alerted = TRUE;

	sprintf(TNC->WEB_COMMSSTATE, "Connected to WinRPR TNC");		
	MySetWindowText(TNC->xIDC_COMMSSTATE, TNC->WEB_COMMSSTATE);

	FreeSemaphore(&Semaphore);

	sprintf(Msg, "Connected to WinRPR TNC Port %d\r\n", TNC->Port);
	WritetoConsoleLocal(Msg);


	#ifndef LINBPQ
//	FreeSemaphore(&Semaphore);
	Sleep(1000);		// Give VARA time to update Window title
//	EnumWindows(EnumVARAWindowsProc, (LPARAM)TNC);
//	GetSemaphore(&Semaphore, 52);
#endif


	while (TNC->CONNECTED)
	{
		FD_ZERO(&readfs);	
		FD_ZERO(&errorfs);

		FD_SET(TNC->TCPSock,&readfs);
		FD_SET(TNC->TCPSock,&errorfs);

		timeout.tv_sec = 600;
		timeout.tv_usec = 0;				// We should get messages more frequently that this

		ret = select((int)TNC->TCPSock + 1, &readfs, NULL, &errorfs, &timeout);
		
		if (ret == SOCKET_ERROR)
		{
			Debugprintf("WinRPR Select failed %d ", WSAGetLastError());
			goto Lost;
		}
		if (ret > 0)
		{
			//	See what happened

			if (FD_ISSET(TNC->TCPSock, &readfs))
			{
				GetSemaphore(&Semaphore, 52);
				WinRPRProcessReceivedPacket(TNC);
				FreeSemaphore(&Semaphore);
			}
								
			if (FD_ISSET(TNC->TCPSock, &errorfs))
			{
Lost:	
				sprintf(Msg, "WinRPR Connection lost for Port %d\r\n", TNC->Port);
				WritetoConsoleLocal(Msg);

				sprintf(TNC->WEB_COMMSSTATE, "Connection to TNC lost");
				MySetWindowText(TNC->xIDC_COMMSSTATE, TNC->WEB_COMMSSTATE);

				TNC->CONNECTED = FALSE;
				TNC->Alerted = FALSE;

				if (TNC->PTTMode)
					Rig_PTT(TNC, FALSE);			// Make sure PTT is down

				if (TNC->Streams[0].Attached)
					TNC->Streams[0].ReportDISC = TRUE;

				TNC->TCPSock = 0;
				return;
			}
		}
	}
	sprintf(Msg, "WinRPR Thread Terminated Port %d\r\n", TNC->Port);
	WritetoConsoleLocal(Msg);
}






