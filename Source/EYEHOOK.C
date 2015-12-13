/***********************************************************************\
* eyehook.c - Mouse hook library for EYECON
\***********************************************************************/

#define INCL_DOSPROCESS
#define INCL_WINHOOKS
#define INCL_WININPUT

#include <os2.h>

VOID PASCAL FAR _loadds QJournalRecordHook(HAB, PQMSG);
long far * PASCAL FAR _loadds InstallEyeHook(HAB, HWND far *, BOOL);
VOID PASCAL FAR _loadds ReleaseEyeHook(void);
VOID PASCAL near _loadds Initdll(HMODULE);

static HMODULE qhmod;
static HWND volatile hwnd;
static long mousesem = 0;
static HAB volatile globalhab = NULL;

VOID PASCAL FAR _loadds QJournalRecordHook(hab,lpqmsg)
HAB hab;
PQMSG lpqmsg;
{
	hab;
	if (lpqmsg->msg == WM_MOUSEMOVE) DosSemClear(&mousesem);
}

long far * PASCAL FAR _loadds InstallEyeHook(hab,phwnd,reassign)
HAB hab;
HWND far *phwnd;
BOOL reassign;
{
	HAB temphab;

	DosEnterCritSec();
	temphab = globalhab;
	if (reassign) hwnd = *phwnd;
	else if (!globalhab) globalhab = hab;
	else *phwnd = hwnd;
	DosExitCritSec();
	if (temphab) return NULL;
	hwnd = *phwnd;
	WinSetHook(hab,NULL,HK_JOURNALRECORD,(PFN)QJournalRecordHook,qhmod);
	return &mousesem;
}

VOID PASCAL FAR _loadds ReleaseEyeHook()
{
	WinReleaseHook(globalhab,NULL,HK_JOURNALRECORD,
		(PFN)QJournalRecordHook,qhmod);
	globalhab = NULL;
}


VOID PASCAL near _loadds Initdll(hmod)
HMODULE hmod;
{
	qhmod = hmod;
}
