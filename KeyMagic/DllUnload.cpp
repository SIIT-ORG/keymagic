#include "DllUnload.h"

char Path[MAX_PATH];
BYTE Injection[] = {0x68,0x90,0x90,0x90,0x90,0xFF,0x15,0x90,0x90,0x90,0x90,0x50,0xFF,0x15,0x90,0x90,0x90,0x90,0xC3,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90};

void Scanner ( )
{
	DWORD Function;

	Function = (DWORD)GetProcAddress(GetModuleHandle("kernel32.dll"), "GetModuleHandleA");
	memcpy(&Injection[19], &Function, 4);
	Function = (DWORD)GetProcAddress(GetModuleHandle("kernel32.dll"), "FreeLibrary");
	memcpy(&Injection[23], &Function, 4);

	// Get the list of process identifiers.

	DWORD aProcesses[1024], cbNeeded, cProcesses;
	unsigned int i;

	if ( !EnumProcesses( aProcesses, sizeof(aProcesses), &cbNeeded ) )
		return;

	// Calculate how many process identifiers were returned.

	cProcesses = cbNeeded / sizeof(DWORD);

	// Get the name of the modules for each process.

	for ( i = 0; i < cProcesses; i++ )
		GetModules( aProcesses[i] );
}

void GetModules( DWORD processID )
{
    HMODULE hMods[1024];
    HANDLE hProcess;
    DWORD cbNeeded;
	TCHAR szTemp[MAX_PATH];
    unsigned int i;

    // Get a list of all the modules in this process.

    hProcess = OpenProcess( PROCESS_ALL_ACCESS,
                            FALSE, processID );
    if (NULL == hProcess)
        return;

    if( EnumProcessModules(hProcess, hMods, sizeof(hMods), &cbNeeded))
    {
        for ( i = 0; i < (cbNeeded / sizeof(HMODULE)); i++ )
        {
            TCHAR szModName[MAX_PATH];

            // Get the full path to the module's file

            if (int l = GetModuleFileNameEx(hProcess, hMods[i], szModName,
                                     sizeof(szModName)/sizeof(TCHAR)))
            {
				for (l;l > 0 ; l--){
					if (szModName[l] == '\\'){
						if (!lstrcmpi((LPSTR)szModName+l+1, (LPSTR)"KeymagicDLL.dll")){
							//Unload KeymagicDll in this process ID
							UnloadDLL( szModName, hProcess );
							CloseHandle( hProcess );
							return;
						}
						break;
					}
				}
            }
        }
    }

    CloseHandle( hProcess );
}

void UnloadDLL ( char *DLLPath, HANDLE hProcess ){

	DWORD InjAddr, Pointer, PathAddress;
	HANDLE hThread;

	InjAddr = (DWORD)VirtualAllocEx(hProcess, NULL, 1000, MEM_COMMIT, PAGE_EXECUTE_READWRITE);

	WriteProcessMemory(hProcess, (LPVOID)InjAddr, &Injection, sizeof(Injection), NULL);

	WriteProcessMemory(hProcess, (LPVOID)((LPBYTE)InjAddr+sizeof(Injection)), DLLPath, MAX_PATH, NULL);

	PathAddress = (DWORD)(InjAddr+sizeof(Injection));
	WriteProcessMemory(hProcess, (LPVOID)((LPBYTE)InjAddr+1), &PathAddress, 4, NULL);

	Pointer = (DWORD)((LPBYTE)InjAddr + 19);
	WriteProcessMemory(hProcess, (LPVOID)((LPBYTE)InjAddr+7), &Pointer, 4, NULL);

	Pointer += 4;
	WriteProcessMemory(hProcess, (LPVOID)((LPBYTE)InjAddr+14), &Pointer, 4, NULL);

	hThread = CreateRemoteThread(hProcess, NULL, NULL, (LPTHREAD_START_ROUTINE)InjAddr, NULL, THREAD_PRIORITY_NORMAL, NULL);

	CloseHandle(hThread);

	Sleep(50);

	VirtualFreeEx(hProcess, (LPVOID)InjAddr, 1000, MEM_DECOMMIT);

};