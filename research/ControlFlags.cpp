#include "StdAfx.h"

_NT_BEGIN

struct HEAP_STACK 
{
	PVOID _buf;
	ULONG_PTR _ptr;
	ULONG _cb;

	HEAP_STACK()
	{
		_buf = 0;
	}

	~HEAP_STACK()
	{
		if (_buf)
		{
			LocalFree(_buf);
		}
	}

	BOOL Create(ULONG cb)
	{
		if (_buf = LocalAlloc(0, cb))
		{
			_cb = cb, _ptr = (ULONG_PTR)RtlOffsetToPointer(_buf, cb);
			return TRUE;
		}

		return FALSE;
	}

	PVOID getStack()
	{
		return (PVOID)_ptr;
	}

	PVOID alloc(ULONG cb)
	{
		cb += 0xf;
		cb &= ~0xf;

		if (_cb < cb)
		{
			RtlRaiseStatus(STATUS_STACK_BUFFER_OVERRUN);
		}

		_cb -= cb, _ptr -= cb;
		return (PVOID)_ptr;
	}

	void free(PVOID pv)
	{
		_cb = RtlPointerToOffset(_buf, pv);
		_ptr = (ULONG_PTR)pv;
	}
};

struct OBJECT_ATTRIBUTES_EX : OBJECT_ATTRIBUTES
{
	UNICODE_STRING _ObjectName;
	OBJECT_ATTRIBUTES_EX* _prev;

	OBJECT_ATTRIBUTES_EX(OBJECT_ATTRIBUTES_EX* prev, HANDLE hRoot = 0)
	{
		_prev = prev;
		Length = sizeof(OBJECT_ATTRIBUTES);
		RootDirectory = hRoot;
		ObjectName = &_ObjectName;
		Attributes = OBJ_CASE_INSENSITIVE|OBJ_OPENLINK;
		SecurityDescriptor = 0; 
		SecurityQualityOfService = 0;
	}

	PWSTR GetName(PWSTR sz)
	{
		if (_prev)
		{
			*(sz = _prev->GetName(sz))++ = OBJ_NAME_PATH_SEPARATOR;
		}

		if (_ObjectName.Length)
		{
			sz += swprintf(sz, L"%wZ", &_ObjectName);
		}

		return sz;
	}

	USHORT GetNameLength()
	{
		USHORT len = _ObjectName.Length;

		if (OBJECT_ATTRIBUTES_EX* p = _prev)
		{
			do 
			{
				len += p->_ObjectName.Length + sizeof(WCHAR);
			} while (p = p->_prev);
		}

		return len;
	}
};

class REG_WALK2 : HEAP_STACK, GENERIC_MAPPING
{
public:
	HANDLE _hToken;
	ULONGLONG _time;

	void Run(OBJECT_ATTRIBUTES_EX* prev, LONG Deep);

	NTSTATUS Init();

	void Print(OBJECT_ATTRIBUTES_EX* poaex, PCCH Format, ...)
	{
		UNICODE_STRING ObjName = { 
			poaex->GetNameLength(), 
			ObjName.Length, 
			(PWSTR)alloc(ObjName.Length + sizeof(WCHAR))
		};

		poaex->GetName(ObjName.Buffer);

		vDbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_INFO_LEVEL, Format, (va_list)(&Format + 1));
		DbgPrint("\"%wZ\"\n", &ObjName);

		free(ObjName.Buffer);
	}

	REG_WALK2()
	{
		_hToken = 0;
	}

	~REG_WALK2()
	{
		if (_hToken) NtClose(_hToken);
	}
};

struct KEY_CONTROL_FLAGS_INFO_W7  // KeyFlagsInformation for Win7
{
	ULONG ControlFlags[3];
};

// ControlFlags[0] - KEY_CTRL_FL_W7_00__XXX
// ... Undock

// ControlFlags[1] - KEY_CTRL_FL_W7_01__XXX
#define KEY_CTRL_FL_W7_01__IS_VOLATILE                                 0x01
#define KEY_CTRL_FL_W7_01__SYM_LINK                                    0x02

// ControlFlags[2] - KEY_CTRL_FL_W7_02__XXX
// ... Undock
NTDLL ZwAccessCheck 	( 	_In_ PSECURITY_DESCRIPTOR  	SecurityDescriptor,
						 _In_ HANDLE  	ClientToken,
						 _In_ ACCESS_MASK  	DesiredAccess,
						 _In_ PGENERIC_MAPPING  	GenericMapping,
						 _Inout_ PPRIVILEGE_SET  	PrivilegeSet,
						 _Inout_ PULONG  	PrivilegeSetLength,
						 _Out_ PACCESS_MASK  	GrantedAccess,
						 _Out_ PNTSTATUS  	AccessStatus 
						 );

void REG_WALK2::Run(OBJECT_ATTRIBUTES_EX* prev, LONG Deep)
{
	ULONG cb = 0, rcb = sizeof(KEY_BASIC_INFORMATION) + 128 * sizeof(WCHAR), r;

	union {
		PVOID buf;
		PKEY_BASIC_INFORMATION pkbi;
		PKEY_VALUE_PARTIAL_INFORMATION pkvpi;
		KEY_CONTROL_FLAGS_INFO_W7 kcf;
		PSECURITY_DESCRIPTOR SecurityDescriptor;
	};

	PVOID stack = getStack();

	NTSTATUS status;

	OBJECT_ATTRIBUTES_EX cn(prev);

	if (0 <= (status = ZwOpenKeyEx(&cn.RootDirectory, KEY_READ, prev, REG_OPTION_BACKUP_RESTORE)))
	{
		Deep--;
#if 0
		do 
		{
			if (cb < rcb) cb = RtlPointerToOffset(buf = alloc(rcb - cb), stack);

			if (0 <= (status = ZwQuerySecurityObject(cn.RootDirectory, 
				DACL_SECURITY_INFORMATION|LABEL_SECURITY_INFORMATION|OWNER_SECURITY_INFORMATION|GROUP_SECURITY_INFORMATION, 
				SecurityDescriptor, cb, &rcb)))
			{
				NTSTATUS AccessStatus;
				ACCESS_MASK GrantedAccess;
				PRIVILEGE_SET PrivilegeSet;
				DWORD dwPrivSetSize = sizeof( PRIVILEGE_SET );

				//de
				if (0 > ZwAccessCheck(SecurityDescriptor, _hToken, KEY_SET_VALUE, this, &PrivilegeSet, &dwPrivSetSize, &GrantedAccess, &AccessStatus))
				{
					__debugbreak();
				}

				if (0 <= AccessStatus)
				{
					Print(prev);

					NtClose(cn.RootDirectory);
					return;
				}
			}
			
		} while (status == STATUS_BUFFER_TOO_SMALL);
#endif

#if 1
		BOOL bNew = _bittestandreset(&Deep, 31);

		if (0 <= ZwQueryKey(cn.RootDirectory, KeyFlagsInformation, &kcf, sizeof(kcf), &r))
		{
			static ULONG _ControlMask[] = { 
				0,
				KEY_CTRL_FL_W7_01__IS_VOLATILE|KEY_CTRL_FL_W7_01__SYM_LINK,
				0
			};

			ULONG n = 3;
			BOOLEAN bAddMask = FALSE;
			do 
			{
				--n;
				if (kcf.ControlFlags[n] & ~_ControlMask[n])
				{
					_ControlMask[n] |= kcf.ControlFlags[n];
					bAddMask = TRUE;
				}
			} while (n);

			if (bAddMask)
			{
				Print(prev, "[ControlFlags]: %08x.%08x.%08x ", 
					kcf.ControlFlags[0], kcf.ControlFlags[1], kcf.ControlFlags[2]);
			}

			if (kcf.ControlFlags[1] & KEY_CTRL_FL_W7_01__IS_VOLATILE)
			{
				Print(prev, "[Volatile]: %x ", bNew);
			}

			if (kcf.ControlFlags[1] & KEY_CTRL_FL_W7_01__SYM_LINK)
			{
				STATIC_UNICODE_STRING_(SymbolicLinkValue);
				do 
				{
					if (cb < rcb) cb = RtlPointerToOffset(buf = alloc(rcb - cb), stack);

					if (0 <= (status = ZwQueryValueKey(cn.RootDirectory, &SymbolicLinkValue, KeyValuePartialInformation, buf, cb, &rcb)))
					{
						if (pkvpi->Type == REG_LINK)
						{
							ULONG DataLength = pkvpi->DataLength;

							if (DataLength && !(DataLength & 1) && DataLength < MAXUSHORT)
							{
								UNICODE_STRING RemaingName = {
									(USHORT)DataLength, (USHORT)DataLength, (PWSTR)pkvpi->Data
								};

								static UNICODE_STRING empty;

								if (*RemaingName.Buffer == OBJ_NAME_PATH_SEPARATOR)
								{
									Print(prev, "[SYM_LINK]: [%x] <%wZ> <- ", 
										kcf.ControlFlags[1] & KEY_CTRL_FL_W7_01__IS_VOLATILE,
										&RemaingName);
								}
								else
								{
									__debugbreak();
								}

							}
							goto __link;
						}
						else
						{
							Print(prev, "[Type != REG_LINK]: 08x ", pkvpi->Type);
						}
					}
				} while (status == STATUS_BUFFER_OVERFLOW);

				if (0 > status)
				{
					Print(prev, "[QueryValue]: %08x ", status);
				}
			}
		}
		else
		{
			Print(prev, "[QueryKey]: %08x ", status);
		}
#endif//0
		ULONGLONG time = _time;

		ULONG index = 0;
		do 
		{
			do 
			{
				if (cb < rcb) cb = RtlPointerToOffset(buf = alloc(rcb - cb), stack);

				if (0 <= (status = ZwEnumerateKey(cn.RootDirectory, index, KeyBasicInformation, buf, cb, &rcb)))
				{
					cn._ObjectName.Buffer = pkbi->Name;
					cn._ObjectName.MaximumLength = cn._ObjectName.Length = (USHORT)pkbi->NameLength;

					if ((ULONGLONG)pkbi->LastWriteTime.QuadPart - (ULONGLONG)time < 20000000)
					{
						_bittestandset(&Deep, 31);
						//Print(&cn);
					}

					if (Deep) Run(&cn, Deep);

					_bittestandreset(&Deep, 31);
				}
			} while (status == STATUS_BUFFER_OVERFLOW);

			index++;

		} while (status != STATUS_NO_MORE_ENTRIES && status != STATUS_INVALID_HANDLE);

__link:
		NtClose(cn.RootDirectory);
	}

	free(stack);
}

NTSTATUS REG_WALK2::Init()
{
	HANDLE hKey;
	STATIC_OBJECT_ATTRIBUTES(Registry, "\\REGISTRY");

	NTSTATUS status = ZwOpenKey(&hKey, KEY_READ, &Registry);

	if (0 <= status)
	{
		POBJECT_TYPE_INFORMATION poti = (POBJECT_TYPE_INFORMATION)alloca(sizeof(OBJECT_TYPE_INFORMATION)+32*sizeof(WCHAR));
		
		if (0 <= ZwQueryObject(hKey, ObjectTypeInformation, poti, sizeof(OBJECT_TYPE_INFORMATION)+32*sizeof(WCHAR), 0))
		{
			memcpy(static_cast<GENERIC_MAPPING*>(this), &poti->GenericMapping, sizeof(GENERIC_MAPPING));
		}

		KEY_BASIC_INFORMATION kbi;
		switch (status = ZwQueryKey(hKey, KeyBasicInformation, &kbi, sizeof(kbi), &kbi.TitleIndex))
		{
		case STATUS_BUFFER_OVERFLOW:
			status = STATUS_SUCCESS;
		case STATUS_SUCCESS:
			_time = kbi.LastWriteTime.QuadPart;
			break;
		}

		ULONG r;
		UCHAR buf[64];
		ZwQueryKey(hKey, KeyFlagsInformation, buf, sizeof(buf), &r);
		NtClose(hKey);
	}

	HANDLE hToken;
	if (0 <= (status = NtOpenProcessToken(NtCurrentProcess(), TOKEN_QUERY|TOKEN_DUPLICATE, &hToken)))
	{
		_hToken = hToken;
		TOKEN_LINKED_TOKEN tlt;
		ULONG cb;
		if (0 <= NtQueryInformationToken(hToken, TokenLinkedToken, &tlt, sizeof(tlt), &cb))
		{
			_hToken = tlt.LinkedToken;
			NtClose(hToken);
		}
	}

	if (0 <= status)
	{
		if (Create(0x8000))//
		{
			return STATUS_SUCCESS;
		}

		return STATUS_INSUFFICIENT_RESOURCES;
	}

	return status;
}

void printTime(PLARGE_INTEGER time)
{
	TIME_FIELDS tf;
	RtlTimeToTimeFields(time, &tf);
	DbgPrint("%u-%02u-%02u %02u:%02u:%02u\n", tf.Year, tf.Month, tf.Day, tf.Hour, tf.Minute, tf.Second);
}

void DoTest0()
{
	BOOLEAN b;
	RtlAdjustPrivilege(SE_BACKUP_PRIVILEGE, TRUE, FALSE, &b);
	RtlAdjustPrivilege(SE_RESTORE_PRIVILEGE, TRUE, FALSE, &b);
	OBJECT_ATTRIBUTES_EX cn(0,0);
	REG_WALK2 rw;

	if (0 <= rw.Init())
	{
		DbgPrint("boot time: ");//\\machine\\system\\machine\\software
		printTime((PLARGE_INTEGER)&rw._time);

		RtlInitUnicodeString(&cn._ObjectName, L"\\registry");

		//ULONG t = GetTickCount();

		__try 
		{
			rw.Run(&cn, MAXUCHAR);
		}
		__except(EXCEPTION_EXECUTE_HANDLER)
		{
		}

	}
}
_NT_END