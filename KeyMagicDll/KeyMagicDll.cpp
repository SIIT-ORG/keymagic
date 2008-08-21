//The head of Keymagic application.
//Copyright (C) 2008  KeyMagic Project
//http://keymagic.googlecode.com
//
//This program is free software; you can redistribute it and/or modify
//it under the terms of the GNU General Public License as published by
//the Free Software Foundation.
//
//This program is distributed in the hope that it will be useful,
//but WITHOUT ANY WARRANTY; without even the implied warranty of
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//GNU General Public License for more details.
//
//You should have received a copy of the GNU General Public License
//along with this program; if not, write to the Free Software
//Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

#include <Userenv.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <stdio.h>

#include "../KeyMagic/KeyMagic.h"
#include "KeyMagicDll.h"
#include "regex.h"

HANDLE	hFile;
LPVOID	FilePtr;
HANDLE	hFileMap;

#pragma data_seg(".keymagic")
HHOOK hKeyHook = NULL;
HHOOK hWndProcHook = NULL;
HHOOK hGetMsgHook = NULL;
HWND Commander_hWnd = NULL;
char szDir[1000] = {0};
#pragma data_seg()

//Make sure that section can READ WRITE and SHARE
#pragma comment(linker, "/SECTION:.keymagic,RWS")

KbFileHeader *FileHeader;
One2One	Single_Input;
File_One2Multi *Multi_Output;
File_Custom *Custom_Patterns;
File_Delete *Back_Patterns;
KM_ShortCut *SC;

bool	isActive = false;
UINT	NumOfShortCut = 0;
UINT	ActiveIndex = 0;

bool	OpenForMapping(LPCSTR FileName);
bool	TranslateToAscii (UINT *uVKey);
void	SendStrokes (wchar_t* Strokes, int len);
LPCSTR	GetKeyBoard(UINT Index);
int		ShortCutCheck (UINT uvKey);
void	Logger(char* fmt, ...);
bool	Do_Operation(const wchar_t user_input);
wchar_t	Match_One2One(const wchar_t user_input);
bool	Customize(const wchar_t *Input_Unicode, int length);
wchar_t *Match_One2Multi(const wchar_t user_input, _Out_ int *length);
void	kmBack(int count);
bool	BackCustomize();
int		inner_back=0;

char Debug[1000];
int DebugLoc=0;

int ShortCutCheck (UINT uvKey){
//	Logger("ShortCutCheck Entry");
	bool isCTRL, isALT, isSHIFT;

	BYTE KeyStatus[256];
	GetKeyboardState(KeyStatus);

	isCTRL = KeyStatus[VK_CONTROL] & 0x80;
	isALT = KeyStatus[VK_MENU] & 0x80;
	isSHIFT = KeyStatus[VK_SHIFT] & 0x80;

//	Logger("ShortCutCheck StartChecking");
	for (int i=0; i < NumOfShortCut; i++){
//		Logger("ShortCutCheck : Not Found Yet i = %X",i);
		if (uvKey != SC[i].ukey){
			continue;
		}

		if (SC[i].modkey & HOTKEYF_SHIFT){
			if (isSHIFT == false)
				continue;
		}

		if (SC[i].modkey & HOTKEYF_CONTROL){
			if (isCTRL == false)
				continue;
		}

		if (SC[i].modkey & HOTKEYF_ALT){
			if (isALT == false)
				continue;
		}

		return i;
	}
//	Logger("ShortCutCheck : return -1");

	return -1;
}

LRESULT KEYMAGICDLL_API CALLBACK HookKeyProc(int nCode, WPARAM wParam, LPARAM lParam)
{

	if (nCode < 0){
		return CallNextHookEx(hKeyHook, nCode, wParam, lParam);
	}

	if (nCode == HC_ACTION && lParam & 0x80000000){
		return CallNextHookEx(hKeyHook, nCode, wParam, lParam);
	}

	else if (nCode == HC_ACTION && lParam){
		if (!GetFocus())
			return CallNextHookEx(hKeyHook, nCode, wParam, lParam);

		int index = ShortCutCheck(wParam);
		if (index >= 0){
			if ( (ActiveIndex == index && isActive) || index == 0){
				isActive = false;
				PostMessage(Commander_hWnd, KM_GETFOCUS, 0, 0);
			}
			else{
				OpenKbFile(index);
				PostMessage(Commander_hWnd, KM_GETFOCUS, index, 0);
			}
			return 1;
		}

		if (isActive == false)
			return CallNextHookEx(hKeyHook, nCode, wParam, lParam);

		wchar_t Input = wParam;

		if (wParam == VK_BACK){
			if (inner_back < 1){
				Logger("UnInnerBack wParam = %X lParam %X", wParam, lParam);
				if (BackCustomize())
					return 1;
				else {
					return CallNextHookEx(hKeyHook, nCode, wParam, lParam);
				}
			}

			else if (inner_back){
				Logger("InnerBack wParam = %X lParam %X", wParam, lParam);
				inner_back--;
			}
		}

		if (!TranslateToAscii((UINT*)&Input))
			return CallNextHookEx(hKeyHook, nCode, wParam, lParam);

		if (Do_Operation(Input))
			return true;
	}

	return CallNextHookEx(hKeyHook, nCode, wParam, lParam);

}

bool Do_Operation(const wchar_t user_input){
	wchar_t OneOutput;
	wchar_t *MultiOutput;

	OneOutput = Match_One2One(user_input);
	int multi_len;
	MultiOutput = Match_One2Multi(user_input, &multi_len);

	if (OneOutput)
		if (!Customize(&OneOutput, 1)){
//			Logger("OneOutput len = %d", 1);
			SendStrokes(&OneOutput,1);
			return true;
		}
		else {return true;}

	else if (MultiOutput)
		if (!Customize(MultiOutput, multi_len)){
//			Logger("MultiOutput len = %d", multi_len);
			SendStrokes(MultiOutput, multi_len);
			return true;
		}
		else {return true;}

	return false;
}

void kmBack(int count){

	if (!GetFocus())
		return ;

	inner_back = --count;
	
	for(int i=0; i < count; i++){
//		Logger("kmBack");
		keybd_event(VK_BACK, 255, 0, 0);
		keybd_event(VK_BACK, 2, KEYEVENTF_KEYUP, 0);
	}

}

bool BackCustomize(){

	HWND hEdit = GetFocus();
	int kmLength = GetWindowTextLengthW(hEdit);
	wchar_t *kmInputs = new wchar_t[kmLength * sizeof(wchar_t)];
	int start;
	SendMessage(hEdit, EM_GETSEL, (WPARAM) &start, NULL);
	GetWindowTextW(hEdit, kmInputs, kmLength * sizeof(wchar_t));

	if (kmLength < 1)
		return false;

	for (int i=0, length=0; i < FileHeader->Back_Count; i++){
		short MP_length = (short)*((LPBYTE)&Back_Patterns->size_MatchPattern + length);
		wchar_t *MP = (wchar_t*)((LPBYTE)&Back_Patterns->size_MatchPattern + sizeof(short) + length);
		short OP_length = (short)*( (LPBYTE) (MP + MP_length));
		wchar_t *OP = (wchar_t*) ((LPBYTE) ( MP + MP_length )+ sizeof(short)); 

		wchar_t *temp_MP = new wchar_t[MP_length+1];

		temp_MP[MP_length] = NULL;
		wcsncpy(temp_MP, MP, MP_length);

//		Logger("BackCustomize");

		wchar_t* found = wcsstr(&kmInputs[kmLength - MP_length], temp_MP);

		if (found)
		{
			kmBack(++MP_length);
			SendStrokes(OP, OP_length);
			return true;
		}

		length += (MP_length + OP_length + sizeof(short) ) *2 ;
	}

	return false;
}

void SendStrokes (wchar_t* Strokes, int len)//Send Keys Strokes
{
	HWND hwnd = GetFocus();
	int i;

	INPUT ip;
	ip.type = INPUT_KEYBOARD;
	ip.ki.dwExtraInfo = 0;
	ip.ki.dwFlags = KEYEVENTF_UNICODE;
	ip.ki.wVk = 0;
	ip.ki.time = 0;

//	Logger("SendStrokes len = %d", len);

	for(i=0; i < len; i++){
		if (!Strokes[i])
			break;
		ip.ki.wScan = Strokes[i];
		SendInput(1, &ip, sizeof(INPUT));
	}

}

bool Customize(const wchar_t *Input_Unicode, int input_length){

	HWND hEdit = GetFocus();
	int kmLength = GetWindowTextLengthW(hEdit);
	wchar_t *kmInputs = new wchar_t[kmLength * sizeof(wchar_t)];

	int start;
	SendMessage(hEdit, EM_GETSEL, (WPARAM) &start, NULL);
	GetWindowTextW(hEdit, kmInputs, kmLength * sizeof(wchar_t));

	if (kmLength < 1)
		return false;

	UINT length = 0;

	wcsncpy(&kmInputs[kmLength], Input_Unicode, input_length);
	kmInputs[kmLength+input_length] = NULL;

	for (int i=0; i < FileHeader->Customize_Count; i++){
//		Logger("length %x", length);
		short MP_length = (short)*((LPBYTE)&Custom_Patterns->size_MatchPattern + length);
//		Logger("MP_length %x", MP_length);
		wchar_t *MP = (wchar_t*)((LPBYTE)&Custom_Patterns->size_MatchPattern + sizeof(short) + length);
//		Logger("MP %x", MP );
		short OP_length = (short)*( (LPBYTE) (MP + MP_length));
//		Logger("OP_length %x", OP_length );
		wchar_t *OP = (wchar_t*) ((LPBYTE) ( MP + MP_length )+ sizeof(short)); 
//		Logger("OP %x", OP );

		wchar_t *temp_MP = new wchar_t[MP_length+1];
		
		temp_MP[MP_length] = NULL;
		wcsncpy(temp_MP, MP, MP_length);

		wchar_t* found = wcsstr(&kmInputs[input_length + kmLength - MP_length], temp_MP);

		if (found)
		{
//			Logger("Customize : FOUND");
			kmBack(MP_length);
			SendStrokes(OP, OP_length);
			return true;
		}

		length += ( MP_length + OP_length + sizeof(short) ) * 2;
		delete temp_MP;
	}
	
	return false;
};

wchar_t *Match_One2Multi(const wchar_t user_input,_Out_ int *length){
	bool isCTRL, isLALT, isRALT, isSHIFT;
	BYTE KeyState[256];
//	Logger("Processing GetKeyboardState");
	GetKeyboardState(KeyState);

	isCTRL = KeyState[VK_CONTROL] & 0x80;
	isLALT = KeyState[VK_LMENU] & 0x80;
	isRALT = KeyState[VK_RMENU] & 0x80;
	isSHIFT = KeyState[VK_SHIFT] & 0x80;

	File_One2Multi *O2M;
	O2M = Multi_Output;

	for (int i=0,next_loc=0; i < FileHeader->Multi_Count; i++)
	{
		Internal_One2Multi *multi;
		next_loc = O2M->size;
		multi = &O2M->One2Multi;

		if (multi->Input_Key != user_input){
			goto Next;
		}

		if (wcsstr((const wchar_t*)L"~!@#$%^&*()_+|}{\":?><QWERTYUIOPASDFGHJKLZXCVBNM", &user_input))
			isSHIFT = false;

		if (multi->CTRL == isCTRL && multi->R_ALT == isRALT && multi->L_ALT == isLALT && multi->SHIFT == isSHIFT)
		{
			*length = O2M->size - ( sizeof(short) + sizeof(DWORD) + sizeof(wchar_t));
			*length /= 2;
			return (wchar_t*)multi->Output;
		}
Next:
		O2M = (File_One2Multi*)(next_loc + (LPBYTE)O2M);
	}

	*length = 0;
	return NULL;
};

wchar_t Match_One2One(const wchar_t user_input){
	const wchar_t *found_location = wcsstr(Single_Input.Input, &user_input);
	if (!found_location)
		return NULL;
	int found_index = found_location - Single_Input.Input;

	if (found_index > FileHeader->One_Count)
		return NULL;

	return Single_Input.Output[found_index];
}

LRESULT KEYMAGICDLL_API CALLBACK HookWndProc(int nCode, WPARAM wParam, LPARAM lParam)
{

	char Name[50];

	if (nCode < 0)
		return CallNextHookEx(hWndProcHook, nCode, wParam, lParam);

	CWPSTRUCT* cwp = (CWPSTRUCT*)lParam;
	
	switch (cwp->message)
	{
	case WM_ACTIVATE:
		if (LOWORD(cwp->wParam) == WA_INACTIVE){

			//kmCursorPos = 0;

			if (cwp->hwnd == Commander_hWnd)
				break;

			GetClassName(cwp->hwnd, (LPSTR)Name, 50);

			if (!lstrcmp(Name,"Shell_TrayWnd"))
				break;
			if (!lstrcmp(Name, "TrayNotifyWnd"))
				break;

			PostMessage(Commander_hWnd, KM_KILLFOCUS, 0,(LPARAM) cwp->hwnd);
		}
		if (LOWORD(cwp->wParam) == WA_ACTIVE){

			//kmCursorPos = 0;

			if (cwp->hwnd == Commander_hWnd)
				break;

			GetClassName(cwp->hwnd, (LPSTR)Name, 50);
			
			if (!lstrcmp(Name,"Shell_TrayWnd"))
				break;
			if (!lstrcmp(Name, "TrayNotifyWnd"))
				break;

			if (isActive)
				PostMessage(Commander_hWnd, KM_GETFOCUS, ActiveIndex,(LPARAM) cwp->hwnd);
			else if (!isActive)
				PostMessage(Commander_hWnd, KM_GETFOCUS, 0, (LPARAM) cwp->hwnd);
		}
		break;

	case KM_SETKBID:
		isActive = cwp->lParam;
		if (isActive)
		{
			if (cwp->wParam == ActiveIndex){
				isActive = false;
				ActiveIndex = -1;
			}
			else{
				OpenKbFile(cwp->wParam);
				ActiveIndex = cwp->wParam;
			}
		}
		break;

	case KM_RESCAN:
		LocalFree(SC);
		GetShortCuts();
	}

	return CallNextHookEx(hWndProcHook, nCode, wParam, lParam);
}


LRESULT KEYMAGICDLL_API CALLBACK HookGetMsgProc(int nCode, WPARAM wParam, LPARAM lParam)
{
	if (nCode < 0)
		return CallNextHookEx(hGetMsgHook, nCode, wParam, lParam);

	MSG* msg = (MSG*)lParam;
	switch (msg->message){
	}

	return CallNextHookEx(hGetMsgHook, nCode, wParam, lParam);
}

void KEYMAGICDLL_API HookInit(HWND hWnd,HHOOK hKbHook,HHOOK hWPHook, HHOOK hGMHook,LPCSTR ParentPath)
{

	Commander_hWnd = hWnd;
	hKeyHook = hKbHook;
	hWndProcHook = hWPHook;
	hGetMsgHook = hGMHook;
	lstrcpy(szDir, ParentPath);
}

bool OpenKbFile(int Index)
{
	if (Index == 0)
		return false;

	char Msg[300];

	if (!OpenForMapping(GetKeyBoard(Index))){
		wsprintf((LPSTR)Msg, "Cannot open \"%s\"!", GetKeyBoard(Index));
		MessageBox(GetDesktopWindow(), (LPSTR)Msg , "KeyMagic", MB_ICONEXCLAMATION | MB_OK);
		isActive = false;
		return false;
	}

	FileHeader = (KbFileHeader*)FilePtr;

	if (FileHeader->Magic != 'FKMK'){
		wsprintf((LPSTR)Msg, "\"%s\"\n is not a vaild KeyMagic's keyboard file!", GetKeyBoard(Index));
		MessageBox(GetDesktopWindow(), (LPSTR)Msg , "KeyMagic", MB_ICONEXCLAMATION | MB_OK);
		isActive = false;
		return false;
	}

	Single_Input.Input = (wchar_t*)((PBYTE)FilePtr+sizeof(KbFileHeader));
	Single_Input.Output = (wchar_t*)((PBYTE)FilePtr+sizeof(KbFileHeader))+FileHeader->One_Count;
	Multi_Output = (File_One2Multi*)(Single_Input.Output + FileHeader->One_Count);


	int total_O2M_size = 0;
	File_One2Multi *O2M;
	O2M = Multi_Output;

	for (int i=0; i < FileHeader->Multi_Count; i++)
	{
		total_O2M_size += O2M->size;
		O2M = (File_One2Multi*)((LPBYTE)O2M + O2M->size);
	}

	Custom_Patterns = (File_Custom*)((LPBYTE)Multi_Output + total_O2M_size);

	int total_CUS_size = 0;

	for (int i=0; i < FileHeader->Customize_Count; i++)
	{
		short MP_length = (short)*((LPBYTE)&Custom_Patterns->size_MatchPattern + total_CUS_size);
		wchar_t *MP = (wchar_t*)((LPBYTE)&Custom_Patterns->size_MatchPattern + sizeof(short) + total_CUS_size);
		short OP_length = (short)*( (LPBYTE) (MP + MP_length));

		total_CUS_size += (MP_length + OP_length + sizeof(short) ) * 2;
	}

	Back_Patterns = (File_Delete*)((PBYTE)Custom_Patterns + total_CUS_size);

	ActiveIndex = Index;
	isActive = true;
	return true;
}

bool OpenForMapping(LPCSTR FileName)
{

	CloseMapping();

	hFile = hFileMap = FilePtr = NULL;

	hFile = CreateFile(FileName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	
	if(hFile == INVALID_HANDLE_VALUE)
	{
		return false;
	}


	hFileMap = CreateFileMapping(hFile, NULL, PAGE_READONLY, 0, 0, NULL);

	if(hFileMap == NULL)
	{
		CloseHandle(hFile);
		hFile = NULL;
		return false;
	}

	FilePtr = MapViewOfFile(hFileMap, FILE_MAP_READ, 0, 0, 0);

	if(FilePtr == NULL)
	{
		CloseHandle(hFileMap);
		CloseHandle(hFile);
		hFileMap = NULL;
		hFile = NULL;
		return false;
	}

	return true;
}

void CloseMapping()
{
	if(FilePtr != NULL)
	{
		UnmapViewOfFile(FilePtr);
		FilePtr = NULL;
	}
	if(hFileMap != NULL)
	{
		CloseHandle(hFileMap);
		hFileMap = NULL;
	}
	if(hFile != NULL)
	{
		CloseHandle(hFile);
		hFile = NULL;
	}
}

LPCSTR GetKeyBoard(UINT Index){
	char szINI[MAX_PATH];
	char szKBNames[500];
	char szKBFile[MAX_PATH];
	char szKBPath[MAX_PATH];
	char szAllUser[MAX_PATH];
	char szKBP[] ="KeyBoardPaths";

	lstrcpy(szINI,szDir);
	PathAppend(szINI, "KeyMagic.ini");

	GetPrivateProfileString(szKBP, NULL, NULL, (LPSTR)szKBNames, 500, szINI);

	for (int i=0,Length = lstrlen(&szKBNames[i]),j=0; 
		j <= Index;
		i+=Length+1, j++, Length = lstrlen(&szKBNames[i])){
			GetPrivateProfileString(szKBP, (LPCSTR)&szKBNames[i], NULL, (LPSTR)szKBFile, MAX_PATH, szINI);
	}

	if (szKBFile[1] == ':')
		return szKBFile;
	
	//if (SUCCEEDED(SHGetFolderPath(NULL,
	//	CSIDL_COMMON_APPDATA,
	//	NULL,
	//	SHGFP_TYPE_CURRENT,
	//	szAllUser))){
	//	lstrcpy(szKBPath, szAllUser);
	//		PathAppend(szKBPath, "KeyMagic");
	//		PathAppend(szKBPath, szKBFile);
	//		return szKBPath;
	//}

	lstrcpy(szKBPath, szDir);
	PathAppend(szKBPath, szKBFile);

	return szKBPath;
}

void GetShortCuts(){
//	Logger("GetShortCuts Entry");
	char szINI[MAX_PATH]={0};
	char szKBNames[MAX_PATH]={0};
	char szKBP[] ="KeyBoardPaths";
	char szSC[] ="ShortCuts";

	lstrcpy(szINI,szDir);
	lstrcat(szINI,"\\KeyMagic.ini");

	GetPrivateProfileString(szKBP, NULL, NULL, (LPSTR)szKBNames, 500, szINI);

	SC = (KM_ShortCut*) LocalAlloc(LPTR, sizeof(KM_ShortCut)*50);

	NumOfShortCut=0;
	for (int i=0,j=0,Length = lstrlen(&szKBNames[i]);
		Length > 0;
		i+=Length+1, Length = lstrlen(&szKBNames[i]),j++){
			WORD Hotkey = GetPrivateProfileInt(szSC, &szKBNames[i], 0, szINI);
			if (Hotkey){
				SC[j].ukey = (char) Hotkey;
				SC[j].modkey = Hotkey >> 8;
			}
			NumOfShortCut++;
	}

	LocalReAlloc(SC, sizeof(KM_ShortCut)*NumOfShortCut, LPTR);

//	Logger("GetShortCuts Return : NumOfShortCut = %X ", NumOfShortCut);
}

bool TranslateToAscii (UINT *uVKey){
//	Logger("TranslateToAscii Entry");

	bool shiftDown;// = GetKeyState(VK_SHIFT) & 0x8000;
	bool capsToggled;// = GetKeyState(VK_CAPITAL) & 0x1;
	bool isUp = false;

	BYTE KeyStates[256];
	GetKeyboardState(KeyStates);

	shiftDown = KeyStates[VK_SHIFT] & 0x80;
	capsToggled = KeyStates[VK_CAPITAL] & 0x1;

//	Logger("UpperAndLower : isUp = %X", isUp);

 	WORD TransedChar = NULL;
	UINT ScanCode = MapVirtualKey(*uVKey, MAPVK_VK_TO_VSC);

//	Logger("uVKey = %X", *uVKey);

	if (!ScanCode)
		return false;

	int Return = ToAscii(*uVKey, ScanCode, KeyStates, &TransedChar, 0);

	if (!Return)
		return false;

//	Logger("uVKey = %X TransedChar = %X Return = %X ScanCode = %X", *uVKey, TransedChar, Return, ScanCode);
	*uVKey = TransedChar;

	//if ( (!shiftDown && capsToggled) || (shiftDown && !capsToggled) )
	//{
	//	isUp = true;
	//}
	//
	//if ( isUp ){
	//	switch (*uVKey)
	//	{
	//	case '0':
	//		*uVKey = ')';
	//		break;
	//	case '1':
	//		*uVKey = '!';
	//		break;
	//	case '2':
	//		*uVKey = '@';
	//		break;
	//	case '3':
	//		*uVKey = '#';
	//		break;
	//	case '4':
	//		*uVKey = '$';
	//		break;
	//	case '5':
	//		*uVKey = '%';
	//		break;
	//	case '6':
	//		*uVKey = '^';
	//		break;
	//	case '7':
	//		*uVKey = '&';
	//		break;
	//	case '8':
	//		*uVKey = '*';
	//		break;
	//	case '9':
	//		*uVKey = '(';
	//		break;
	//	case '-':
	//		*uVKey = '_';
	//		break;
	//	case '=':
	//		*uVKey = '+';
	//		break;
	//	case '`':
	//		*uVKey = '~';
	//		break;
	//	case '[':
	//		*uVKey = '{';
	//		break;
	//	case ']':
	//		*uVKey = '}';
	//		break;
	//	case '\\':
	//		*uVKey = '|';
	//		break;
	//	case ';':
	//		*uVKey = ':';
	//		break;
	//	case '\'':
	//		*uVKey = '"';
	//		break;
	//	case ',':
	//		*uVKey = '<';
	//		break;
	//	case '.':
	//		*uVKey = '>';
	//		break;
	//	case '/':
	//		*uVKey = '?';
	//		break;
	//	}
	//}

	//else if ( (*uVKey >= 'A') && (*uVKey <= 'Z') &&!isUp)
	//	*uVKey += 32;

//	Logger("TranslateToAscii Return");
	return true;
}

void Logger(char* fmt, ...)
{
#ifdef DEBUG
	char Memory[100];
	RECT rc;

	va_list list;

	va_start(list, fmt);
	//Format
	wvsprintf(Memory, fmt,list);

	GetClientRect(Commander_hWnd, &rc);

	rc.top += 50;
	SetWindowText(Commander_hWnd, Memory);
	
	//Cleanup
	va_end(list);
#else
	return;
#endif
}