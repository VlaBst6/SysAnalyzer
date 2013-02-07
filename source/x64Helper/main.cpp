#include <stdio.h>
#include <conio.h>
#include <Windows.h>
#include <windows.h>
#include <tchar.h>
#include <stdio.h>
#include <psapi.h>
#pragma comment(lib, "psapi") 

typedef unsigned int uint;

//this works against 32 bit processes as well...

// To ensure correct resolution of symbols, add Psapi.lib to TARGETLIBS
// and compile with -DPSAPI_VERSION=1

bool FileExists(char* szPath)
{
  DWORD dwAttrib = GetFileAttributes(szPath);
  bool rv = (dwAttrib != INVALID_FILE_ATTRIBUTES && !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY)) ? true : false;
  return rv;
}

bool isx64Process(int pid){
	BOOL ret = false;
	HANDLE hProcess = OpenProcess( PROCESS_QUERY_INFORMATION, FALSE, pid );
	if (NULL == hProcess){
		printf("Error: failed to open process..");
        return false;
	}
	if(IsWow64Process(hProcess, &ret)==0){
		printf("Error: IsWow64Process failed");
		ret = FALSE;
	}
	CloseHandle(hProcess);
	return ret == TRUE ? true : false;
}

BOOL SetPrivilege(
    HANDLE hToken,          // access token handle
    LPCTSTR lpszPrivilege,  // name of privilege to enable/disable
    BOOL bEnablePrivilege   // to enable or disable privilege
    ) 
{
    TOKEN_PRIVILEGES tp;
    LUID luid;

    if ( !LookupPrivilegeValue( 
            NULL,            // lookup privilege on local system
            lpszPrivilege,   // privilege to lookup 
            &luid ) )        // receives LUID of privilege
    {
        printf("Error: LookupPrivilegeValue error: %u\n", GetLastError() ); 
        return FALSE; 
    }

    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    if (bEnablePrivilege)
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    else
        tp.Privileges[0].Attributes = 0;

    // Enable the privilege or disable all privileges.

    if ( !AdjustTokenPrivileges(
           hToken, 
           FALSE, 
           &tp, 
           sizeof(TOKEN_PRIVILEGES), 
           (PTOKEN_PRIVILEGES) NULL, 
           (PDWORD) NULL) )
    { 
          printf("Error: AdjustTokenPrivileges error: %u\n", GetLastError() ); 
          return FALSE; 
    } 

    if (GetLastError() == ERROR_NOT_ALL_ASSIGNED)

    {
          printf("Error: The token does not have the specified privilege. \n");
          return FALSE;
    } 

    return TRUE;
}

BOOL EnableSeDebug(void){
	HANDLE hToken;
	BOOL rv = false;
	OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken);
	rv = SetPrivilege(hToken,SE_DEBUG_NAME, TRUE);
	CloseHandle(hToken);
	printf("SeDebug Enabled? %s\n", rv==TRUE ? "true" : "false");
	return rv;
};

int PrintModules( DWORD processID )
{
    HMODULE hMods[1024];
	MODULEINFO minfo;
    HANDLE hProcess;
    DWORD cbNeeded;
	TCHAR szModName[MAX_PATH];
	int modSize=0;

    hProcess = OpenProcess( PROCESS_QUERY_INFORMATION |
                            PROCESS_VM_READ,
                            FALSE, processID );
    if (NULL == hProcess){
		printf("Error: failed to open process..");
        return 1;
	}

	int ret = EnumProcessModulesEx(hProcess, hMods, sizeof(hMods), &cbNeeded, LIST_MODULES_ALL);

	if( cbNeeded > sizeof(hMods) ){
		printf("Error: buffer to small to get all modules!"); //shouldnt happen...
		return 1;
	}

    if( ret )
    {
        for (int i = 0; i < (cbNeeded / sizeof(HMODULE)); i++ )
        {
            
			if(GetModuleInformation(hProcess, hMods[i], &minfo, sizeof(minfo)) == 0){
				modSize=0;
			}else{
				modSize = minfo.SizeOfImage;
			}

            if ( GetModuleFileNameEx( hProcess, hMods[i], szModName, sizeof(szModName) / sizeof(TCHAR)))
            {
                _tprintf( TEXT("0x%010X,0x%010X,%s\n"), hMods[i] , modSize, szModName);
            }
        }
	}
	else{
		printf("Error: EnumProcessModulesEx failed %x", ret); 
    }

    CloseHandle( hProcess );

    return 0;
}

int inject(char* dll, uint pid){
	
	HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);

	int sz = strlen(dll) + 1;

	if (hProcess == INVALID_HANDLE_VALUE)
	{
		printf("Error: cannot open pid %x\n", pid);
		return 1;
	}

	printf("OpenProcess( %d ) = %x\n", pid, hProcess);

	PVOID mem = VirtualAllocEx(hProcess, NULL, sz, MEM_COMMIT, PAGE_READWRITE);

	if (mem == NULL)
	{
		printf("Error: can't allocate memory for dll name\n");
		CloseHandle(hProcess);
		return 1;
	}

	printf("VirtualAllocEx() = %x\n", mem);

	if (WriteProcessMemory(hProcess, mem, (void*)dll, sz, NULL) == 0)
	{
		printf("Error: failed to write to dll name to remote process memory\n");
		VirtualFreeEx(hProcess, mem, sz, MEM_RELEASE);
		CloseHandle(hProcess);
		return 1;
	}

	printf("WriteProcessMemory() = success\n");

	HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0, (LPTHREAD_START_ROUTINE) GetProcAddress(GetModuleHandle("KERNEL32.DLL"),"LoadLibraryA"), mem, 0, NULL);
	if (hThread == INVALID_HANDLE_VALUE)
	{
		printf("Error: CreateRemoteThread failed\n");
		VirtualFreeEx(hProcess, mem, sz , MEM_RELEASE);
		CloseHandle(hProcess);
		return 1;
	}

	printf("CreateRemoteThread() = %x\n", hThread); 
	CloseHandle(hProcess);

	return 0;

}

int startwdll(char* dll, char* exePath){
	
	PROCESS_INFORMATION pi;
	STARTUPINFO si;

	printf("exe: %s\n", exePath);
	printf("dll: %s\n", dll);

	memset(&pi, 0, sizeof(PROCESS_INFORMATION) );
	memset(&si, 0, sizeof(STARTUPINFO) );

	if( CreateProcess(0, exePath, 0, 0, 1, CREATE_SUSPENDED, 0, 0, &si, &pi) == 0){;
		printf("Error: failed to start process %s\n", exePath);
		return 1;
	}
	
	
	int sz = strlen(dll) + 1;
	HANDLE hProcess = pi.hProcess;

	if (hProcess == INVALID_HANDLE_VALUE)
	{
		printf("Error: cannot open pid %x\n", pi.dwProcessId);
		return 1;
	}

	printf("CreateProcess( pid = %d, hProcess = %x)\n", pi.dwProcessId, hProcess);

	PVOID mem = VirtualAllocEx(hProcess, NULL, sz, MEM_COMMIT, PAGE_READWRITE);

	if (mem == NULL)
	{
		printf("Error: can't allocate memory for dll name\n");
		CloseHandle(hProcess);
		return 1;
	}

	printf("VirtualAllocEx() = %x\n", mem);

	if (WriteProcessMemory(hProcess, mem, (void*)dll, sz, NULL) == 0)
	{
		printf("Error: failed to write to dll name to remote process memory\n");
		VirtualFreeEx(hProcess, mem, sz, MEM_RELEASE);
		CloseHandle(hProcess);
		return 1;
	}

	printf("WriteProcessMemory() = success\n");

	FARPROC lpstart = GetProcAddress(GetModuleHandle("KERNEL32.DLL"),"LoadLibraryA");
	printf("start address = %x\n", lpstart);

	HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0, (LPTHREAD_START_ROUTINE) lpstart, mem, 0, NULL);
	if (hThread == INVALID_HANDLE_VALUE)
	{
		printf("Error: CreateRemoteThread failed\n");
		VirtualFreeEx(hProcess, mem, sz , MEM_RELEASE);
		CloseHandle(hProcess);
		return 1;
	}

	printf("CreateRemoteThread() = %x\n", hThread); 
	CloseHandle(hProcess);


	Sleep(300);

	if( ResumeThread(pi.hThread) == -1){
		printf("Error: Resume thread failed...\n");
		return 1;
	}

	CloseHandle(pi.hProcess);

	return 0;

}


int dump(int pid, uint base, uint size, char* out_file){

	HANDLE h = OpenProcess(PROCESS_VM_READ, FALSE, pid);

	if (h == INVALID_HANDLE_VALUE)
	{
		printf("Error: cannot open pid %x\n", pid);
		return 1;
	}

	void* mem = malloc(size);

	if (mem == NULL)
	{
		printf("Error: can't allocate memory buffer sz=%x\n",size);
		CloseHandle(h);
		return 1;
	}

	SIZE_T bytesRead;
	if( ReadProcessMemory(h,(void*)base,mem,size,&bytesRead) == 0){
		printf("Error: Failed to read memory from process base=%x, size=%x\n",base,size);
		CloseHandle(h);
		free(mem);
		return 1;
	}

	FILE* fp = fopen(out_file,"wb");
	fwrite(mem,1,size,fp);
	fclose(fp);
	printf("Dump saved to %s\n", out_file);

	CloseHandle(h);
	free(mem);
	return 0;
}

int DumpProcess( DWORD processID, char* dumpPath )
{
    HMODULE hMods[1024];
	MODULEINFO minfo;
    HANDLE hProcess;
    DWORD cbNeeded;
	int rv=0;

    hProcess = OpenProcess( PROCESS_QUERY_INFORMATION |
                            PROCESS_VM_READ,
                            FALSE, processID );
    if (NULL == hProcess){
		printf("Error: failed to open process..");
        return 1;
	}

	int ret = EnumProcessModulesEx(hProcess, hMods, sizeof(hMods), &cbNeeded, LIST_MODULES_ALL);

	if( cbNeeded > sizeof(hMods) ){
		printf("Error: buffer to small to get all modules!"); //shouldnt happen...
		return 1;
	}

    if( ret )
    {
		if(GetModuleInformation(hProcess, hMods[0], &minfo, sizeof(minfo)) == 0){
			printf("Error: GetModuleInformation failed %x", ret); 
		}else{
			rv = dump(processID, (uint)minfo.lpBaseOfDll, minfo.SizeOfImage, dumpPath);
		}
	}
	else{
		printf("Error: EnumProcessModulesEx failed %x", ret); 
    }

    CloseHandle( hProcess );

    return rv;
}

void usage(int invalidOptionCount=0){
	if(invalidOptionCount>0) printf("Error: Invalid option sequence specified expects %d..\n\n", invalidOptionCount);
	printf("Usage x64Helper:\n");
	printf("\t/inject decimal_pid dll_path\n");
	printf("\t/dlls decimal_pid\n");
	printf("\t/dumpmodule decimal_pid hex_string_base hex_string_size out_file_path\n");
	printf("\t/dumpprocess decimal_pid out_file_path\n");
	printf("\t/startwdll exe_path dll_path\n");
	if( IsDebuggerPresent() ) getch();
	exit(0);
}

int main(int argc, char* argv[] )
{
	uint pid=0;
	char* dll= 0;
	int rv = 0;
	bool handled = false;

	if(argc < 2) usage();
	if( argv[1][0] == '-') argv[1][0] = '/'; //standardize

	EnableSeDebug();

	// /inject decimal_pid dll_path
	if(strstr(argv[1],"/inject") > 0 ){ 
		if(argc!=4) usage(3);
		pid = atoi( argv[2] );
		dll = strdup(argv[3]);
		if(!FileExists(dll)){
			printf("Error: dll file not found: %s\n\n",dll);
			usage();
		}
		rv = inject(dll,pid);
		handled = true;
	}
		
	// /dlls decimal_pid
	if(strstr(argv[1],"/dlls") > 0 ){ 
		if(argc!=3) usage(2);
		pid = atoi( argv[2] );
		rv = PrintModules(pid);
		handled = true;
	}

	// /dumpprocess decimal_pid out_file_path
	if(strstr(argv[1],"/dumpproc") > 0 ){ 
		if(argc!=4) usage(3);
		pid        = atoi( argv[2] );
		char* dumpFile = strdup(argv[3]);
		if(FileExists(dumpFile)){
			printf("Error: dump file already exists aborting: %s\n\n",  dumpFile);
		}
		else{
			rv = DumpProcess(pid,dumpFile);
		}
		handled = true;
	}
	 
	// /dump decimal_pid, hex_string_base, hex_string_size out_file_path
	if(!handled && strstr(argv[1],"/dumpmod") > 0 ){ 
		if(argc!=6) usage(5);
		pid        = atoi( argv[2] );
		uint base  = strtol(argv[3], NULL, 16);
		uint sz    = strtol(argv[4], NULL, 16);
		char* dumpFile = strdup(argv[5]);
		if(FileExists(dumpFile)){
			printf("Error: dump file already exists aborting: %s\n\n",  dumpFile);
		}
		else{
			rv = dump(pid,base,sz,dumpFile);
		}
		handled = true;
	}

	// /startwdll exe_path dll_path
	if(strstr(argv[1],"/startwdll") > 0 ){ 
		if(argc!=4) usage(3);
		char* exe = strdup(argv[2]);
		dll = strdup(argv[3]);
		if(!FileExists(dll)){
			printf("Error: dll file not found: %s\n\n",dll);
			usage();
		}
		rv = startwdll(dll,exe);
		handled = true;
	}


	if(handled==false){
		printf("Error: Unknown option %s\n\n", argv[1]);
		usage();
	}

	if( IsDebuggerPresent() ) getch();

    return rv;
}

/*
int WINAPI DllMain(
  _In_  HINSTANCE hinstDLL,
  _In_  DWORD fdwReason,
  _In_  LPVOID lpvReserved
){

	if(1==fdwReason) MessageBox(0,"In x64 dll main","",0);

}*/
/*
void main(void){
	printf("LoadLibrary=%x", (int)LoadLibrary);
	getch();
	LoadLibrary("x64.dll");
}*/

/*
  DWORD aProcesses[1024]; 
    DWORD cbNeeded; 
    DWORD cProcesses;
    unsigned int i;

    // Get the list of process identifiers.

    if ( !EnumProcesses( aProcesses, sizeof(aProcesses), &cbNeeded ) )
        return 1;

    // Calculate how many process identifiers were returned.

    cProcesses = cbNeeded / sizeof(DWORD);

    // Print the names of the modules for each process.

    for ( i = 0; i < cProcesses; i++ )
    {
        PrintModules( aProcesses[i] );
    } 
*/