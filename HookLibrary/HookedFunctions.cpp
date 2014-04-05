#include "HookMain.h"
#include "HookedFunctions.h"
#include "HookHelper.h"
#include <intrin.h>

#pragma intrinsic(_ReturnAddress)

extern HOOK_DLL_EXCHANGE DllExchange;

void FilterProcess(PSYSTEM_PROCESS_INFORMATION pInfo);
void FilterObjects(POBJECT_TYPES_INFORMATION pObjectTypes);
void FilterObject(POBJECT_TYPE_INFORMATION pObject);
void FilterHwndList(HWND * phwndFirst, PUINT pcHwndNeeded);

typedef struct _SAVE_DEBUG_REGISTERS {
	DWORD dwThreadId;
	DWORD   Dr0;
	DWORD   Dr1;
	DWORD   Dr2;
	DWORD   Dr3;
	DWORD   Dr6;
	DWORD   Dr7;
} SAVE_DEBUG_REGISTERS;

SAVE_DEBUG_REGISTERS ArrayDebugRegister[100] = { 0 }; //Max 100 threads


NTSTATUS NTAPI HookedNtSetInformationThread(HANDLE ThreadHandle, THREADINFOCLASS ThreadInformationClass, PVOID ThreadInformation, ULONG ThreadInformationLength)
{
    if (ThreadInformationClass == ThreadHideFromDebugger && ThreadInformation == 0 && ThreadInformationLength == 0)
    {
        if (ThreadHandle == NtCurrentThread || GetCurrentProcessId() == GetProcessIdByThreadHandle(ThreadHandle)) //thread inside this process?
        {
            return STATUS_SUCCESS;
        }
    }
    return DllExchange.dNtSetInformationThread(ThreadHandle, ThreadInformationClass, ThreadInformation, ThreadInformationLength);
}

NTSTATUS NTAPI HookedNtQuerySystemInformation(SYSTEM_INFORMATION_CLASS SystemInformationClass, PVOID SystemInformation, ULONG SystemInformationLength, PULONG ReturnLength)
{
    if (SystemInformationClass == SystemKernelDebuggerInformation || SystemInformationClass == SystemProcessInformation)
    {
        NTSTATUS ntStat = DllExchange.dNtQuerySystemInformation(SystemInformationClass, SystemInformation, SystemInformationLength, ReturnLength);
        if (NT_SUCCESS(ntStat))
        {
            if (SystemInformationClass == SystemKernelDebuggerInformation)
            {
                ((PSYSTEM_KERNEL_DEBUGGER_INFORMATION)SystemInformation)->KernelDebuggerEnabled = FALSE;
                ((PSYSTEM_KERNEL_DEBUGGER_INFORMATION)SystemInformation)->KernelDebuggerNotPresent = TRUE;
            }
            else if (SystemInformationClass == SystemProcessInformation)
            {
                FilterProcess((PSYSTEM_PROCESS_INFORMATION)SystemInformation);
            }
        }

        return ntStat;
    }
    return DllExchange.dNtQuerySystemInformation(SystemInformationClass, SystemInformation, SystemInformationLength, ReturnLength);
}

static ULONG ValueProcessBreakOnTermination = FALSE;

NTSTATUS NTAPI HookedNtQueryInformationProcess(HANDLE ProcessHandle, PROCESSINFOCLASS ProcessInformationClass, PVOID ProcessInformation, ULONG ProcessInformationLength, PULONG ReturnLength)
{
    if (ProcessHandle == NtCurrentProcess || GetCurrentProcessId() == GetProcessIdByProcessHandle(ProcessHandle))
    {
        NTSTATUS ntStat = DllExchange.dNtQueryInformationProcess(ProcessHandle, ProcessInformationClass, ProcessInformation, ProcessInformationLength, ReturnLength);

        if (NT_SUCCESS(ntStat) && ProcessInformation != 0 && ProcessInformationLength != 0)
        {
            if (ProcessInformationClass == ProcessDebugFlags)
            {
                *((ULONG *)ProcessInformation) = 1;
            }
            else if (ProcessInformationClass == ProcessDebugObjectHandle)
            {
                *((HANDLE *)ProcessInformation) = 0;
                return STATUS_PORT_NOT_SET;
            }
            else if (ProcessInformationClass == ProcessDebugPort)
            {
                *((HANDLE *)ProcessInformation) = 0;
            }
            else if (ProcessInformationClass == ProcessBasicInformation) //Fake parent
            {
                ((PPROCESS_BASIC_INFORMATION)ProcessInformation)->InheritedFromUniqueProcessId = (HANDLE)GetExplorerProcessId();
            }
            else if (ProcessInformationClass == ProcessBreakOnTermination)
            {
                *((ULONG *)ProcessInformation) = ValueProcessBreakOnTermination;
            }
        }

        return ntStat;
    }
    return DllExchange.dNtQueryInformationProcess(ProcessHandle, ProcessInformationClass, ProcessInformation, ProcessInformationLength, ReturnLength);
}

NTSTATUS NTAPI HookedNtSetInformationProcess(HANDLE ProcessHandle, PROCESSINFOCLASS ProcessInformationClass, PVOID ProcessInformation, ULONG ProcessInformationLength)
{
    if (ProcessHandle == NtCurrentProcess || GetCurrentProcessId() == GetProcessIdByProcessHandle(ProcessHandle))
    {
        if (ProcessInformationClass == ProcessBreakOnTermination && ProcessInformation != 0 && sizeof(ULONG) == ProcessInformationLength)
        {
            ValueProcessBreakOnTermination = *((ULONG *)ProcessInformation);
            return STATUS_SUCCESS;
        }
    }
    return DllExchange.dNtSetInformationProcess(ProcessHandle, ProcessInformationClass, ProcessInformation, ProcessInformationLength);
}

NTSTATUS NTAPI HookedNtQueryObject(HANDLE Handle, OBJECT_INFORMATION_CLASS ObjectInformationClass, PVOID ObjectInformation, ULONG ObjectInformationLength, PULONG ReturnLength)
{
    NTSTATUS ntStat = DllExchange.dNtQueryObject(Handle, ObjectInformationClass, ObjectInformation, ObjectInformationLength, ReturnLength);

    if (NT_SUCCESS(ntStat) && ObjectInformation)
    {
        if (ObjectInformationClass == ObjectTypesInformation)
        {
            FilterObjects((POBJECT_TYPES_INFORMATION)ObjectInformation);
        }
        else if (ObjectInformationClass == ObjectTypeInformation)
        {
            FilterObject((POBJECT_TYPE_INFORMATION)ObjectInformation);
        }
    }

    return ntStat;
}

NTSTATUS NTAPI HookedNtYieldExecution()
{
    DllExchange.dNtYieldExecution();
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI HookedNtGetContextThread(HANDLE ThreadHandle, PCONTEXT ThreadContext)
{
    DWORD ContextBackup = 0;
    if (ThreadHandle == NtCurrentThread || GetCurrentProcessId() == GetProcessIdByThreadHandle(ThreadHandle)) //thread inside this process?
    {
        if (ThreadContext)
        {
            ContextBackup = ThreadContext->ContextFlags;
            ThreadContext->ContextFlags &= ~CONTEXT_DEBUG_REGISTERS;
        }
    }

    NTSTATUS ntStat = DllExchange.dNtGetContextThread(ThreadHandle, ThreadContext);

    if (ContextBackup)
    {
        ThreadContext->ContextFlags = ContextBackup;
    }
    return ntStat;
}

NTSTATUS NTAPI HookedNtSetContextThread(HANDLE ThreadHandle, PCONTEXT ThreadContext)
{
    DWORD ContextBackup = 0;
    if (ThreadHandle == NtCurrentThread || GetCurrentProcessId() == GetProcessIdByThreadHandle(ThreadHandle)) //thread inside this process?
    {
        if (ThreadContext)
        {
            ContextBackup = ThreadContext->ContextFlags;
            ThreadContext->ContextFlags &= ~CONTEXT_DEBUG_REGISTERS;
        }
    }

    NTSTATUS ntStat = DllExchange.dNtSetContextThread(ThreadHandle, ThreadContext);

    if (ContextBackup)
    {
        ThreadContext->ContextFlags = ContextBackup;
    }

    return ntStat;
}

int slotIndex = 0;

void NTAPI HandleKiUserExceptionDispatcher(PEXCEPTION_RECORD pExcptRec, PCONTEXT ContextFrame)
{
	if (ContextFrame && (ContextFrame->ContextFlags & CONTEXT_DEBUG_REGISTERS))
	{
		ArrayDebugRegister[slotIndex].dwThreadId = GetCurrentThreadId();
		ArrayDebugRegister[slotIndex].Dr0 = ContextFrame->Dr0;
		ArrayDebugRegister[slotIndex].Dr1 = ContextFrame->Dr1;
		ArrayDebugRegister[slotIndex].Dr2 = ContextFrame->Dr2;
		ArrayDebugRegister[slotIndex].Dr3 = ContextFrame->Dr3;
		ArrayDebugRegister[slotIndex].Dr6 = ContextFrame->Dr6;
		ArrayDebugRegister[slotIndex].Dr7 = ContextFrame->Dr7;

		ContextFrame->Dr0 = 0;
		ContextFrame->Dr1 = 0;
		ContextFrame->Dr2 = 0;
		ContextFrame->Dr3 = 0;
		ContextFrame->Dr6 = 0;
		ContextFrame->Dr7 = 0;
	}
}

VOID __declspec(naked) NTAPI HookedKiUserExceptionDispatcher()// (PEXCEPTION_RECORD pExcptRec, PCONTEXT ContextFrame) //remove DRx Registers
{
	//MOV ECX,DWORD PTR SS:[ESP+4] <- ContextFrame
	//MOV EBX,DWORD PTR SS:[ESP] <- pExcptRec
	__asm {
			MOV EAX, [ESP + 4]
			MOV ECX, [ESP]
			PUSH EAX
			PUSH ECX
			CALL HandleKiUserExceptionDispatcher
			jmp DllExchange.dKiUserExceptionDispatcher
	}

    //return DllExchange.dKiUserExceptionDispatcher(pExcptRec, ContextFrame);
}

static DWORD_PTR KiUserExceptionDispatcherAddress = 0;

NTSTATUS NTAPI HookedNtContinue(PCONTEXT ThreadContext, BOOLEAN RaiseAlert) //restore DRx Registers
{
	DWORD_PTR retAddress = (DWORD_PTR)_ReturnAddress();
	if (!KiUserExceptionDispatcherAddress)
	{
		KiUserExceptionDispatcherAddress = (DWORD_PTR)GetProcAddress(DllExchange.hNtdll, "KiUserExceptionDispatcher");
	}

	if (ThreadContext)
	{
		//char text[100];
		//wsprintfA(text, "HookedNtContinue return %X", _ReturnAddress());
		//MessageBoxA(0, text, "debug", 0);

		if (retAddress >= KiUserExceptionDispatcherAddress && retAddress < (KiUserExceptionDispatcherAddress + 0x200))
		{
			ThreadContext->Dr0 = ArrayDebugRegister[slotIndex].Dr0;
			ThreadContext->Dr1 = ArrayDebugRegister[slotIndex].Dr1;
			ThreadContext->Dr2 = ArrayDebugRegister[slotIndex].Dr2;
			ThreadContext->Dr3 = ArrayDebugRegister[slotIndex].Dr3;
			ThreadContext->Dr6 = ArrayDebugRegister[slotIndex].Dr6;
			ThreadContext->Dr7 = ArrayDebugRegister[slotIndex].Dr7;
		}
    }

    return DllExchange.dNtContinue(ThreadContext, RaiseAlert);
}

NTSTATUS NTAPI HookedNtClose(HANDLE Handle)
{
    OBJECT_HANDLE_FLAG_INFORMATION flags;
    flags.ProtectFromClose = 0;
    flags.Inherit = 0;
    if (NtQueryObject(Handle, ObjectHandleFlagInformation, &flags, sizeof(OBJECT_HANDLE_FLAG_INFORMATION), 0) >= 0)
    {
        if (flags.ProtectFromClose)
        {
            return STATUS_HANDLE_NOT_CLOSABLE;
        }

        return DllExchange.dNtClose(Handle);
    }
    else
    {
        return STATUS_INVALID_HANDLE;
    }
}

static DWORD OneTickCount = 0;

DWORD WINAPI HookedGetTickCount(void)
{
    if (!OneTickCount)
    {
        OneTickCount = DllExchange.dGetTickCount();
    }
    else
    {
        OneTickCount++;
    }
    return OneTickCount;
}

static BOOL isBlocked = FALSE;

BOOL WINAPI HookedBlockInput(BOOL fBlockIt)
{
    if (isBlocked == FALSE && fBlockIt != FALSE)
    {
        isBlocked = TRUE;
        return TRUE;
    }
    else if (isBlocked != FALSE && fBlockIt == FALSE)
    {
        isBlocked = FALSE;
        return TRUE;
    }

    return FALSE;
}

//GetLastError() function might not change if a  debugger is present (it has never been the case that it is always set to zero).
DWORD WINAPI HookedOutputDebugStringA(LPCSTR lpOutputString) //Worst anti-debug ever
{
    if (IsAtleastVista())
    {
        return 0;
    }
    else
    {
        SetLastError(GetLastError() + 1); //change last error
        return 1; //WinXP EAX -> 1
    }
}

HWND NTAPI HookedNtUserFindWindowEx(HWND hWndParent, HWND hWndChildAfter, PUNICODE_STRING lpszClass, PUNICODE_STRING lpszWindow, DWORD dwType)
{
    HWND resultHwnd = DllExchange.dNtUserFindWindowEx(hWndParent, hWndChildAfter, lpszClass, lpszWindow, dwType);
    if (resultHwnd)
    {
        if (IsWindowClassBad(lpszClass) || IsWindowNameBad(lpszWindow))
        {
            return 0;
        }
    }
    return resultHwnd;
}

NTSTATUS NTAPI HookedNtSetDebugFilterState(ULONG ComponentId, ULONG Level, BOOLEAN State)
{
    return STATUS_ACCESS_DENIED;
}

void FilterHwndList(HWND * phwndFirst, PUINT pcHwndNeeded)
{
	DWORD dwProcessId = 0;

	for (UINT i = 0; i < *pcHwndNeeded; i++)
	{
		if (DllExchange.EnableProtectProcessId == TRUE)
		{
			//GetWindowThreadProcessId(phwndFirst[i], &dwProcessId);
			dwProcessId = (DWORD)DllExchange.NtUserQueryWindow(phwndFirst[i], WindowProcess);
			if (dwProcessId == DllExchange.dwProtectedProcessId)
			{
				if (i > 0)
				{
					phwndFirst[i] = phwndFirst[i - 1]; //just override with previous
				}
				else
				{
					phwndFirst[i] = phwndFirst[i + 1];
				}
			}
		}
	}

}

NTSTATUS NTAPI HookedNtUserBuildHwndList(HDESK hdesk, HWND hwndNext, BOOL fEnumChildren, DWORD idThread, UINT cHwndMax, HWND *phwndFirst, PUINT pcHwndNeeded)
{
	NTSTATUS ntStat = DllExchange.dNtUserBuildHwndList(hdesk, hwndNext, fEnumChildren, idThread, cHwndMax, phwndFirst, pcHwndNeeded);

	if (NT_SUCCESS(ntStat) && pcHwndNeeded != 0 && phwndFirst != 0)
	{
		FilterHwndList(phwndFirst, pcHwndNeeded);
	}

	return ntStat;
}

void FilterObjects(POBJECT_TYPES_INFORMATION pObjectTypes)
{
    POBJECT_TYPE_INFORMATION pObject = pObjectTypes->TypeInformation;
    for (ULONG i = 0; i < pObjectTypes->NumberOfTypes; i++)
    {
        FilterObject(pObject);

        pObject = (POBJECT_TYPE_INFORMATION)(((PCHAR)(pObject + 1) + ALIGN_UP(pObject->TypeName.MaximumLength, ULONG_PTR)));
    }
}

void FilterObject(POBJECT_TYPE_INFORMATION pObject)
{
    const WCHAR strDebugObject[] = L"DebugObject";

    if (pObject->TypeName.Length == (sizeof(strDebugObject)-sizeof(WCHAR)))
    {
        if (!memcmp(strDebugObject, pObject->TypeName.Buffer, pObject->TypeName.Length))
        {
            pObject->TotalNumberOfObjects = 0;
            pObject->TotalNumberOfHandles = 0;
        }
    }

}

void FilterProcess(PSYSTEM_PROCESS_INFORMATION pInfo)
{
    PSYSTEM_PROCESS_INFORMATION pPrev = pInfo;

    while (TRUE)
    {
		if (IsProcessBad(&pInfo->ImageName) || ((DllExchange.EnableProtectProcessId == TRUE) && (pInfo->UniqueProcessId == (HANDLE)DllExchange.dwProtectedProcessId)))
        {
            ZeroMemory(pInfo->ImageName.Buffer, pInfo->ImageName.Length);

            if (pInfo->NextEntryOffset == 0) //last element
            {
                pPrev->NextEntryOffset = 0;
            }
            else
            {
                pPrev->NextEntryOffset += pInfo->NextEntryOffset;
            }
        }
        else
        {
            pPrev = pInfo;
        }

        if (pInfo->NextEntryOffset == 0)
        {
            break;
        }
        else
        {
            pInfo = (PSYSTEM_PROCESS_INFORMATION)((DWORD_PTR)pInfo + pInfo->NextEntryOffset);
        }
    }
}