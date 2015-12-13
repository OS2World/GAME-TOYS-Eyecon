/***************************************************************************\
* eyecon.c - Pointer tracking program
\***************************************************************************/

#define MAXEYECONS 25
#define VIEWPOINT 25

#define ID_DUPLICATE 256
#define ID_CLOSEALL 257
#define ID_SAVEPOSITION 258
#define ID_TITLE 259
#define ID_SQUARE 260
#define ID_BLOODSHOT 261
#define ID_ABOUT 262

#define INCL_DOSPROCESS
#define INCL_GPIPATHS
#define INCL_GPIPRIMITIVES
#define INCL_WINFRAMEMGR
#define INCL_WININPUT
#define INCL_WINMENUS
#define INCL_WINPOINTERS
#define INCL_WINSHELLDATA
#define INCL_WINSYS
#define INCL_WINWINDOWMGR

#include <os2.h>

typedef struct {
	SHORT x,y;
	unsigned int enabled : 1;
	unsigned int notitle : 1;
	unsigned int notsquare : 1;
	unsigned int eyetype : 13;
} PROFILEREC;

typedef struct {
	SHORT position;
	USHORT style;
	USHORT id;
	char *text;
} MENUMODIFY;

long far * PASCAL FAR InstallEyeHook(HAB, HWND far *, BOOL);
VOID PASCAL FAR ReleaseEyeHook(void);
void cdecl main(int, char **);
MRESULT EXPENTRY _export eyeconproc(HWND, USHORT, MPARAM, MPARAM);
MRESULT EXPENTRY _export eyeframeproc(HWND, USHORT, MPARAM, MPARAM);
static void far eyedaemon(void);
static long sqr(long);
static void makewin(int, HWND, SHORT, SHORT);
void cdecl _setenvp(void);

static HAB hab;
static TID threadid;
static PFNWP pfnwp[MAXEYECONS];
static HWND title[MAXEYECONS];
static HDC volatile hdc[MAXEYECONS];
static HPS volatile hps[MAXEYECONS];
static HBITMAP volatile eyemap[MAXEYECONS];
static HWND volatile frame[MAXEYECONS];
static BOOL volatile repaint[MAXEYECONS];
static BOOL volatile nopaint[MAXEYECONS];
static PROFILEREC volatile profile[MAXEYECONS];
static long volatile quitsem = 0;
static long stopsem = 0;
static long far *mousesem = NULL;
static char name[] = "Eyecon";
static char key[] = "Positions";
static ULONG fcfval = FCF_TITLEBAR|FCF_SYSMENU;

void cdecl main(argc,argv)
int argc;
char **argv;
{
	USHORT i;
	HMQ hmsgq;
	HWND client;
	QMSG qmsg;
	char stack[3072];

	hab = WinInitialize(0);
	hmsgq = WinCreateMsgQueue(hab,DEFAULT_QUEUE_SIZE);
	WinRegisterClass(hab,name,eyeconproc,0L,2);
	if (argc > 1 && argv[1][0] == '/' && argv[1][1] == 'C')
		WinWriteProfileData(hab,name,NULL,NULL,0);
	profile[0].x = -1;
	profile[0].enabled = TRUE;
	profile[0].notitle = FALSE;
	profile[0].notsquare = FALSE;
	profile[0].eyetype = 0;
	for (i = 1; i < MAXEYECONS; i++) profile[i].enabled = FALSE;
	i = sizeof(profile);
	WinQueryProfileData(hab,name,key,(PBYTE)profile,&i);
	for (i = 0; i < MAXEYECONS; i++) if (profile[i].enabled) {
		frame[i] = WinCreateStdWindow(HWND_DESKTOP,0L,&fcfval,
			name,name,0L,0,1,&client);
		if (!mousesem && !(mousesem = InstallEyeHook(hab,&client,FALSE))) {
			WinDestroyWindow(frame[i]);
			WinPostMsg(client,WM_USER,MPFROM2SHORT(FALSE,FALSE),MPFROMSHORT(0));
			goto abort;
		}
		makewin(i,client,profile[i].x,profile[i].y);
	}			
	DosCreateThread(eyedaemon,&threadid,stack+3070);
	DosSetPrty(PRTYS_THREAD,PRTYC_IDLETIME,PRTYD_MAXIMUM,threadid);
	while(WinGetMsg(hab,&qmsg,NULL,0,0)) WinDispatchMsg(hab,&qmsg);
	ReleaseEyeHook();
	DosSemSet((HSEM)&quitsem);
	DosSemClear(mousesem);
	DosSetPrty(PRTYS_THREAD,PRTYC_REGULAR,0,threadid);
	DosSemWait((HSEM)&quitsem,SEM_INDEFINITE_WAIT);
	for (i = 0; i < MAXEYECONS; i++) if (profile[i].enabled) {
		if (hps[i]) {
			GpiDeleteBitmap(eyemap[i]);
			GpiDestroyPS(hps[i]);
		}
		if (profile[i].notitle) WinSetParent(title[i],frame[i],FALSE);
		WinDestroyWindow(frame[i]);
	}
abort:
	WinDestroyMsgQueue(hmsgq);
	WinTerminate(hab);
}

MRESULT EXPENTRY _export eyeconproc(hwnd,message,mp1,mp2)
HWND hwnd;
USHORT message;
MPARAM mp1,mp2;
{
	int i,who;
	HWND client;
	RECTL rectup;

	who = WinQueryWindowUShort(hwnd,0);
	switch (message) {
	case WM_PAINT:
		WinQueryUpdateRect(hwnd,&rectup);
		WinValidateRect(hwnd,&rectup,FALSE);
		if (!nopaint[who]) {
			repaint[who] = TRUE;
			if (profile[who].notsquare) WinShowWindow(frame[who],FALSE);
			DosSemClear(mousesem);
		}
		else nopaint[who] = FALSE;
		return FALSE;
	case WM_USER:
		for (i = 0; i < MAXEYECONS && profile[i].enabled; i++);
		if (i == MAXEYECONS) return FALSE;
		profile[i].notitle = SHORT1FROMMP(mp1);
		profile[i].notsquare = SHORT2FROMMP(mp1);
		profile[i].eyetype = SHORT1FROMMP(mp2);
		frame[i] = WinCreateStdWindow(HWND_DESKTOP,0L,&fcfval,
			name,name,0L,0,1,&client);
		makewin(i,client,-1,-1);
		profile[i].enabled = TRUE;
		return MRFROMSHORT(TRUE);
	case WM_CLOSE:
		for (i = 0; i < MAXEYECONS && (i == who || !profile[i].enabled); i++);
		if (i != MAXEYECONS) {
			client = WinWindowFromID(frame[i],FID_CLIENT);
			InstallEyeHook(hab,&client,TRUE);
			DosSetPrty(PRTYS_THREAD,PRTYC_REGULAR,0,threadid);
			DosSemRequest(&stopsem,SEM_INDEFINITE_WAIT);
			if (hps[who]) {
				GpiDeleteBitmap(eyemap[who]);
				GpiDestroyPS(hps[who]);
			}
			if (profile[who].notitle) WinSetParent(title[who],frame[who],FALSE);
			WinDestroyWindow(frame[who]);
			profile[who].enabled = FALSE;
			DosSetPrty(PRTYS_THREAD,PRTYC_IDLETIME,PRTYD_MAXIMUM,threadid);
			DosSemClear(&stopsem);
			return FALSE;
		}
	}
	return WinDefWindowProc(hwnd,message,mp1,mp2);
}

MRESULT EXPENTRY _export eyeframeproc(hwnd,message,mp1,mp2)
HWND hwnd;
USHORT message;
MPARAM mp1,mp2;

{
	int i,j;
	USHORT checked;
	HWND client,sysmenu;
	POINTL pnt;

	client = WinWindowFromID(hwnd,FID_CLIENT);
	i = WinQueryWindowUShort(client,0);
	switch (message) {
	case WM_BUTTON1DBLCLK:
		WinPostMsg(client,WM_USER,
			MPFROM2SHORT(profile[i].notitle,profile[i].notsquare),
			MPFROMSHORT(profile[i].eyetype));
		return MRFROMSHORT(TRUE);
	case WM_SEM1:
		WinShowWindow(hwnd,TRUE);
		return FALSE;
	case WM_SYSCOMMAND:
		switch (SHORT1FROMMP(mp1)) {
		case ID_DUPLICATE:
			WinPostMsg(client,WM_USER,
				MPFROM2SHORT(profile[i].notitle,profile[i].notsquare),
				MPFROMSHORT(profile[i].eyetype));
			return FALSE;
		case ID_CLOSEALL:
			WinPostMsg(client,WM_QUIT,0,0);
			return FALSE;
		case ID_SAVEPOSITION:
			for (j = 0; j < MAXEYECONS; j++) if (profile[j].enabled) {
				pnt.x = pnt.y = 0;
				WinMapWindowPoints(frame[j],HWND_DESKTOP,&pnt,1);
				profile[j].x = (SHORT)pnt.x;
				profile[j].y = (SHORT)pnt.y;
			}
			WinWriteProfileData(hab,name,key,(PBYTE)profile,sizeof(profile));
			return FALSE;
		case ID_ABOUT:
			WinMessageBox(HWND_DESKTOP,client,"An unbelievably silly "
				"pointer tracking program.\n\nInspired by the Mac version.\n\n"
				"By John Ridges","EYECON",0,
				MB_OK|MB_NOICON|MB_DEFBUTTON1|MB_APPLMODAL);
			return FALSE;
		default:
			if (SHORT1FROMMP(mp1) >= ID_TITLE && SHORT1FROMMP(mp1) <= ID_BLOODSHOT) {
				sysmenu = WinWindowFromID(frame[i],FID_SYSMENU);
				checked = SHORT1FROMMR(WinSendMsg(sysmenu,MM_QUERYITEMATTR,
					MPFROM2SHORT(SHORT1FROMMP(mp1),TRUE),MPFROMSHORT(MIA_CHECKED)));
				checked ^= MIA_CHECKED;
				WinSendMsg(sysmenu,MM_SETITEMATTR,MPFROM2SHORT(SHORT1FROMMP(mp1),TRUE),
					MPFROM2SHORT(MIA_CHECKED,checked));
				DosSetPrty(PRTYS_THREAD,PRTYC_REGULAR,0,threadid);
				DosSemRequest(&stopsem,SEM_INDEFINITE_WAIT);
				switch (SHORT1FROMMP(mp1)) {
				case ID_TITLE:
					profile[i].notitle = checked != MIA_CHECKED;
					WinSetParent(title[i],checked != MIA_CHECKED ? HWND_OBJECT :
						frame[i],FALSE);
					WinSendMsg(frame[i],WM_UPDATEFRAME,MPFROMSHORT(FCF_TITLEBAR),0);
					WinSetFocus(HWND_DESKTOP,NULL);
					WinSetFocus(HWND_DESKTOP,frame[i]);
					break;
				case ID_SQUARE:
					profile[i].notsquare = checked != MIA_CHECKED;
					nopaint[i] = FALSE;
					WinInvalidateRect(client,NULL,FALSE);
					break;
				case ID_BLOODSHOT:
					profile[i].eyetype = checked == MIA_CHECKED;
					if (hps[i]) {
						GpiDeleteBitmap(eyemap[i]);
						GpiDestroyPS(hps[i]);
						hps[i] = NULL;
					}
					repaint[i] = TRUE;
				}
				DosSetPrty(PRTYS_THREAD,PRTYC_IDLETIME,PRTYD_MAXIMUM,threadid);
				DosSemClear(&stopsem);
				DosSemClear(&mousesem);
				return FALSE;
			}
		}
		break;
	case WM_BUTTON1DOWN:
		if (profile[i].notsquare) nopaint[i] = TRUE;
	}
	return (pfnwp[i])(hwnd,message,mp1,mp2);
}

static void far eyedaemon()
{
	int i,j;
	long x,y,d;
	HAB thab;
	HWND client;
	RECTL rectup;
	POINTL pnt,rpnt,aptl[4];
	static SIZEL sizl = { 0, 0 };
	static ARCPARAMS arc = { 7, 15, 0, 0 };

	thab = WinInitialize(0);
	aptl[0].y = 0;
	aptl[1].y = 31;
	while (!quitsem) {
		DosSemWait(mousesem,SEM_INDEFINITE_WAIT);
		DosSemRequest(&stopsem,SEM_INDEFINITE_WAIT);
		DosSemSet(mousesem);
		WinQueryPointerPos(HWND_DESKTOP,&pnt);
		for (j = 0; j < MAXEYECONS; j++) if (profile[j].enabled) {
			client = WinWindowFromID(frame[j],FID_CLIENT);
			if (!hps[j]) {
				hps[j] = GpiCreatePS(thab,hdc[j],&sizl,
					PU_PELS|GPIF_DEFAULT|GPIT_MICRO|GPIA_ASSOC);
				eyemap[j] = GpiLoadBitmap(hps[j],0,profile[j].eyetype+1,0L,0L);
				GpiSetArcParams(hps[j],&arc);
			}
			if (repaint[j]) {
				GpiSetClipPath(hps[j],0L,SCP_RESET);
				if (!profile[j].notsquare) {
					WinQueryWindowRect(client,&rectup);
					WinFillRect(hps[j],&rectup,SYSCLR_BACKGROUND);
				}
				else {
					nopaint[j] = TRUE;
					WinPostMsg(frame[j],WM_SEM1,0,0);
				}
				GpiBeginPath(hps[j],1L);
				rpnt.y = 15;
				for (i = 7; i < 39; i += 16) {
					rpnt.x = i;
					GpiMove(hps[j],&rpnt);
					GpiFullArc(hps[j],DRO_OUTLINE,MAKEFIXED(1,0));
				}
				GpiEndPath(hps[j]);
				GpiSetClipPath(hps[j],1L,SCP_ALTERNATE|SCP_AND);
				repaint[j] = FALSE;
			}
			rpnt.x = rpnt.y = 0;
			WinMapWindowPoints(client,HWND_DESKTOP,&rpnt,1);
			for (i = 0; i < 32; i += 16) {
				x = rpnt.x+7+i-pnt.x;
				y = rpnt.y+15-pnt.y;
				d = sqr(VIEWPOINT*VIEWPOINT+x*x+y*y);
				x = (x*7+(d>>1))/d;
				y = (y*15+(d>>1))/d;
				aptl[0].x = i;
				aptl[1].x = i+15;
				aptl[2].x = x+8;
				aptl[2].y = y+16;
				aptl[3].x = x+24;
				aptl[3].y = y+48;
				GpiWCBitBlt(hps[j],eyemap[j],4L,aptl,ROP_SRCCOPY,BBO_IGNORE);
			}
		}
		DosSemClear(&stopsem);
	}
	WinTerminate(thab);
	DosEnterCritSec();
	DosSemClear((HSEM)&quitsem);
}

static long sqr(arg)
long arg;
{
	long sqrt,temp,range;

	sqrt = 0;
	temp = 1L<<15;
	for (range = 1L<<30; range > arg; range >>= 2) temp >>= 1;
	while (temp) {
		sqrt ^= temp;
		if (sqrt*sqrt > arg) sqrt ^= temp;
		temp >>= 1;
	}
	return sqrt;
}

static void makewin(i,client,x,y)
int i;
HWND client;
SHORT x,y;
{
	int id,j;
	HWND sysmenu;
	MENUITEM mi,submenu;
	static USHORT deleted[] = { SC_SIZE, SC_MINIMIZE, SC_MAXIMIZE, SC_RESTORE };
	static MENUMODIFY menu[] = {
		{ 0, MIS_SYSCOMMAND|MIS_TEXT, ID_CLOSEALL, "Close ~All" },
		{ 0, MIS_SYSCOMMAND|MIS_TEXT, ID_DUPLICATE, "~Duplicate" },
		{ MIT_END, MIS_SEPARATOR, 0, "" },
		{ MIT_END, MIS_SYSCOMMAND|MIS_TEXT, ID_SAVEPOSITION, "Save ~Positions" },
		{ MIT_END, MIS_SEPARATOR, 1, "" },
		{ MIT_END, MIS_SYSCOMMAND|MIS_TEXT, ID_TITLE, "~Icon Title" },
		{ MIT_END, MIS_SYSCOMMAND|MIS_TEXT, ID_SQUARE, "Square ~Window" },
		{ MIT_END, MIS_SYSCOMMAND|MIS_TEXT, ID_BLOODSHOT, "~Bloodshot Eyes" },
		{ MIT_END, MIS_SEPARATOR, 2, "" },
		{ MIT_END, MIS_SYSCOMMAND|MIS_TEXT, ID_ABOUT, "About Eyecon..." }
	};

	hps[i] = NULL;
	repaint[i] = nopaint[i] = FALSE;
	WinSetWindowUShort(client,0,i);
	pfnwp[i] = WinSubclassWindow(frame[i],eyeframeproc);
	WinSetWindowPos(frame[i],NULL,0,0,0,0,SWP_MINIMIZE);
	sysmenu = WinWindowFromID(frame[i],FID_SYSMENU);
	id = SHORT1FROMMR(WinSendMsg(sysmenu,MM_ITEMIDFROMPOSITION,0,0));
	WinSendMsg(sysmenu,MM_QUERYITEM,MPFROM2SHORT(id,FALSE),MPFROMP(&submenu));
	for (j = 0; j < sizeof(deleted)/sizeof(USHORT); j++)
		WinSendMsg(submenu.hwndSubMenu,MM_DELETEITEM,
			MPFROM2SHORT(deleted[j],FALSE),0);
	menu[0].position = 1+SHORT1FROMMR(WinSendMsg(submenu.hwndSubMenu,MM_ITEMPOSITIONFROMID,
		MPFROM2SHORT(SC_CLOSE,FALSE),0));
	mi.hwndSubMenu = NULL;
	mi.hItem = 0;
	for (j = 0; j < sizeof(menu)/sizeof(MENUMODIFY); j++) {
		mi.iPosition = menu[j].position;
		mi.afStyle = menu[j].style;
		mi.id = menu[j].id;
		if ((menu[j].id == ID_TITLE && !profile[i].notitle) ||
			(menu[j].id == ID_SQUARE && !profile[i].notsquare) ||
			(menu[j].id == ID_BLOODSHOT && profile[i].eyetype))
			mi.afAttribute = MIA_CHECKED;
		else mi.afAttribute = 0;
		WinSendMsg(submenu.hwndSubMenu,MM_INSERTITEM,MPFROMP(&mi),
			MPFROMP(menu[j].text));
	}
	if (x >= 0) WinSetWindowPos(frame[i],NULL,x,y,0,0,SWP_MOVE);
	title[i] = WinWindowFromID(frame[i],FID_TITLEBAR);
	if (profile[i].notitle) {
		WinSetParent(title[i],HWND_OBJECT,FALSE);
		WinSendMsg(frame[i],WM_UPDATEFRAME,MPFROMSHORT(FCF_TITLEBAR),0);
	}
	WinSetWindowPos(frame[i],NULL,0,0,0,0,SWP_SHOW);
	hdc[i] = WinOpenWindowDC(client);
}

void cdecl _setenvp()
{
}
