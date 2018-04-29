#include "stdafx.h"
#include "EnumHandle.h"

#define SafeDeleteArraySize(pData) { if(pData){delete []pData;pData=NULL;} }  
pfnNtQuerySystemInformation ZwQuerySystemInformation;
pfnNtQueryObject ZwQueryObject;
pfnNtQueryInformationProcess ZwQueryInformationProcess;

bool EnableDebugPrivilege()
{
	HANDLE hToken;
	LUID sedebugnameValue;
	TOKEN_PRIVILEGES tkp;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
	{
		return   FALSE;
	}
	if (!LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &sedebugnameValue))
	{
		CloseHandle(hToken);
		return false;
	}
	tkp.PrivilegeCount = 1;
	tkp.Privileges[0].Luid = sedebugnameValue;
	tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
	if (!AdjustTokenPrivileges(hToken, FALSE, &tkp, sizeof(tkp), NULL, NULL))
	{
		CloseHandle(hToken);
		return false;
	}
	return true;
}

BOOL InitUnDocumentProc()
{
	HMODULE hNtdll = GetModuleHandle(L"ntdll.dll");
	if (hNtdll == NULL)	return FALSE;

	ZwQuerySystemInformation = \
		(pfnNtQuerySystemInformation)GetProcAddress(hNtdll, "NtQuerySystemInformation");
	ZwQueryObject = \
		(pfnNtQueryObject)GetProcAddress(hNtdll, "NtQueryObject");
	ZwQueryInformationProcess = \
		(pfnNtQueryInformationProcess)GetProcAddress(hNtdll, "NtQueryInformationProcess");

	if ((ZwQuerySystemInformation == NULL) || \
		(ZwQueryObject == NULL) || \
		(ZwQueryInformationProcess == NULL))
		return FALSE;
	return TRUE;
}

SYSTEM_HANDLE_INFORMATIO_EX *GetSystemProcessHandleInfo()
{
	DWORD buffLen = 0x1000;
	NTSTATUS status;
	BYTE* buff = new BYTE[buffLen];
	do {
		status = ZwQuerySystemInformation(SystemHandleInformation, buff, buffLen, &buffLen);
		if (status == STATUS_INFO_LENGTH_MISMATCH)
		{
			delete[] buff;
			buff = new BYTE[buffLen];
		}
		else
			break;

	} while (TRUE);
	return (SYSTEM_HANDLE_INFORMATIO_EX*)buff;
}

EnumHandle::EnumHandle()
{
}


EnumHandle::~EnumHandle()
{
}



BOOL EnumHandle::Init()
{
	EnableDebugPrivilege();
	InitUnDocumentProc();

	return true;
}

wchar_t* GetProcessIdName(HANDLE ProcessId)
{
	//定义变量  
	NTSTATUS status;
	ULONG NeededSize;
	PVOID pBuffer = NULL;
	wchar_t* pProcessName = NULL;
	PSYSTEM_PROCESS_INFORMATION pInfo;
	typedef NTSTATUS(__stdcall  *fnZwQuerySystemInformation)(SYSTEM_INFORMATION_CLASS SystemInformationClass, PVOID SystemInformation, ULONG SystemInformationLength, PULONG ReturnLength);
	static fnZwQuerySystemInformation pZwQuerySystemInformation = (fnZwQuerySystemInformation)GetProcAddress(GetModuleHandle(TEXT("ntdll.dll")), "ZwQuerySystemInformation");

	do
	{
		status = pZwQuerySystemInformation(SystemProcessesAndThreadsInformation, NULL, 0, &NeededSize);
		if (status != STATUS_INFO_LENGTH_MISMATCH || NeededSize <= 0)break;

		NeededSize *= 2;
		pBuffer = new BYTE[NeededSize];
		if (pBuffer == NULL)break;

		RtlZeroMemory(pBuffer, NeededSize);
		status = pZwQuerySystemInformation(SystemProcessesAndThreadsInformation, pBuffer, NeededSize, NULL);
		if (!NT_SUCCESS(status))break;
		pInfo = (PSYSTEM_PROCESS_INFORMATION)pBuffer;

		do
		{
			if (pInfo == NULL)break;
			if (pInfo->NextEntryOffset == 0)break;
			pInfo = (PSYSTEM_PROCESS_INFORMATION)(((PUCHAR)pInfo) + pInfo->NextEntryOffset);


			if (ProcessId == pInfo->UniqueProcessId)
			{
				pProcessName = (wchar_t*)new BYTE[pInfo->ImageName.Length + sizeof(wchar_t)];
				ZeroMemory(pProcessName, pInfo->ImageName.Length + sizeof(wchar_t));
				CopyMemory(pProcessName, pInfo->ImageName.Buffer, pInfo->ImageName.Length);
				break;
			}


		} while (TRUE);



	} while (FALSE);

	SafeDeleteArraySize(pBuffer);
	return pProcessName;
}

void EnumHandle::Show(DWORD pid,CListCtrl* m_list)
{
	NTSTATUS Status;
	HANDLE hSource = NULL;
	HANDLE hDuplicate = NULL;
	DWORD HandleCount = 0;
	OBJECT_NAME_INFORMATION *ObjectName;
	OBJECT_TYPE_INFORMATION *ObjectType;
	char BufferForObjectName[1024];
	char BufferForObjectType[1024];

	m_list->DeleteAllItems();

	hSource = OpenProcess(PROCESS_ALL_ACCESS | PROCESS_DUP_HANDLE | PROCESS_SUSPEND_RESUME, FALSE, pid);
	if (hSource != NULL)
	{
		DWORD dwHandle;
		Status = ZwQueryInformationProcess(hSource, ProcessHandleCount, &HandleCount, sizeof(HandleCount), NULL);

		for (DWORD i = 1; i <= 1024*10; i++)//穷举句柄
		{
			dwHandle = i * 4;
			if (DuplicateHandle(hSource, //复制一个句柄对象 && 判断此句柄是否有效
				(HANDLE)dwHandle,
				GetCurrentProcess(),
				&hDuplicate,
				0, FALSE, DUPLICATE_SAME_ACCESS))
			{
				ZeroMemory(BufferForObjectName, 1024);
				ZeroMemory(BufferForObjectType, 1024);

				//获取句柄类型
				Status = ZwQueryObject(hDuplicate,
					ObjectTypeInformation,
					BufferForObjectType,
					sizeof(BufferForObjectType),
					NULL);

				ObjectType = (OBJECT_TYPE_INFORMATION*)BufferForObjectType;
				if (Status == STATUS_INFO_LENGTH_MISMATCH || !NT_SUCCESS(Status))
					continue;

				//获取句柄名
				Status = ZwQueryObject((HANDLE)hDuplicate,
					ObjectNameInformation,
					BufferForObjectName,
					sizeof(BufferForObjectName),
					NULL);

				//关闭复制的句柄
				
				ObjectName = (POBJECT_NAME_INFORMATION)BufferForObjectName;
				if (Status == STATUS_INFO_LENGTH_MISMATCH || !NT_SUCCESS(Status))
					continue;
				if (CString((WCHAR*)(ObjectType->TypeName.Buffer)) != L"Process")
				{
					continue;
				}
				
				CString strHandle;
				strHandle.Format(L"%d", dwHandle);
				DWORD dwLid = m_list->InsertItem(0, strHandle, 0);
				CString strPid;
				strPid.Format(L"%s", GetProcessIdName((HANDLE)GetProcessId(hDuplicate)));
				m_list->SetItemText(dwLid, 1, CString((WCHAR*)(ObjectType->TypeName.Buffer)));
				m_list->SetItemText(dwLid, 2, strPid);
				CloseHandle(hDuplicate);

			}
		}
		CloseHandle(hSource);
	}
	return;

}
