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
#include "tncinfo.h"

//#include "bpq32.h"

static char ClassName[]="TRACKERSTATUS";
static char WindowTitle[] = "SCS Tracker";
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
BOOL TrkWriteCommBlock(struct TNCINFO * TNC);
BOOL DestroyTTYInfo(int port);
static void DEDCheckRX(struct TNCINFO * TNC);
VOID DEDPoll(int Port);
VOID StuffAndSend(struct TNCINFO * TNC, UCHAR * Msg, int Len);
unsigned short int compute_crc(unsigned char *buf,int len);
int Unstuff(UCHAR * MsgIn, UCHAR * MsgOut, int len);
VOID TrkProcessDEDFrame(struct TNCINFO * TNC);
VOID TrkProcessTermModeResponse(struct TNCINFO * TNC);
VOID TrkExitHost(struct TNCINFO * TNC);
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
extern VOID TrkTidyClose(struct TNCINFO * TNC, int Stream);
extern VOID TrkForcedClose(struct TNCINFO * TNC, int Stream);
extern VOID TrkCloseComplete(struct TNCINFO * TNC, int Stream);
VOID RPRWriteCOMBlock(struct TNCINFO * TNC, UCHAR * Msg, int Len);
void WriteDebugLogLine(int Port, char Dirn, char * Msg, int MsgLen);
VOID UpdateMHwithDigis(struct TNCINFO * TNC, UCHAR * Call, char Mode, char Direction);


BOOL TrkWriteCommBlock(struct TNCINFO * TNC)
{
	if (TNC->Hardware == H_WINRPR)
		RPRWriteCOMBlock(TNC, TNC->TXBuffer, TNC->TXLen);
	else
		WriteCOMBlock(TNC->hDevice, TNC->TXBuffer, TNC->TXLen);

	if (TNC->WRITELOG)
		WriteDebugLogLine(TNC->Port, 'T', TNC->TXBuffer, TNC->TXLen);

	TNC->Timeout = 20;				// 2 secs
	return TRUE;
}


VOID TRKSuspendPort(struct TNCINFO * TNC, struct TNCINFO * ThisTNC)
{
	struct STREAMINFO * STREAM = &TNC->Streams[0];

	STREAM->CmdSet = STREAM->CmdSave = zalloc(100);
	sprintf(STREAM->CmdSet, "\1\1\1IDSPTNC");
}

VOID TRKReleasePort(struct TNCINFO * TNC)
{
	struct STREAMINFO * STREAM = &TNC->Streams[0];

	STREAM->CmdSet = STREAM->CmdSave = zalloc(100);
	sprintf(STREAM->CmdSet, "\1\1\1I%s", TNC->NodeCall);
}

static FILE * LogHandle[32] = {0};
time_t LogTime[32] = {0};

static char BaseDir[MAX_PATH]="c:\\";

VOID CloseDebugLogFile(int Port)
{
	fclose(LogHandle[Port]);
	LogHandle[Port] = NULL;
}

BOOL OpenDebugLogFile(int Port)
{
	UCHAR FN[MAX_PATH];

	time_t T;
	struct tm * tm;

	if (LogHandle[Port])
		return TRUE;				// already open

	T = LogTime[Port] = time(NULL);
	tm = gmtime(&T);	

	sprintf(FN,"%s/logs/Port%02dDebugLog_%02d%02d.txt", LogDirectory, Port, tm->tm_mon + 1, tm->tm_mday);

	LogHandle[Port] = fopen(FN, "ab");
	
	return (LogHandle[Port] != NULL);
}

void WriteDebugLogLine(int Port, char Dirn, char * Msg, int MsgLen)
{
	if (LogHandle[Port] == NULL)
		OpenDebugLogFile(Port);

	if (LogHandle[Port])
	{
		time_t T;
		struct tm * tm;
		char hddr[32];
		char hex[4];
		int len;
		UCHAR c;
		char textbit[33] = "                                ";
		int i;
		T = time(NULL);
		tm = gmtime(&T);	
			
		len = sprintf(hddr,"%02d:%02d:%02d %c Len %3d ", tm->tm_hour, tm->tm_min, tm->tm_sec, Dirn, MsgLen);

		fwrite(hddr, 1, len, LogHandle[Port]);

		// Write up to 32 bytes of Text - replace ctrl with .

		for (i = 0; i < 33 && i < MsgLen; i++)
		{
			c = Msg[i];
			if (c < 32) c = '.';
			textbit[i] = c;
		}
		fwrite(textbit, 1, 32, LogHandle[Port]);

		// display fiorsy 16 in hex

		if (MsgLen > 16)
			MsgLen = 16;

		while (MsgLen--)
		{
			c = *(Msg++);
			sprintf(hex, " %02X", c);
			fwrite(hex, 1, 3, LogHandle[Port]);
		}
		fwrite("\r\n", 1, 2, LogHandle[Port]);

		if (T - LogTime[Port] > 30)
			CloseDebugLogFile(Port);

	}
}




static int ProcessLine(char * buf, int Port)
{
	UCHAR * ptr,* p_cmd;
	char * p_port = 0;
	int BPQport;
	int len=510;
	struct TNCINFO * TNC;
	char errbuf[256];

	BPQport = Port;

	TNC = TNCInfo[BPQport] = malloc(sizeof(struct TNCINFO));
	memset(TNC, 0, sizeof(struct TNCINFO));

	TNC->InitScript = malloc(1000);
	TNC->InitScript[0] = 0;

	strcpy(TNC->NormSpeed, "300");		// HF Packet
	strcpy(TNC->RobustSpeed, "R600");

	goto ConfigLine;

	while(TRUE)
	{
		if (GetLine(buf) == 0)
			return TRUE;
ConfigLine:
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
		else
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
		
	if (TNC->hDevice == 0)
	{
		// Clear anything from UI_Q

		while (TNC->PortRecord->UI_Q)
		{
			buffptr = Q_REM(&TNC->PortRecord->UI_Q);
			ReleaseBuffer(buffptr);
		}

		if (fn > 3  && fn < 6)
			goto ok;

		// Try to reopen every 30 secs

		TNC->ReopenTimer++;

		if (TNC->ReopenTimer < 300)
			return 0;

		TNC->ReopenTimer = 0;
		
		if (TNC->PortRecord->PORTCONTROL.PortStopped == 0)
			OpenCOMMPort(TNC, TNC->PortRecord->PORTCONTROL.SerialPortName, TNC->PortRecord->PORTCONTROL.BAUDRATE, TRUE);

		if (TNC->hDevice == 0)
			return 0;
	}
ok:
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
		CloseCOMPort(TNC->hDevice);
		TNC->hDevice =(HANDLE)0;
		TNC->ReopenTimer = 250;
		TNC->HostMode = FALSE;

		return (0);

	case 5:				// Close

		// Ensure in Pactor

		TrkExitHost(TNC);

		Sleep(25);

		CloseCOMPort(TNC->hDevice);
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

int TrkWebProc(struct TNCINFO * TNC, char * Buff, BOOL LOCAL)
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
	"<head><title>SCSTracker Status</title></head><body><h2>SCSTracker Status</h2>", Interval);

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


void * TrackerExtInit(EXTPORTDATA *  PortEntry)
{
	char msg[500];
	struct TNCINFO * TNC;
	int port;
	char * ptr;
	int Stream = 0;
	char * TempScript;

	//
	//	Will be called once for each DED Host TNC Port
	//	The COM port number is in IOBASE
	//

	sprintf(msg,"SCSTRK %s", PortEntry->PORTCONTROL.SerialPortName);
	
	WritetoConsoleLocal(msg);

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
	
	TNC->Port = port;
	TNC->Hardware = H_TRK;

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

	PortEntry->PORTCONTROL.PORTSTARTCODE = KAMStartPort;
	PortEntry->PORTCONTROL.PORTSTOPCODE = KAMStopPort;

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


	TNC->WebWindowProc = TrkWebProc;
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
		char Cmd[40];
		TNC->Robust = TRUE;
		sprintf(Cmd, "%%B %s\r", TNC->RobustSpeed);
		strcat(TNC->InitScript, Cmd);
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

	OpenCOMMPort(TNC,PortEntry->PORTCONTROL.SerialPortName, PortEntry->PORTCONTROL.BAUDRATE, FALSE);

	TNC->InitPtr = TNC->InitScript;

	if (TNC->RIG == &TNC->DummyRig || TNC->RIG == NULL)
		TNC->SwitchToPactor = TNC->RobustTime;		// Don't alternate Modes if using Rig Control

	WritetoConsoleLocal("\n");

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
			TNC->TermReinitCount = 0;
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

VOID DEDPoll(int Port)
{
	struct TNCINFO * TNC = TNCInfo[Port];
	UCHAR * Poll = TNC->TXBuffer;
	char Status[80];
	int Stream = 0;
	int nn;
	struct STREAMINFO * STREAM;

	for (Stream = 0; Stream <= MaxStreams; Stream++)
	{
		if (TNC->PortRecord->ATTACHEDSESSIONS[Stream] && TNC->Streams[Stream].Attached == 0)
		{
			// New Attach

			int calllen = 0;

			TNC->CurrentMode = 0;				// Mode may be changed manually


			TNC->Streams[Stream].Attached = TRUE;
			TNC->PortRecord->ATTACHEDSESSIONS[Stream]->L4USER[6] |= 0x60; // Ensure P or T aren't used on ax.25
			calllen = ConvFromAX25(TNC->PortRecord->ATTACHEDSESSIONS[Stream]->L4USER, TNC->Streams[Stream].MyCall);
			TNC->Streams[Stream].MyCall[calllen] = 0;

			// Set call to null to stop inbound connects (We only support one stream)

			TNC->Streams[Stream].CmdSet = TNC->Streams[Stream].CmdSave = zalloc(100);
			sprintf(TNC->Streams[Stream].CmdSet, "\1\1\1IDSPTNC");

			if (Stream == 0)
			{
				sprintf(TNC->WEB_TNCSTATE, "In Use by %s", TNC->Streams[0].MyCall);
				SetWindowText(TNC->xIDC_TNCSTATE, TNC->WEB_TNCSTATE);
				TNC->WEB_CHANGED = TRUE;

				// Stop Scanner

				TNC->SwitchToPactor = 0;						// Cancel any RP to Pactor switch

				sprintf(Status, "%d SCANSTOP", TNC->Port);
				Rig_Command( (TRANSPORTENTRY *) -1, Status);

				SuspendOtherPorts(TNC);			// Prevent connects on other ports in same scan gruop
			}
		}
	}

	if (TNC->Timeout)
	{
		TNC->Timeout--;

		if (TNC->Timeout)			// Still waiting
			return;

		// Can't use retries, as we have no way of detecting lost chars. Have to re-init on timeout

		if (TNC->HostMode == 0 || TNC->ReinitState == 10)		// 10 is Recovery Mode
		{
			TNC->TermReinitCount++;

			if (TNC->TermReinitCount == 10)
				goto reinit;

			DoTermModeTimeout(TNC);
			return;
		}

		// Timed out in host mode - Clear any connection and reinit the TNC

		Debugprintf("DEDHOST - Link to TNC Lost Port %d", TNC->Port);

reinit:

		TNC->TNCOK = FALSE;

		sprintf(TNC->WEB_COMMSSTATE, "%s Open but TNC not responding", TNC->PortRecord->PORTCONTROL.SerialPortName);
		SetWindowText(TNC->xIDC_COMMSSTATE, TNC->WEB_COMMSSTATE);
		TNC->WEB_CHANGED = TRUE;

		TNC->HostMode = 0;
		TNC->ReinitState = 0;
		TNC->TermReinitCount = 0;

		CloseCOMPort(TNC->hDevice);
		OpenCOMMPort(TNC, TNC->PortRecord->PORTCONTROL.SerialPortName, TNC->PortRecord->PORTCONTROL.BAUDRATE, TRUE);

		TNC->InitPtr = TNC->InitScript;
		TNC->HOSTSTATE = 0;

		for (Stream = 0; Stream <= MaxStreams; Stream++)
		{
			if (TNC->PortRecord->ATTACHEDSESSIONS[Stream])		// Connected
			{
				TNC->Streams[Stream].Connected = FALSE;		// Back to Command Mode
				TNC->Streams[Stream].ReportDISC = TRUE;		// Tell Node
			}
		}

		// Clear anything from UI_Q

		while (TNC->PortRecord->UI_Q)
		{
			UINT * buffptr = Q_REM(&TNC->PortRecord->UI_Q);
			ReleaseBuffer(buffptr);
		}
	}

	if (TNC->SwitchToPactor)
	{
		TNC->SwitchToPactor--;

		if (TNC->SwitchToPactor == 0)
		{
			TNC->SwitchToPactor = TNC->RobustTime;
			if (TNC->Robust)
				SwitchToNormPacket(TNC, "");
			else
				SwitchToRPacket(TNC, TNC->RobustSpeed);
		}
	}


	for (Stream = 0; Stream <= MaxStreams; Stream++)
	{
		STREAM = &TNC->Streams[Stream];

		if (STREAM->Attached)
			CheckForDetach(TNC, Stream, STREAM, TrkTidyClose, TrkForcedClose, TrkCloseComplete);

		if (STREAM->NeedDisc)
		{
			STREAM->NeedDisc--;

			if (STREAM->NeedDisc == 0)
				STREAM->ReportDISC = TRUE;

		}

		if (TNC->Timeout)
			return;				// We've sent something
	}

	// if we have just restarted or TNC appears to be in terminal mode, run Initialisation Sequence

	if (!TNC->HostMode)
	{
		DoTNCReinit(TNC);
		return;
	}

	if (TNC->InitPtr)
	{
		char * start, * end;
		int len;

		start = TNC->InitPtr;

		if (*(start) == 0)			// End of Script
		{
			TNC->InitPtr = NULL;
			Debugprintf("TRK - Init Complete Port %d", TNC->Port);
		}
		else
		{
			end = strchr(start, 13);
			len = (int)(++end - start) - 1;	// exclude cr

			TNC->InitPtr = end;

			Poll[0] = 0;			// Channel
			Poll[1] = 1;			// Command
			Poll[2] = len - 1;
			memcpy(&Poll[3], start, len);

			StuffAndSend(TNC, Poll, len + 3);

			return;

		}
	}

	if (TNC->NeedPACTOR)
	{
		TNC->NeedPACTOR--;

		if (TNC->NeedPACTOR == 0)
		{
			TNC->Streams[0].CmdSet = TNC->Streams[0].CmdSave = zalloc(100);
			sprintf(TNC->Streams[0].CmdSet, "\1\1\1%%B %s%c\1\1\1I%s", (TNC->RobustDefault) ? TNC->RobustSpeed : TNC->NormSpeed, 0, TNC->NodeCall);
			
			strcpy(TNC->Streams[0].MyCall, TNC->NodeCall);
		}
	}

	for (Stream = 0; Stream <= MaxStreams; Stream++)
	{
		if (TNC->Streams[Stream].CmdSet)
		{
			char * start, * end;
			int len;

			start = TNC->Streams[Stream].CmdSet;

			if (*(start + 2) == 0)			// End of Script
			{
				free(TNC->Streams[Stream].CmdSave);
				TNC->Streams[Stream].CmdSet = NULL;
			}
			else
			{
				end = strchr(start + 3, 0);
				len = (int)(++end - start) - 1;	// exclude cr
				TNC->Streams[Stream].CmdSet = end;

//				Debugprintf("TRK Cmdset %s", start + 3);

				memcpy(&Poll[0], start, len);
				Poll[2] = len - 4;

				StuffAndSend(TNC, Poll, len);

				return;
			}
		}
	}

	for (nn = 0; nn <= MaxStreams; nn++)
	{
		Stream = TNC->LastStream++;

		if (TNC->LastStream > MaxStreams) TNC->LastStream = 0;

		if (TNC->TNCOK && TNC->Streams[Stream].BPQtoPACTOR_Q)
		{
			int datalen;
			PMSGWITHLEN buffptr;
			char * Buffer;

			buffptr = Q_REM(&TNC->Streams[Stream].BPQtoPACTOR_Q);

			datalen = (int)buffptr->Len;
			Buffer = (char *)buffptr->Data;	// Data portion of frame

			Poll[0] = TNC->Streams[Stream].DEDStream;		// Channel

			if (TNC->Streams[Stream].Connected)
			{
				if (TNC->SwallowSignon && Stream == 0)
				{
					TNC->SwallowSignon = FALSE;	
					if (strstr(Buffer, "Connected"))	// Discard *** connected
					{
						ReleaseBuffer(buffptr);
						return;
					}
				}

				Poll[1] = 0;			// Data
				TNC->Streams[Stream].BytesTXed += datalen;

				Poll[2] = datalen - 1;
				memcpy(&Poll[3], Buffer, datalen);

				WritetoTrace(TNC, Buffer, datalen);

				ReleaseBuffer(buffptr);

				StuffAndSend(TNC, Poll, datalen + 3);

				TNC->Streams[Stream].InternalCmd = TNC->Streams[Stream].Connected;

				if (STREAM->Disconnecting && TNC->Streams[Stream].BPQtoPACTOR_Q == 0)
					TrkTidyClose(TNC, 0);

				// Make sure Node Keepalive doesn't kill session.

				{
					TRANSPORTENTRY * SESS = TNC->PortRecord->ATTACHEDSESSIONS[0];

					if (SESS)
					{
						SESS->L4KILLTIMER = 0;
						SESS = SESS->L4CROSSLINK;
						if (SESS)
							SESS->L4KILLTIMER = 0;
					}
				}

				ShowTraffic(TNC);
				return;
			}

			// Command. Do some sanity checking and look for things to process locally

			Poll[1] = 1;			// Command
			datalen--;				// Exclude CR

			if (datalen == 0)		// Null Command
			{
				ReleaseBuffer(buffptr);
				return;
			}

			Buffer[datalen] = 0;	// Null Terminate
			_strupr(Buffer);

			if (_memicmp(Buffer, "D", 1) == 0)
			{
				TNC->Streams[Stream].ReportDISC = TRUE;		// Tell Node
				ReleaseBuffer(buffptr);
				return;
			}

			if (memcmp(Buffer, "RADIO ", 6) == 0)
			{
				sprintf(&Buffer[40], "%d %s", TNC->Port, &Buffer[6]);

				if (Rig_Command(TNC->PortRecord->ATTACHEDSESSIONS[0]->L4CROSSLINK, &Buffer[40]))
				{
					ReleaseBuffer(buffptr);
				}
				else
				{
					buffptr->Len = sprintf(buffptr->Data, "%s", &Buffer[40]);
					C_Q_ADD(&TNC->Streams[Stream].PACTORtoBPQ_Q, buffptr);
				}
				return;
			}

			if ((Stream == 0) && memcmp(Buffer, "RPACKET", 7) == 0)
			{
				TNC->Robust = TRUE;
				buffptr->Len = sprintf(buffptr->Data, "TRK} OK\r");
				C_Q_ADD(&TNC->Streams[0].PACTORtoBPQ_Q, buffptr);
				SetWindowText(TNC->xIDC_MODE, "Robust Packet");
				strcpy(TNC->WEB_MODE, "Robust Packet");
				TNC->WEB_CHANGED = TRUE;

				return;
			}

			if ((Stream == 0) && memcmp(Buffer, "HFPACKET", 8) == 0)
			{
				if (TNC->ForceRobust)
				{
					buffptr->Len = sprintf(buffptr->Data, "TRK} HF Packet Disabled\r");
					C_Q_ADD(&TNC->Streams[0].PACTORtoBPQ_Q, buffptr);
					return;
				}

				if (strlen(Buffer) > 10)
				{
					// Speed follows HFPACKET

					Buffer += 9;

					Buffer = strtok(Buffer, " \r");

					if (strlen(Buffer) < 6)
						strcpy(TNC->NormSpeed, Buffer);
				}

				buffptr->Len = sprintf(buffptr->Data, "TRK} OK\r");

				C_Q_ADD(&TNC->Streams[0].PACTORtoBPQ_Q, buffptr);
				TNC->Robust = FALSE;
				SetWindowText(TNC->xIDC_MODE, "HF Packet");
				strcpy(TNC->WEB_MODE, "HF Packet");
				TNC->WEB_CHANGED = TRUE;
				return;
			}

			if (Buffer[0] == 'C' && datalen > 2)	// Connect
			{
				if (*(++Buffer) == ' ') Buffer++;		// Space isn't needed

				memcpy(TNC->Streams[Stream].RemoteCall, Buffer, 9);

				TNC->Streams[Stream].Connecting = TRUE;

				if (Stream == 0)
				{
					// Send MYCall, Mode Command followed by connect 

					TNC->Streams[0].CmdSet = TNC->Streams[0].CmdSave = zalloc(100);

					sprintf(TNC->Streams[0].CmdSet, "\1\1\1%%B %s%c\1\1\1I%s%c\1\1\1C%s",
						(TNC->Robust) ? TNC->RobustSpeed : TNC->NormSpeed, 0, TNC->Streams[0].MyCall,0, Buffer);

					ReleaseBuffer(buffptr);

					sprintf(TNC->WEB_TNCSTATE, "%s Connecting to %s", TNC->Streams[Stream].MyCall, TNC->Streams[Stream].RemoteCall);
					SetWindowText(TNC->xIDC_TNCSTATE, TNC->WEB_TNCSTATE);
					TNC->WEB_CHANGED = TRUE;

					TNC->Streams[0].InternalCmd = FALSE;
					return;
				}
			}

			Poll[2] = datalen - 1;
			memcpy(&Poll[3], buffptr->Data, datalen);

			ReleaseBuffer(buffptr);

			StuffAndSend(TNC, Poll, datalen + 3);

			TNC->Streams[Stream].InternalCmd = TNC->Streams[Stream].Connected;

			return;
		}
	}

	if (TNC->TNCOK && TNC->PortRecord->UI_Q)
	{
		int datalen;
		char * Buffer;
		char CCMD[80] = "C";
		char Call[12] = "           ";	
		struct _MESSAGE * buffptr;

		buffptr = Q_REM(&TNC->PortRecord->UI_Q);

		datalen = buffptr->LENGTH - 7;
		Buffer = &buffptr->DEST[0];		// Raw Frame

		Buffer[datalen] = 0;

		// Buffer has an ax.25 header, which we need to pick out and set as channel 0 Connect address
		// before sending the beacon

		ConvFromAX25(Buffer, &Call[1]);			// Dest
		strlop(&Call[1], ' ');
		strcat(CCMD, Call);
		Buffer += 14;							// Skip Origin
		datalen -= 7;

		while ((Buffer[-1] & 1) == 0)
		{
			ConvFromAX25(Buffer, &Call[1]);
			strlop(&Call[1], ' ');
			strcat(CCMD, Call);
			Buffer += 7;	// End of addr
			datalen -= 7;
		}

		if (Buffer[0] == 3)				// UI
		{
			Buffer += 2;
			datalen -= 2;

			Poll[0] = 0;				// UI Channel
			Poll[1] = 1;				// CMD
			Poll[2] = (int)strlen(CCMD) - 1;
			strcpy(&Poll[3], CCMD);
			StuffAndSend(TNC, Poll, Poll[2] + 4);

			TNC->Streams[0].CmdSet = TNC->Streams[0].CmdSave = zalloc(400);
			sprintf(TNC->Streams[0].CmdSet, "%c%c%c%s", 0, 0, 1, Buffer);
		}

		ReleaseBuffer((UINT *)buffptr);
		return;
	}

	// if frames outstanding, issue a poll (but not too often)

	TNC->IntCmdDelay++;

	if (TNC->IntCmdDelay == 5 && TNC->Streams[0].FramesOutstanding)
	{
		Poll[0] = TNC->Streams[0].DEDStream;
		Poll[1] = 0x1;			// Command
		TNC->InternalCmd = TRUE;

		Poll[2] = 1;			// Len-1
		Poll[3] = '@';
		Poll[4] = 'B';			// Buffers
		StuffAndSend(TNC, Poll, 5);
		return;
	}

	if (TNC->IntCmdDelay > 10)
	{
		TNC->IntCmdDelay = 0;

		if (TNC->Streams[0].FramesOutstanding)
		{
			Poll[0] = TNC->Streams[0].DEDStream;
			Poll[1] = 0x1;			// Command
			TNC->InternalCmd = TRUE;

			Poll[2] = 0;			// Len-1
			Poll[3] = 'L';			// Status
			StuffAndSend(TNC, Poll, 4);

			return;
		}
	}
	// Need to poll channels 0 and 1 in turn

	TNC->StreamtoPoll++;

	if (TNC->StreamtoPoll > 1)
		TNC->StreamtoPoll = 0;

	Poll[0] = TNC->StreamtoPoll;	// Channel
	Poll[1] = 0x1;			// Command
	Poll[2] = 0;			// Len-1
	Poll[3] = 'G';			// Poll

	StuffAndSend(TNC, Poll, 4);
	TNC->InternalCmd = FALSE;

	return;

}

VOID DoTNCReinit(struct TNCINFO * TNC)
{
	UCHAR * Poll = TNC->TXBuffer;

	if (TNC->ReinitState == 0)
	{
		// Just Starting - Send a TNC Mode Command to see if in Terminal or Host Mode
		
		TNC->TNCOK = FALSE;
		sprintf(TNC->WEB_COMMSSTATE, "%s Initialising TNC", TNC->PortRecord->PORTCONTROL.SerialPortName);
		SetWindowText(TNC->xIDC_COMMSSTATE, TNC->WEB_COMMSSTATE);
		TNC->WEB_CHANGED = TRUE;

		memcpy(&TNC->TXBuffer[0], "\x18\x1b\r", 2);
		TNC->TXLen = 2;

		if (TrkWriteCommBlock(TNC) == FALSE)
		{
			CloseCOMPort(TNC->hDevice);
			OpenCOMMPort(TNC, TNC->PortRecord->PORTCONTROL.SerialPortName, TNC->PortRecord->PORTCONTROL.BAUDRATE, TRUE);
		}
		return;
	}

	if (TNC->ReinitState == 1)		// Forcing back to Term
		TNC->ReinitState = 0;

	if (TNC->ReinitState == 2)		// In Term State, Sending Initialisation Commands
	{
		// Put into Host Mode

		memcpy(Poll, "\x18\x1bJHOST1\r", 9);

		TNC->TXLen = 9;
		TrkWriteCommBlock(TNC);

		TNC->ReinitState = 5;
		return;
	}

	if (TNC->ReinitState == 5)
		TNC->ReinitState = 0;

}

VOID DoTermModeTimeout(struct TNCINFO * TNC)
{
	UCHAR * Poll = TNC->TXBuffer;

	if (TNC->ReinitState == 0)
	{
		//Checking if in Terminal Mode - Try to set back to Term Mode

		TNC->ReinitState = 1;
		TrkExitHost(TNC);

		return;
	}

	if (TNC->ReinitState == 1)
	{
		// No Response to trying to enter term mode - do error recovery

		Debugprintf("TRK - Starting Resync Port %d", TNC->Port);

		TNC->ReinitState = 10;
		TNC->ReinitCount = 256;
		TNC->HostMode = TRUE;			// Must be in Host Mode if we need recovery

		Poll[0] = 1;
		TNC->TXLen = 1;
		TrkWriteCommBlock(TNC);
		TNC->Timeout = 10;				// 2 secs

		return;
	}

	if (TNC->ReinitState == 10)
	{
		// Continue error recovery

		TNC->ReinitCount--;

		if (TNC->ReinitCount)
		{
			Poll[0] = 1;
			TNC->TXLen = 1;
			TrkWriteCommBlock(TNC);
			TNC->Timeout = 3;				// 0.3 secs

			return;
		}

		// Try Again

		Debugprintf("TRK Continuing recovery Port %d", TNC->Port);
	
		TNC->ReinitState = 0;

		// Close and re-open TNC

		TrkExitHost(TNC);
		Sleep(50);
		CloseCOMPort(TNC->hDevice);
		TNC->hDevice =(HANDLE)0;
		TNC->ReopenTimer = 290;
		TNC->HostMode = FALSE;
		TNC->TermReinitCount = 0;

		return;
	}
	if (TNC->ReinitState == 3)
	{
		// Entering Host Mode
	
		// Assume ok

		TNC->HostMode = TRUE;
		TNC->IntCmdDelay = 10;
		TNC->TermReinitCount = 0;

		return;
	}
}


//#include "Mmsystem.h"

VOID TrkExitHost(struct TNCINFO * TNC)
{
	UCHAR * Poll = TNC->TXBuffer;

	// Try to exit Host Mode

	TNC->TXBuffer[0] = 1;
	TNC->TXBuffer[1] = 1;
	TNC->TXBuffer[2] = 1;
	memcpy(&TNC->TXBuffer[3], "%R", 2);

	StuffAndSend(TNC, Poll, 5);

	return;
}

VOID StuffAndSend(struct TNCINFO * TNC, UCHAR * Msg, int Len)
{
	TNC->TXLen = Len;
	TrkWriteCommBlock(TNC);
}

VOID TrkProcessTermModeResponse(struct TNCINFO * TNC)
{
	UCHAR * Poll = TNC->TXBuffer;

	if (TNC->WRITELOG)
		WriteDebugLogLine(TNC->Port, 'R', TNC->RXBuffer, TNC->RXLen);

	if (TNC->ReinitState == 0)
	{
		// Testing if in Term Mode. It is, so can now send Init Commands

		TNC->InitPtr = TNC->InitScript;
		TNC->ReinitState = 2;
	}

	if (TNC->ReinitState == 1)
	{
		// trying to set term mode

		// If already in Term Mode, TNC echos command, with control chars replaced with '.'

		if (memcmp(TNC->RXBuffer, "....%R", 6) == 0)
		{
			// In term mode, Need to put into Host Mode

			TNC->ReinitState = 2;
			DoTNCReinit(TNC);
			return;
		}
	}

	if (TNC->ReinitState == 2)
	{
		// Sending Init Commands

		DoTNCReinit(TNC);		// Send Next Command
		return;
	}

	if (TNC->ReinitState == 5)	// Waiting for response to JHOST1
	{
		if (TNC->RXBuffer[TNC->RXLen-1] == 10 || TNC->RXBuffer[TNC->RXLen-1] == 13)	// NewLine
		{
			TNC->HostMode = TRUE;
			TNC->Timeout = 0;
			TNC->TermReinitCount = 0;
		}
		return;
	}
}

VOID TrkProcessDEDFrame(struct TNCINFO * TNC)
{
	PMSGWITHLEN buffptr;
	char * Buffer;				// Data portion of frame
	char Status[80];
	UINT Stream = 0;
	UCHAR * Msg = TNC->DEDBuffer;
	int framelen = TNC->InputLen;

	if (TNC->WRITELOG)
		WriteDebugLogLine(TNC->Port, 'R', Msg, framelen);

	if (TNC->ReinitState == 10)
	{
		// Recovering from Sync Failure

		// Any Response indicates we are in host mode, and back in sync

		TNC->HostMode = TRUE;
		TNC->Timeout = 0;
		TNC->ReinitState = 0;
		TNC->RXLen = 0;
		TNC->HOSTSTATE = 0;
		TNC->TermReinitCount = 0;

		Debugprintf("TRK - Resync Complete");
		return;
	}

	// Any valid frame is an ACK

	TNC->Timeout = 0;

	if (TNC->TNCOK == FALSE)
	{
		// Just come up
		
		TNC->TNCOK = TRUE;
		sprintf(TNC->WEB_COMMSSTATE, "%s TNC link OK", TNC->PortRecord->PORTCONTROL.SerialPortName);
		SetWindowText(TNC->xIDC_COMMSSTATE, TNC->WEB_COMMSSTATE);
		TNC->WEB_CHANGED = TRUE;
	}

	if (TNC->InitPtr)					// Response to Init Script
		return;

	if (TNC->MSGCHANNEL > 26)
		return;

	Stream = TNC->MSGCHANNEL - 1;

	//	See if Poll Reply or Data
	
	if (TNC->MSGTYPE == 0)
	{
		// Success - Nothing Follows

		if (Stream < 32)
			if (TNC->Streams[Stream].CmdSet)
				return;						// Response to Command Set or Init Script

		if ((TNC->TXBuffer[1] & 1) == 0)	// Data
			return;

		// If the response to a Command, then we should convert to a text "Ok" for forward scripts, etc

		if (TNC->TXBuffer[3] == 'G')	// Poll
			return;

		if (TNC->TXBuffer[3] == 'C')	// Connect - reply we need is async
			return;

		if (TNC->TXBuffer[3] == 'L')	// Shouldnt happen!
			return;


		if (TNC->TXBuffer[3] == 'J')	// JHOST
		{
			if (TNC->TXBuffer[8] == '0')	// JHOST0
			{
				TNC->Timeout = 1;			// 
				return;
			}
		}

		if (TNC->MSGCHANNEL == 0)			// Unproto Channel
			return;

		buffptr = GetBuff();

		if (buffptr == NULL) return;			// No buffers, so ignore

		buffptr->Len = sprintf(buffptr->Data, "TRK} Ok\r");

		C_Q_ADD(&TNC->Streams[Stream].PACTORtoBPQ_Q, buffptr);

		return;
	}

	if (TNC->MSGTYPE > 0 && TNC->MSGTYPE < 6)
	{
		// Success with message - null terminated

		char * ptr;
		int len;

		Buffer = Msg;
		
		ptr = strchr(Buffer, 0);

		if (ptr == 0)
			return;

		*(ptr++) = 13;
		*(ptr) = 0;

		len = (int)(ptr - Buffer);

		if (len > 256)
			return;

		// See if we need to process locally (Response to our command, Incoming Call, Disconencted, etc

		if (TNC->MSGTYPE < 3)						// 1 or 2 - Success or Fail
		{
			// See if a response to internal command

			if (TNC->InternalCmd)
			{
				// Process it

				char LastCmd = TNC->TXBuffer[3];

				if (LastCmd == 'L')		// Status
				{
					int s1, s2, s3, s4, s5, s6, num;

					num = sscanf(Buffer, "%d %d %d %d %d %d", &s1, &s2, &s3, &s4, &s5, &s6);
			
					TNC->Streams[Stream].FramesOutstanding = s3;
					return;
				}

				if (LastCmd == '@')		// @ Commands
				{
					if (TNC->TXBuffer[4]== 'B')	// Buffer Status
					{
						TNC->Buffers = atoi(Buffer);
						SetWindowText(TNC->xIDC_BUFFERS, Buffer);
						strcpy(TNC->WEB_BUFFERS, Buffer);
						return;
					}
				}

				if (LastCmd == '%')		// % Commands
				{				
					if (TNC->TXBuffer[4]== 'T')	// TX count Status
					{
						sprintf(TNC->WEB_TRAFFIC, "RX %d TX %d ACKED %s", TNC->Streams[Stream].BytesRXed, TNC->Streams[Stream].BytesTXed, Buffer);
						SetWindowText(TNC->xIDC_TRAFFIC, TNC->WEB_TRAFFIC);
						TNC->WEB_CHANGED = TRUE;
						return;
					}

					if (TNC->TXBuffer[4] == 'W')	// Scan Control
					{
						if (Msg[4] == '1')			// Ok to Change
							TNC->OKToChangeFreq = 1;
						else
							TNC->OKToChangeFreq = -1;
					}
				}
				return;
			}
			
			// Not Internal Command, so send to user

			if (TNC->Streams[Stream].CmdSet || TNC->InitPtr)
				return;						// Response to Command Set or Init Script

			if ((TNC->TXBuffer[1] & 1) == 0)	// Data
			{
				// Should we look for "CHANNEL NOT CONNECTED" here (or somewhere!)
				return;
			}

			// If the response to a Command, then we should convert to a text "Ok" for forward scripts, etc

			if (TNC->TXBuffer[3] == 'G')	// Poll
				return;

			if (TNC->TXBuffer[3] == 'C')	// Connect - reply we need is async
				return;

			if (TNC->TXBuffer[3] == 'L')	// Shouldnt happen!
				return;

			if (TNC->TXBuffer[3] == 'J')	// JHOST
			{	
				if (TNC->TXBuffer[8] == '0')	// JHOST0
				{
					TNC->Timeout = 1;			// 
					return;
				}
			}

			if (TNC->MSGCHANNEL == 0)			// Unproto Channel
				return;

			buffptr = GetBuff();

			if (buffptr == NULL) return;			// No buffers, so ignore

			buffptr->Len = sprintf(buffptr->Data, "TRK} %s", Buffer);

			C_Q_ADD(&TNC->Streams[Stream].PACTORtoBPQ_Q, buffptr);

			return;
		}

		if (TNC->MSGTYPE == 3)					// Status
		{			
			struct STREAMINFO * STREAM = &TNC->Streams[Stream];

			if (strstr(Buffer, "DISCONNECTED") || strstr(Buffer, "LINK FAILURE")  || strstr(Buffer, "BUSY"))
			{
				if ((STREAM->Connecting | STREAM->Connected) == 0)
					return;

				if (STREAM->Connecting && STREAM->Disconnecting == FALSE)
				{
					// Connect Failed
			
					buffptr = GetBuff();
					if (buffptr == 0) return;			// No buffers, so ignore

					if (strstr(Buffer, "BUSY"))
						buffptr->Len = sprintf(buffptr->Data, "*** Busy from %s\r", TNC->Streams[Stream].RemoteCall);
					else
						buffptr->Len  = sprintf(buffptr->Data, "*** Failure with %s\r", TNC->Streams[Stream].RemoteCall);

					C_Q_ADD(&STREAM->PACTORtoBPQ_Q, buffptr);
	
					STREAM->Connecting = FALSE;
					STREAM->Connected = FALSE;				// In case!
					STREAM->FramesOutstanding = 0;

					if (Stream == 0)
					{
						sprintf(TNC->WEB_TNCSTATE, "In Use by %s", STREAM->MyCall);
						SetWindowText(TNC->xIDC_TNCSTATE, TNC->WEB_TNCSTATE);
						TNC->WEB_CHANGED = TRUE;
					}

					if (TNC->RPBEACON)
						SendRPBeacon(TNC);
				
					return;
				}
					
				// Must Have been connected or disconnecting - Release Session

				STREAM->Connecting = FALSE;
				STREAM->Connected = FALSE;		// Back to Command Mode
				STREAM->FramesOutstanding = 0;

				if (STREAM->Disconnecting == FALSE)
					STREAM->ReportDISC = TRUE;		// Tell Node

				STREAM->Disconnecting = FALSE;

				if (TNC->RPBEACON)
					SendRPBeacon(TNC);

				return;
			}

			if (strstr(Buffer, "CONNECTED"))
			{
				char * Call = strstr(Buffer, " to ");
				char * ptr;
				char MHCall[30];

				Call += 4;

				if (Call[1] == ':')
					Call +=2;

				ptr = strchr(Call, ' ');	
				if (ptr) *ptr = 0;

				ptr = strchr(Call, 13);	
				if (ptr) *ptr = 0;

				STREAM->Connected = TRUE;			// Subsequent data to data channel
				STREAM->Connecting = FALSE;
				STREAM->ConnectTime = time(NULL); 
				STREAM->BytesRXed = STREAM->BytesTXed = 0;

				if (TNC->SlowTimer)
					Debugprintf("RP Incoming call to APPLCALL completed");

				TNC->SlowTimer = 0;					// Cancel Reset MYCALL timer

				//	Stop Scanner

				if (Stream == 0)
				{
					TNC->SwitchToPactor = 0;						// Cancel any RP to Pactor switch

					sprintf(Status, "%d SCANSTOP", TNC->Port);
					Rig_Command( (TRANSPORTENTRY *) -1, Status);

					memcpy(MHCall, Call, 9);
					MHCall[9] = 0;
				}

				if (TNC->PortRecord->ATTACHEDSESSIONS[Stream] == 0)
				{
					// Incoming Connect

					APPLCALLS * APPL;
					char * ApplPtr = APPLS;
					int App;
					char Appl[10];
					char DestCall[10];
					TRANSPORTENTRY * SESS;
					struct WL2KInfo * WL2K = TNC->WL2K;
					struct PORTCONTROL * PORT = &TNC->PortRecord->PORTCONTROL;
					int Totallen = 0;

					if (Stream == 0)
					{
						char Save = TNC->RIG->CurrentBandWidth;
						TNC->RIG->CurrentBandWidth = 'R';
						UpdateMH(TNC, MHCall, '+', 'I');
						TNC->RIG->CurrentBandWidth = Save;
					}

					ProcessIncommingConnect(TNC, Call, Stream, FALSE);

					// if Port CTEXT defined, use it

					if (PORT->CTEXT)
					{
						Totallen = strlen(PORT->CTEXT);
						ptr = PORT->CTEXT;
					}
					else if (HFCTEXTLEN > 0)
					{
						Totallen = HFCTEXTLEN;
						ptr = HFCTEXT;
					}
					else if (FULL_CTEXT)
					{
						Totallen = CTEXTLEN;
						ptr = CTEXTMSG;
					}

					while (Totallen)
					{
						int sendLen = TNC->PortRecord->ATTACHEDSESSIONS[Stream]->SESSPACLEN;

						if (sendLen == 0)
							sendLen = 80;

						if (Totallen < sendLen)
							sendLen = Totallen;

						buffptr = (PMSGWITHLEN)GetBuff();

						if (buffptr)
						{
							buffptr->Len = sendLen;
							memcpy(&buffptr->Data[0], ptr, sendLen);
							C_Q_ADD(&TNC->Streams[Stream].BPQtoPACTOR_Q, buffptr);
						}
						Totallen -= sendLen;
						ptr += sendLen;
					}

					SESS = TNC->PortRecord->ATTACHEDSESSIONS[Stream];

					if (TNC->RIG && TNC->RIG != &TNC->DummyRig && strcmp(TNC->RIG->RigName, "PTT"))
					{
						sprintf(TNC->WEB_TNCSTATE, "%s Connected to %s Inbound Freq %s", TNC->Streams[0].RemoteCall, TNC->Streams[0].MyCall, TNC->RIG->Valchar);
						SESS->Frequency = (int)(atof(TNC->RIG->Valchar) * 1000000.0);		// Convert to Centre Freq
						if (SESS->Frequency == 0)
						{
							// try to get from WL2K record

							if (WL2K)
								SESS->Frequency = WL2K->Freq;
						}
					}
					else
					{
						sprintf(TNC->WEB_TNCSTATE, "%s Connected to %s Inbound", TNC->Streams[0].RemoteCall, TNC->Streams[0].MyCall);
						if (WL2K)
						{
							SESS->Frequency = WL2K->Freq;
							SESS->Mode = WL2K->mode;
						}
					}
			
					if (WL2K)
					{
						strcpy(SESS->RMSCall, WL2K->RMSCall);
						SESS->Mode = WL2K->mode;
					}

					if (Stream == 0)
					{
						SetWindowText(TNC->xIDC_TNCSTATE, TNC->WEB_TNCSTATE);
						TNC->WEB_CHANGED = TRUE;
					
						// If an autoconnect APPL is defined, send it
						// See which application the connect is for

						strcpy(DestCall, STREAM->MyCall);
					
						if (TNC->UseAPPLCalls && strcmp(DestCall, TNC->NodeCall) != 0)		// Not Connect to Node Call
						{
							if (strcmp(DestCall, NodeCall) == 0)		// Call to NodeCall (when not PortCall)
							{
								goto DontUseAPPLCmd;
							}

							for (App = 0; App < 32; App++)
							{
								APPL=&APPLCALLTABLE[App];
								memcpy(Appl, APPL->APPLCALL_TEXT, 10);
								ptr=strchr(Appl, ' ');

								if (ptr)
									*ptr = 0;
	
								if (_stricmp(DestCall, Appl) == 0)
									break;
							}

							if (App < 32)
							{
								char AppName[13];

								memcpy(AppName, &ApplPtr[App * sizeof(CMDX)], 12);
								AppName[12] = 0;

								// Make sure app is available

								if (CheckAppl(TNC, AppName))
								{
									int MsgLen = sprintf(Buffer, "%s\r", AppName);
									buffptr = GetBuff();

									if (buffptr == 0) return;			// No buffers, so ignore

									buffptr->Len = MsgLen;
									memcpy(buffptr->Data, Buffer, MsgLen);

									C_Q_ADD(&STREAM->PACTORtoBPQ_Q, buffptr);
									TNC->SwallowSignon = TRUE;
								}
								else
								{
									char Msg[] = "Application not available\r\n";
					
									// Send a Message, then a disconenct
					
									buffptr = GetBuff();
									if (buffptr == 0) return;			// No buffers, so ignore

									buffptr->Len = strlen(Msg);
									memcpy(buffptr->Data, Msg, strlen(Msg));
									C_Q_ADD(&STREAM->BPQtoPACTOR_Q, buffptr);

									STREAM->NeedDisc = 100;	// 10 secs
								}
								return;
							}

							// Not to a known appl - drop through to Node
						}

				//		if (TNC->UseAPPLCalls)
				//			goto DontUseAPPLCmd;
	
						if (TNC->ApplCmd)	
						{
							buffptr = GetBuff();
							if (buffptr == 0) return;			// No buffers, so ignore

							buffptr->Len = sprintf(buffptr->Data, "%s\r", TNC->ApplCmd);
							C_Q_ADD(&TNC->Streams[Stream].PACTORtoBPQ_Q, buffptr);
							TNC->SwallowSignon = TRUE;
							return;
						}

					}	// End of Stream 0 or RP or Drop through from not APPL Connect
				
				DontUseAPPLCmd:
					return;
				}
				else
				{
					// Connect Complete
			
					buffptr = GetBuff();
					if (buffptr == 0) return;			// No buffers, so ignore

					buffptr->Len  = sprintf(buffptr->Data, "*** Connected to %s\r", Call);;

					C_Q_ADD(&STREAM->PACTORtoBPQ_Q, buffptr);

					if (Stream == 0)
					{
						if (TNC->RIG)
							sprintf(TNC->WEB_TNCSTATE, "%s Connected to %s Outbound Freq %s", STREAM->MyCall, STREAM->RemoteCall, TNC->RIG->Valchar);
						else
							sprintf(TNC->WEB_TNCSTATE, "%s Connected to %s Outbound", STREAM->MyCall, STREAM->RemoteCall);

						SetWindowText(TNC->xIDC_TNCSTATE, TNC->WEB_TNCSTATE);
						TNC->WEB_CHANGED = TRUE;

						if (STREAM->DEDStream == 30)	// Robust Mode
						{
							char Save = TNC->RIG->CurrentBandWidth;
							TNC->RIG->CurrentBandWidth = 'R';
							UpdateMH(TNC, Call, '+', 'O');
							TNC->RIG->CurrentBandWidth = Save;
						}
						else
						{
							UpdateMH(TNC, Call, '+', 'O');
						}
					}
					return;
				}
			}
			return;
		}

		if (TNC->MSGTYPE == 4 || TNC->MSGTYPE == 5)
		{

			// 4 Is header only - Null Terminated
			// 5 Is header with data following - Null Terminated

			struct STREAMINFO * STREAM = &TNC->Streams[0];		// RP Stream

			// Monitor

			if (TNC->UseAPPLCalls && strstr(&Msg[4], "SABM") && STREAM->Attached == FALSE)
			{
				// See if a call to Nodecall or one of our APPLCALLS - if so, stop scan and switch MYCALL

				char DestCall[10] = "NOCALL  ";
				char * ptr1 = strstr(&Msg[7], "to ");
				int i;
				APPLCALLS * APPL;
				char Appl[11];
				char Status[80];

				if (ptr1) memcpy(DestCall, &ptr1[3], 10);
				
				ptr1 = strchr(DestCall, ' ');
				if (ptr1) *(ptr1) = 0;					// Null Terminate

				Debugprintf("RP SABM Received for %s" , DestCall);

				if (strcmp(TNC->NodeCall, DestCall) != 0 && TNC->SlowTimer == 0)
				{
					// Not Calling NodeCall/Portcall

					if (strcmp(NodeCall, DestCall) == 0)
						goto SetThisCall;

					// See if to one of our ApplCalls

					for (i = 0; i < 32; i++)
					{
						APPL=&APPLCALLTABLE[i];

						if (APPL->APPLCALL_TEXT[0] > ' ')
						{
							char * ptr;
							memcpy(Appl, APPL->APPLCALL_TEXT, 10);
							ptr=strchr(Appl, ' ');

							if (ptr) *ptr = 0;

							if (strcmp(Appl, DestCall) == 0)
							{
						SetThisCall:

								TNC->SlowTimer = 450;	// Allow 45 seconds for connect to complete
								Debugprintf("RP SABM is for NODECALL or one of our APPLCalls - setting MYCALL to %s and pausing scan", DestCall);

								sprintf(Status, "%d SCANSTART 60", TNC->Port);	// Pause scan for 60 secs
								Rig_Command( (TRANSPORTENTRY *) -1, Status);
								
								if ((TNC->RIG == &TNC->DummyRig || TNC->RIG == NULL) && TNC->RobustTime)
									TNC->SwitchToPactor = 600;		// Don't change modes for 60 secs

								strcpy(STREAM->MyCall, DestCall);
								STREAM->CmdSet = STREAM->CmdSave = zalloc(100);
								sprintf(STREAM->CmdSet, "\1\1\1I%s", DestCall);
								break;
							}
						}
					}
				}
			}

			DoMonitorHddr(TNC, Msg, framelen, TNC->MSGTYPE);
			return;

		}

		// 1, 2, 4, 5 - pass to Appl

		if (TNC->MSGCHANNEL == 0)			// Unproto Channel
			return;

		buffptr = GetBuff();

		if (buffptr == NULL) return;			// No buffers, so ignore

		buffptr->Len = sprintf(buffptr->Data,"TRK} %s", &Msg[4]);

		C_Q_ADD(&TNC->Streams[Stream].PACTORtoBPQ_Q, buffptr);

		return;
	}

	if (TNC->MSGTYPE == 6)
	{
		// Monitor Data With length)

		DoMonitorData(TNC, Msg, framelen);
		return;
	}

	if (TNC->MSGTYPE == 7)
	{
		//char StatusMsg[60];
		//int Status, ISS, Offset;
		
		// Connected Data
		
		buffptr = GetBuff();

		if (buffptr == NULL) return;			// No buffers, so ignore
			
		buffptr->Len = framelen;				// Length
		TNC->Streams[Stream].BytesRXed += (int)buffptr->Len;
		memcpy(buffptr->Data, Msg, buffptr->Len);
		
		WritetoTrace(TNC, Msg, (int)buffptr->Len);

		C_Q_ADD(&TNC->Streams[Stream].PACTORtoBPQ_Q, buffptr);
		ShowTraffic(TNC);

		return;
	}
}


static MESSAGE Monframe[32];	// I frames come in two parts.

static MESSAGE * AdjMsg[32];	// Adjusted for digis


VOID DoMonitorHddr(struct TNCINFO * TNC, UCHAR * Msg, int Len, int Type)
{
	// Convert to ax.25 form and pass to monitor

	// Only update MH on UI, SABM, UA

	UCHAR * ptr, * starptr;
	char * context;
	int Port = TNC->Port;
	char * MHCall = Monframe[Port].ORIGIN;

	Monframe[Port].LENGTH = MSGHDDRLEN + 16;	// Control Frame
	Monframe[Port].PORT = TNC->Port;
	
	AdjMsg[Port] = &Monframe[Port];					// Adjusted fir digis
	ptr = strstr(Msg, "fm ");

	ConvToAX25(&ptr[3], Monframe[Port].ORIGIN);

//	memcpy(MHCall, &ptr[3], 11);
//	strlop(MHCall, ' ');

	ptr = strstr(ptr, "to ");

	ConvToAX25(&ptr[3], Monframe[Port].DEST);

	ptr = strstr(ptr, "via ");

	if (ptr)
	{
		// We have digis

		char Save[100];
		char * fiddle;

		memcpy(Save, &ptr[4], 60);

		ptr = strtok_s(Save, " ", &context);
DigiLoop:

		fiddle = (char *)AdjMsg[Port];
		fiddle += 7;
		AdjMsg[Port] = (MESSAGE *)fiddle;

		Monframe[Port].LENGTH += 7;

		starptr = strchr(ptr, '*');
		if (starptr)
			*(starptr) = 0;

		ConvToAX25(ptr, AdjMsg[Port]->ORIGIN);

		if (starptr)
			AdjMsg[Port]->ORIGIN[6] |= 0x80;				// Set end of address

		ptr = strtok_s(NULL, " ", &context);

		if (memcmp(ptr, "ctl", 3))
			goto DigiLoop;
	}
	AdjMsg[Port]->ORIGIN[6] |= 1;				// Set end of address

	ptr = strstr(Msg, "ctl ");

	if (memcmp(&ptr[4], "SABM", 4) == 0)
	{
		AdjMsg[Port]->CTL = 0x2f;
		if (TNC->Robust)
			UpdateMHwithDigis(TNC, MHCall, '.', 0);
		else
			UpdateMHwithDigis(TNC, MHCall, ' ', 0);
	}
	else  
	if (memcmp(&ptr[4], "DISC", 4) == 0)
		AdjMsg[Port]->CTL = 0x43;
	else 
	if (memcmp(&ptr[4], "UA", 2) == 0)
	{
		AdjMsg[Port]->CTL = 0x63;
		if (TNC->Robust)
			UpdateMHwithDigis(TNC, MHCall, '.', 0);
		else
			UpdateMHwithDigis(TNC, MHCall, ' ', 0);
	}
	else  
	if (memcmp(&ptr[4], "DM", 2) == 0)
		AdjMsg[Port]->CTL = 0x0f;
	else 
	if (memcmp(&ptr[4], "UI", 2) == 0)
	{
		AdjMsg[Port]->CTL = 0x03;
		if (TNC->Robust)
			UpdateMHwithDigis(TNC, MHCall, '.', 0);
		else
			UpdateMHwithDigis(TNC, MHCall, ' ', 0);
	}
	else 
	if (memcmp(&ptr[4], "RR", 2) == 0)
		AdjMsg[Port]->CTL = 0x1 | (ptr[6] << 5);
	else 
	if (memcmp(&ptr[4], "RNR", 3) == 0)
		AdjMsg[Port]->CTL = 0x5 | (ptr[7] << 5);
	else 
	if (memcmp(&ptr[4], "REJ", 3) == 0)
		AdjMsg[Port]->CTL = 0x9 | (ptr[7] << 5);
	else 
	if (memcmp(&ptr[4], "FRMR", 4) == 0)
		AdjMsg[Port]->CTL = 0x87;
	else  
	if (ptr[4] == 'I')
	{
		AdjMsg[Port]->CTL = (ptr[5] << 5) | (ptr[6] & 7) << 1 ;
	}

	if (strchr(&ptr[4], '+'))
	{
		AdjMsg[Port]->CTL |= 0x10;
		Monframe[Port].DEST[6] |= 0x80;				// SET COMMAND
	}

	if (strchr(&ptr[4], '-'))	
	{
		AdjMsg[Port]->CTL |= 0x10;
		Monframe[Port].ORIGIN[6] |= 0x80;				// SET COMMAND
	}

	if (Type == 5)								// More to come
	{
		ptr = strstr(ptr, "pid ");	
		sscanf(&ptr[3], "%x", (UINT *)&AdjMsg[Port]->PID);
		return;	
	}

	time(&Monframe[Port].Timestamp);

	BPQTRACE((MESSAGE *)&Monframe[Port], TRUE);
}

VOID DoMonitorData(struct TNCINFO * TNC, UCHAR * Msg, int Len)
{
	int Port = TNC->Port;

	// // Second part of I or UI

	if (AdjMsg[Port])
	{
		memcpy(AdjMsg[Port]->L2DATA, Msg, Len);
		Monframe[Port].LENGTH += Len;

		time(&Monframe[Port].Timestamp);

		BPQTRACE((MESSAGE *)&Monframe[Port], TRUE);
	}
	return;
}


//1:fm G8BPQ to KD6PGI-1 ctl I11^ pid F0
//fm KD6PGI-1 to G8BPQ ctl DISC+

VOID TrkTidyClose(struct TNCINFO * TNC, int Stream)
{
	// Queue it as we may have just sent data

	TNC->Streams[Stream].CmdSet = TNC->Streams[Stream].CmdSave = zalloc(100);
	sprintf(TNC->Streams[Stream].CmdSet, "\1\1\1D");
}


VOID TrkForcedClose(struct TNCINFO * TNC, int Stream)
{
	TrkTidyClose(TNC, Stream);			// I don't think Hostmode has a DD
}

VOID TrkCloseComplete(struct TNCINFO * TNC, int Stream)
{
	char Status[80];

	TNC->NeedPACTOR = 20;		// Delay a bit for UA to be sent before changing mode and call
	
	sprintf(Status, "%d SCANSTART 15", TNC->Port);

	strcpy(TNC->WEB_TNCSTATE, "Idle");
	SetWindowText(TNC->xIDC_TNCSTATE, TNC->WEB_TNCSTATE);
	TNC->WEB_CHANGED = TRUE;

	
	Rig_Command( (TRANSPORTENTRY *) -1, Status);

	if (TNC->RIG == &TNC->DummyRig)		// Not using Rig control
		TNC->SwitchToPactor = TNC->RobustTime;

	ReleaseOtherPorts(TNC);
}

VOID SwitchToRPacket(struct TNCINFO * TNC, char * Baud)
{
	if (TNC->Robust == FALSE)
	{
		TNC->Streams[0].CmdSet = TNC->Streams[0].CmdSave = zalloc(100);
		sprintf(TNC->Streams[0].CmdSet, "\1\1\1%%B %s", Baud);
		TNC->Robust = TRUE;
		SetWindowText(TNC->xIDC_MODE, "Robust Packet");
		strcpy(TNC->WEB_MODE, "Robust Packet");
		TNC->WEB_CHANGED = TRUE;
	}
}
VOID SwitchToNormPacket(struct TNCINFO * TNC, char * Baud)
{
	if (TNC->ForceRobust)
		return;
	
	TNC->Streams[0].CmdSet = TNC->Streams[0].CmdSave = zalloc(100);
		
	if (Baud[0] == 0)
		sprintf(TNC->Streams[0].CmdSet, "\1\1\1%%B %s", TNC->NormSpeed);
	else
		sprintf(TNC->Streams[0].CmdSet, "\1\1\1%%B %s", Baud);
			
	TNC->Robust = FALSE;

	SetWindowText(TNC->xIDC_MODE, "HF Packet");
	strcpy(TNC->WEB_MODE, "HF Packet");
	TNC->WEB_CHANGED = TRUE;
}

VOID SendRPBeacon(struct TNCINFO * TNC)
{
	MESSAGE AXMSG;
	PMESSAGE AXPTR = &AXMSG;
	char BEACONMSG[80];

	int DataLen = sprintf(BEACONMSG, "QRA %s %s", TNC->NodeCall, LOC);

	// Block includes the Msg Header (7 bytes), Len Does not!

	ConvToAX25("BEACON", AXPTR->DEST);
	ConvToAX25(TNC->NodeCall, AXPTR->ORIGIN);

	AXPTR->DEST[6] &= 0x7e;			// Clear End of Call
	AXPTR->DEST[6] |= 0x80;			// set Command Bit

	AXPTR->ORIGIN[6] |= 1;			// Set End of Call
	AXPTR->CTL = 3;					//UI
	AXPTR->PID = 0xf0;
	memcpy(AXPTR->L2DATA, BEACONMSG, DataLen);

	Send_AX(&AXMSG, DataLen + 16, TNC->Port);
	return;

}



