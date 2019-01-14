#include "StdAfx.h"

_NT_BEGIN

extern volatile UCHAR guz;

PWSTR DumpAcl(PWSTR lpsz, PACL acl, PCWSTR caption)
{
	lpsz += swprintf(lpsz, caption);

	if (!acl)
	{
		return lpsz + swprintf(lpsz, L"NULL\r\n");
	}

	USHORT AceCount = acl->AceCount;

	if (!AceCount)
	{
		return lpsz + swprintf(lpsz, L"empty\r\n");
	}

	lpsz += swprintf(lpsz, L"T FL AcessMsK Sid\r\n");

	union {
		PVOID pv;
		PBYTE pb;
		PACCESS_ALLOWED_ACE pah;
	};

	pv = acl + 1;

	char sz[16], sz2[16];

	do
	{
		UNICODE_STRING us;
		if (0 <= RtlConvertSidToUnicodeString(&us, (PSID)&pah->SidStart, TRUE))
		{
			WCHAR name[256], DomainName[128];
			ULONG cch = RTL_NUMBER_OF(name);
			::SID_NAME_USE snu;
			DWORD cchReferencedDomainName = RTL_NUMBER_OF(DomainName);

			if (!LookupAccountSidW(0, (PSID)&pah->SidStart, name, &cch, DomainName, &cchReferencedDomainName, &snu))
			{
				name[0]=0;//\r\n
			}

			ACCESS_MASK Mask = pah->Mask;
			sprintf(sz2, "%08X", Mask);

			switch (pah->Header.AceType)
			{
			case ACCESS_ALLOWED_ACE_TYPE:
				sz[0] = 'A', sz[1] = 0;
				break;
			case ACCESS_DENIED_ACE_TYPE:
				sz[0] = 'D', sz[1] = 0;
				break;
			case SYSTEM_MANDATORY_LABEL_ACE_TYPE:
				sz[0] = 'L', sz[1] = 0;
				sz2[0] = Mask & SYSTEM_MANDATORY_LABEL_NO_READ_UP ? 'R' : ' ';
				sz2[1] = Mask & SYSTEM_MANDATORY_LABEL_NO_WRITE_UP ? 'W' : ' ';
				sz2[2] = Mask & SYSTEM_MANDATORY_LABEL_NO_EXECUTE_UP ? 'E' : ' ';
				sz2[3] = 0;
				break;
			default:
				sprintf(sz, "%u", pah->Header.AceType);
			}
			lpsz += swprintf(lpsz, L"%S %02X %S %wZ '%s'\r\n", sz, pah->Header.AceFlags, sz2, &us, name);
			RtlFreeUnicodeString(&us);
		}

		pb += pah->Header.AceSize;

	} while (--AceCount);

	return lpsz;
}

PCSTR GetSidNameUseName(::SID_NAME_USE snu)
{
	switch (snu)
	{
	case SidTypeUser: return "User";
	case SidTypeGroup: return "Group";
	case SidTypeDomain: return "Domain";
	case SidTypeAlias: return "Alias";
	case SidTypeWellKnownGroup: return "WellKnownGroup";
	case SidTypeDeletedAccount: return "DeletedAccount";
	case SidTypeInvalid: return "Invalid";
	case SidTypeUnknown: return "Unknown";
	case SidTypeComputer: return "Computer";
	case SidTypeLabel: return "Label";
	case SidTypeLogonSession: return "LogonSession";
	}
	return "?";
}

PWSTR DumpObjectSecurity(PWSTR lpsz, HANDLE hObject)
{
	ULONG cb = 0, rcb = 64;

	PVOID stack = alloca(guz);

	union {
		PVOID buf;
		PSECURITY_DESCRIPTOR psd;
	};

#if 0
	if (GetObjPointer(hObject, buf))
	{
		lpsz += swprintf(lpsz, L"Pointer: %p\r\n", buf);
	}
#endif

	NTSTATUS status;
	do 
	{
		if (cb < rcb)
		{
			cb = RtlPointerToOffset(buf = alloca(rcb - cb), stack);
		}

		if (0 <= (status = ZwQuerySecurityObject(hObject, DACL_SECURITY_INFORMATION|LABEL_SECURITY_INFORMATION|
			OWNER_SECURITY_INFORMATION, psd, cb, &rcb)))
		{
			//DbgPrint()
			PACL Acl;
			BOOLEAN bPresent, bDefault;
			if (0 <= RtlGetDaclSecurityDescriptor(psd, &bPresent, &Acl, &bDefault))
			{
				lpsz = DumpAcl(lpsz, bPresent ? Acl : 0, L"DACL:\r\n");
			}

			if (0 <= RtlGetSaclSecurityDescriptor(psd, &bPresent, &Acl, &bDefault))
			{
				lpsz = DumpAcl(lpsz, bPresent ? Acl : 0, L"LABEL:\r\n");
			}

			PSID Owner;
			if (0 <= RtlGetOwnerSecurityDescriptor(psd, &Owner, &bDefault) && Owner)
			{
				UNICODE_STRING us;
				::SID_NAME_USE snu;
				WCHAR name[256], DomainName[128];
				ULONG cch, cchReferencedDomainName;

				if (0 <= RtlConvertSidToUnicodeString(&us, Owner, TRUE))
				{
					cch = RTL_NUMBER_OF(name);
					cchReferencedDomainName = RTL_NUMBER_OF(DomainName);

					if (!LookupAccountSidW(NULL, Owner, name, &cch, DomainName, &cchReferencedDomainName, &snu))
					{
						*name = 0;
						*DomainName = 0;
					}
					lpsz += swprintf(lpsz, L"Owner: %wZ '%s' @ '%s' [%S]\r\n", &us, name, DomainName, GetSidNameUseName(snu));
					RtlFreeUnicodeString(&us);
				}

			}
			break;
		}

	} while (status == STATUS_BUFFER_TOO_SMALL);

	return lpsz;
}

void ShowSD(HANDLE hObject, PCWSTR caption, HWND hwndMain, HFONT hFont)
{
	if (HWND hwnd = CreateWindowExW(0, WC_EDIT, caption, WS_OVERLAPPEDWINDOW|WS_VSCROLL|ES_MULTILINE,
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hwndMain, 0, 0, 0))
	{
		SendMessage(hwnd, WM_SETFONT, (WPARAM)hFont, 0);

		PVOID BaseAddress = 0;
		SIZE_T ViewSize = 0x10000;
		if (0 <= ZwAllocateVirtualMemory(NtCurrentProcess(), &BaseAddress, 0, &ViewSize, MEM_COMMIT, PAGE_READWRITE))
		{
			__try {
				DumpObjectSecurity((PWSTR)BaseAddress, hObject);
				SetWindowTextW(hwnd, (PWSTR)BaseAddress);
			}
			__except(EXCEPTION_EXECUTE_HANDLER){

			}
			ZwFreeVirtualMemory(NtCurrentProcess(), &BaseAddress, &ViewSize,  MEM_RELEASE);
		}

		ShowWindow(hwnd, SW_SHOWNORMAL);
	}
}

_NT_END