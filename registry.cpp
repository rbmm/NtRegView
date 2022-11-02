// Wobj.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

_NT_BEGIN

#include "../inc/initterm.h"
#include "../winZ/app.h"
#include "../winz/Frame.h"
#include "../winz/split.h"
#include "resource.h"
#define AFX_IDW_STATUS_BAR 0xE801 
void ShowSD(HANDLE hObject, PCWSTR caption, HWND hwndMain, HFONT hFont);

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

#define IDM_COPY_TIME	0x80000000
#define IDM_COPY_NAME	0x80000001
#define IDM_SSD			0x80000002

#define ID_TV 1
#define ID_LV 2

volatile UCHAR guz;

void SetStringToClipboard(HWND hwnd, PCWSTR lpsz, ULONG cb)
{
	if (OpenClipboard(hwnd))
	{
		EmptyClipboard();
		if (HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, cb))
		{
			memcpy(GlobalLock(hg), lpsz, cb);
			GlobalUnlock(hg);
			if (!SetClipboardData(CF_UNICODETEXT, hg)) GlobalFree(hg);
		}
		CloseClipboard();
	}
}

HIMAGELIST CreateIL(HINSTANCE hInstance, int cx, int cy, UINT flags, DWORD n, WORD pID[], HICON* phi)
{
	HIMAGELIST himl = ImageList_Create(cx, cy, flags, n, 0);

	if (himl)
	{
		do 
		{
			if (HICON hi = (HICON)LoadImage(hInstance, MAKEINTRESOURCE(*pID++), IMAGE_ICON, cx, cy, 0))
			{
				*phi = hi;
				ImageList_ReplaceIcon(himl, -1, hi);
			}
		} while (++phi, --n);
	}

	return himl;
}

#include "../inc/rtlframe.h"

struct _DeleteTag{};

typedef RTL_FRAME<_DeleteTag> DeleteTag;

struct REG_NODE : UNICODE_STRING
{
	LARGE_INTEGER _LastWriteTime;
	REG_NODE* _parent;
	HANDLE _hKey;
	NTSTATUS _status;
	LONG _SubKeys, _Index;
	USHORT _level;
	BOOLEAN _bTryOpen, _bQuryInfo, _bVolatile, _bLink;

	REG_NODE(REG_NODE* parent)
	{
		_parent = parent;
		_hKey = 0;
		Buffer = 0;
		Length = 0;
		MaximumLength = 0;
		_SubKeys = -1;
		_bTryOpen = TRUE;
		_bQuryInfo = TRUE;
		_bVolatile = FALSE;
		_bLink = FALSE;
		_status = 0;
		_level = parent ? parent->_level + 1 : 0;
	}

	~REG_NODE()
	{
		if (_hKey)
		{
			ZwClose(_hKey);
		}
		RtlFreeUnicodeString(this);
	}

	NTSTATUS Create(PCUNICODE_STRING Name)
	{
		return RtlDuplicateUnicodeString(RTL_DUPLICATE_UNICODE_STRING_NULL_TERMINATE, Name, this);
	}

	BOOL ExpandNode(HWND hwndTV, HTREEITEM hParent)
	{
		LONG n = _SubKeys;

		if (0 >= n)
		{
			return TRUE;
		}

		TVINSERTSTRUCT tv;
		tv.hParent = hParent;
		tv.hInsertAfter = TVI_FIRST;
		tv.item.mask = TVIF_TEXT|TVIF_PARAM|TVIF_CHILDREN|TVIF_IMAGE|TVIF_SELECTEDIMAGE;
		tv.item.cChildren = I_CHILDRENCALLBACK;
		tv.item.pszText = LPSTR_TEXTCALLBACK;
		tv.item.iImage =  I_IMAGECALLBACK;
		tv.item.iSelectedImage = I_IMAGECALLBACK;

		do 
		{
			if (REG_NODE* p = new REG_NODE(this))
			{
				p->_Index = --n;
				tv.item.lParam = (LPARAM)p;
				if (!TreeView_InsertItem(hwndTV, &tv))
				{
					delete p;
				}
			}
		} while (n);

		return FALSE;
	}

	NTSTATUS Open()
	{
		if (DeleteTag::get())
		{
			return STATUS_DELETE_PENDING;
		}

		if (_bTryOpen)
		{
			//DbgPrint("open %wZ\n", static_cast<PUNICODE_STRING>(this));
			_bTryOpen = FALSE;

			OBJECT_ATTRIBUTES oa = { sizeof(oa), _parent ? _parent->_hKey : 0, this, OBJ_CASE_INSENSITIVE|OBJ_OPENLINK };

			return ZwOpenKeyEx(&_hKey, KEY_READ, &oa, REG_OPTION_BACKUP_RESTORE);
		}

		return STATUS_SUCCESS;
	}

	void GetNodeText(LPTVITEM item)
	{
		ULONG r;
		if (item->mask & TVIF_CHILDREN)
		{
			if (0 > _SubKeys)
			{
				_SubKeys = 0;

				if (0 <= (_status = Open()))
				{
					union {
						KEY_CONTROL_FLAGS_INFO_W7 kcf;
						KEY_FULL_INFORMATION kfi;
					};

					if (0 <= ZwQueryKey(_hKey, KeyFlagsInformation, &kcf, sizeof(kcf), &r))
					{
						if (kcf.ControlFlags[1] & KEY_CTRL_FL_W7_01__IS_VOLATILE)
						{
							_bVolatile = TRUE;
						}

						if (kcf.ControlFlags[1] & KEY_CTRL_FL_W7_01__SYM_LINK)
						{
							_bLink = TRUE;
						}
					}

					switch (_status = ZwQueryKey(_hKey, KeyFullInformation, &kfi, sizeof(kfi), &kfi.TitleIndex))
					{
					case STATUS_BUFFER_OVERFLOW:
					case STATUS_SUCCESS:
						_SubKeys = kfi.SubKeys;
						_LastWriteTime = kfi.LastWriteTime;
						_status = 0;
						break;
					}
				}
				else
				{
					_bVolatile = TRUE, _bLink = FALSE;
				}
			}

			item->cChildren = 0 < _SubKeys;
		}

		if (item->mask & (TVIF_IMAGE|TVIF_SELECTEDIMAGE))
		{
			r = _bLink ? 7 :(_bVolatile ? 8 : 6);// 
			item->iSelectedImage = r;
			item->iImage = r;
			
			//if (_bLink)
			//{
			//	item->state |= INDEXTOOVERLAYMASK(1);//INDEXTOSTATEIMAGEMASK(3);
			//	item->stateMask |= TVIS_OVERLAYMASK ;//TVIS_STATEIMAGEMASK ;
			//	item->mask |= TVIF_STATE;
			//}
		}

		if (item->mask & TVIF_TEXT)
		{
			if (_bQuryInfo)
			{
				_bQuryInfo = FALSE;

				PVOID stack = alloca(guz);
				ULONG cb = 0, rcb = sizeof(KEY_BASIC_INFORMATION) + 32*sizeof(WCHAR);

				union {
					PVOID buf;
					PKEY_BASIC_INFORMATION pkbi;
				};

				HANDLE hKey = _parent->_hKey;
				NTSTATUS status;

				do 
				{
					if (cb < rcb) cb = RtlPointerToOffset(buf = alloca(rcb - cb), stack);

					if (0 <= (status = ZwEnumerateKey(hKey, _Index, KeyBasicInformation, buf, cb, &rcb)))
					{
						UNICODE_STRING ObjectName;
						ObjectName.MaximumLength = ObjectName.Length = (USHORT)pkbi->NameLength;
						ObjectName.Buffer = pkbi->Name;

						Create(&ObjectName);
					}
				} while (status == STATUS_BUFFER_OVERFLOW);

			}

			if (!Length)
			{
				return ;
			}

			PWSTR pszText = item->pszText;
			int cchTextMax = item->cchTextMax;

			if (!cchTextMax)
			{
				return ;
			}
			pszText[--cchTextMax] = 0;

			_snwprintf(pszText, cchTextMax, L"%wZ", static_cast<PUNICODE_STRING>(this));
		}
	}

	static NTSTATUS AddRoot(HWND hwndTV)
	{
		if (REG_NODE* p = new REG_NODE(0))
		{
			p->_bQuryInfo = FALSE;
			STATIC_UNICODE_STRING(sus, "\\REGISTRY");
			NTSTATUS status = p->Create(&sus);
			if (0 <= status)
			{
				TVINSERTSTRUCT tv;
				tv.hParent = TVI_ROOT;
				tv.hInsertAfter = TVI_LAST;
				tv.item.mask = TVIF_TEXT|TVIF_PARAM|TVIF_CHILDREN|TVIF_IMAGE|TVIF_SELECTEDIMAGE;
				tv.item.cChildren = I_CHILDRENCALLBACK;
				tv.item.pszText = LPSTR_TEXTCALLBACK;
				tv.item.lParam = (LPARAM)p;
				tv.item.iImage =  6;
				tv.item.iSelectedImage = 6;

				if (TreeView_InsertItem(hwndTV, &tv))
				{
					return STATUS_SUCCESS;
				}
			}
			delete p;
			return status;
		}
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	LONG QueryName(PWSTR& Buf, LONG& Len)
	{
		LONG NameLen = 0, _NameLen = Length >> 1;
		if (_parent) 
		{
			NameLen = _parent->QueryName(Buf, Len) + 1;
			if (1 <= Len) memcpy(Buf, L"\\", sizeof WCHAR);
			Buf++, Len--;
		}
		if (_NameLen <= Len) memcpy(Buf, Buffer, Length);
		Buf += _NameLen, Len -= _NameLen;
		return NameLen + _NameLen;
	}

	LONG get_FullName(PWSTR lpsz, LONG cb)
	{
		LONG NameLength = QueryName(lpsz, cb >>= 1);
		if (0 < cb) *lpsz = 0;
		return (NameLength << 1) + sizeof WCHAR;
	}
};


void DoCollapse(HWND hwnd, HTREEITEM hItem)
{
	DeleteTag tag;
	
	if (hItem = TreeView_GetChild(hwnd, hItem))
	{
		do 
		{
			HTREEITEM _hItem = hItem;
			hItem = TreeView_GetNextSibling(hwnd, hItem);
			TreeView_DeleteItem(hwnd, _hItem);

		} while (hItem);
	}
}

void SetTipText(HWND hwnd, int iItem, PWSTR pszText, UINT cchTextMax)
{
	if (!cchTextMax)
	{
		return ;
	}
	
	*pszText = 0;

	WCHAR buf[0x400];
	LVITEM item = { LVIF_TEXT, iItem, 2 };
	item.cchTextMax = RTL_NUMBER_OF(buf);
	item.pszText = buf;

	if (ListView_GetItem(hwnd, &item) && *item.pszText == '@')
	{
		item.mask = LVIF_IMAGE;
		item.iSubItem = 0;
		if (ListView_GetItem(hwnd, &item) && item.iImage == 3)
		{
			if (SHLoadIndirectString(buf, pszText, cchTextMax, 0) != S_OK)
			{
				*pszText = 0;
			}
		}
	}
}

class MySplit : public ZSplitWnd
{
	HICON m_hi[16];
	HWND _hwndLV, _hwndTV;
	HIMAGELIST _himl;
	REG_NODE* m_item;
	HFONT _hFont;
	int m_nSelColum;

	void DisplayNode(HWND hwnd, REG_NODE* item);

	virtual HWND CreateChild(BOOL lt, HWND hwndParent, int x, int y, int nWidth, int nHeight)
	{
		HWND hwnd = 0;
		HFONT hfont = ZGLOBALS::getFont()->getStatusFont();

		if (lt)
		{
			if (hwnd = CreateWindowExW(0, WC_TREEVIEW, 0, WS_CHILD|WS_BORDER|WS_VISIBLE|TVS_SHOWSELALWAYS|
				TVS_LINESATROOT|TVS_HASLINES|TVS_HASBUTTONS|TVS_DISABLEDRAGDROP|
				TVS_EDITLABELS, x, y, nWidth, nHeight, hwndParent, (HMENU)ID_TV, 0, 0))
			{
				_hwndTV = hwnd;

				if (_himl)
				{
					ImageList_SetOverlayImage(_himl, 7, 1);
					TreeView_SetImageList(hwnd, _himl, TVSIL_NORMAL);
					TreeView_SetImageList(hwnd, _himl, TVSIL_STATE);
				}

				REG_NODE::AddRoot(hwnd);
			}
		}
		else
		{
			if (hwnd = CreateWindowExW(0, WC_LISTVIEW, 0, 
				WS_VISIBLE|WS_CHILD|WS_BORDER|LVS_EDITLABELS|LVS_REPORT|
				LVS_SHOWSELALWAYS|LVS_SHAREIMAGELISTS|LVS_SINGLESEL|WS_HSCROLL|WS_VSCROLL,
				x, y, nWidth, nHeight, hwndParent, (HMENU)ID_LV, 0, 0))
			{
				_hwndLV = hwnd;

				LV_COLUMN lvclmn = { LVCF_TEXT | LVCF_WIDTH };
				lvclmn.cx = (nWidth >> 1) - (nWidth >> 3);
				lvclmn.pszText = L" Name ";
				ListView_InsertColumn(hwnd, lvclmn.iSubItem = 0, &lvclmn);
				lvclmn.cx = nWidth >> 3;
				lvclmn.pszText = L" Type ";
				ListView_InsertColumn(hwnd, lvclmn.iSubItem = 1, &lvclmn);
				lvclmn.cx = nWidth >> 1;
				lvclmn.pszText = L" Value ";
				ListView_InsertColumn(hwnd, lvclmn.iSubItem = 2, &lvclmn);

				ListView_SetExtendedListViewStyle(hwnd, LVS_EX_FULLROWSELECT|LVS_EX_HEADERDRAGDROP|LVS_EX_LABELTIP|LVS_EX_DOUBLEBUFFER|LVS_EX_INFOTIP);

				if (_himl)
				{
					ListView_SetImageList(hwnd, _himl, LVSIL_SMALL);
				}
			}
		}

		if (hwnd)
		{
			SetWindowTheme(hwnd, L"Explorer", 0);
			if (hfont)
			{
				SendMessage(hwnd, WM_SETFONT, (WPARAM)hfont, 0);
			}
		}
		return hwnd;
	}

	~MySplit()
	{
		int i = RTL_NUMBER_OF(m_hi);
		do 
		{
			if (m_hi[--i]) DestroyIcon(m_hi[i]);
		} while (i);

		if (_himl)
		{
			ImageList_Destroy(_himl);
		}
	}

	void DeleteContents()
	{
	}

	void OnCreate()
	{
		ULONG m;
		RtlGetNtVersionNumbers(&m, 0, 0);

		NONCLIENTMETRICS ncm = { m < 6 ? sizeof(NONCLIENTMETRICS) - 4 : sizeof(NONCLIENTMETRICS) };
		if (SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
		{
			ncm.lfCaptionFont.lfHeight = -ncm.iMenuHeight;
			ncm.lfCaptionFont.lfWeight = FW_NORMAL;
			ncm.lfCaptionFont.lfQuality = CLEARTYPE_QUALITY;
			ncm.lfCaptionFont.lfPitchAndFamily = FIXED_PITCH|FF_MODERN;
			wcscpy(ncm.lfCaptionFont.lfFaceName, L"Courier New");

			_hFont = CreateFontIndirect(&ncm.lfCaptionFont);
		}
	}

	void OnRclickTv(HWND hwnd, HWND hwntTV)
	{
		POINT pt;
		TVHITTESTINFO ht;
		GetCursorPos(&pt);
		ht.pt = pt;
		ScreenToClient(hwntTV, &ht.pt);

		TVITEM item = { TVIF_PARAM, TreeView_HitTest(hwntTV, &ht)};
		
		if (item.hItem && (ht.flags & TVHT_ONITEM))
		{
			if (TreeView_GetItem(hwntTV, &item) && item.lParam)
			{
				if (HMENU hmenu = CreatePopupMenu())
				{
					MENUITEMINFO mii = { sizeof(mii), MIIM_ID|MIIM_STRING, 0, 0, IDM_SSD, 0, 0, 0, 0, L"SecurityDescriptor"};
					InsertMenuItem(hmenu, 0, TRUE, &mii);
					mii.wID = IDM_COPY_NAME;
					mii.dwTypeData = L"Copy";
					InsertMenuItem(hmenu, 0, TRUE, &mii);

					ULONG cmd = TrackPopupMenu(hmenu, TPM_NONOTIFY|TPM_RETURNCMD , pt.x, pt.y, 0, hwnd, 0);
					DestroyMenu(hmenu);
					switch (cmd)
					{
					case IDM_SSD:
					case IDM_COPY_NAME:
						OnMenuCmd(hwnd, (REG_NODE*)item.lParam, cmd);
						break;
					}
				}
			}
		}
	}

	void OnMenuCmd(HWND hwnd, REG_NODE* item, WPARAM wParam)
	{
		if (!item)
		{
			return;
		}

		ULONG cb;
		PWSTR name;

		if (wParam == IDM_COPY_TIME)
		{
			name = (PWSTR)alloca(128);
			TIME_FIELDS tf;
			RtlTimeToTimeFields(&item->_LastWriteTime, &tf);
			SetStringToClipboard(hwnd, name, 
				(1 + swprintf(name, L"%u-%02u-%02u %02u:%02u:%02u", tf.Year, tf.Month, tf.Day, tf.Hour, tf.Minute, tf.Second)) << 1);
			return;
		}

		cb = item->get_FullName(0, 0);
		name = (PWSTR)alloca(cb);
		item->get_FullName(name, cb);

		switch (wParam)
		{
		case IDM_COPY_NAME:
			SetStringToClipboard(hwnd, name, cb);
			return ;
		case IDM_SSD:
			if (item->_hKey)
			{
				ShowSD(item->_hKey, name, ZGLOBALS::getMainHWND(), _hFont);
			}
			break;
		}

	}
	virtual LRESULT WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		union {
			UINT mask;
			int i;
		};

		switch (uMsg)
		{
		case WM_CREATE:
			OnCreate();
			break;
		case WM_DESTROY:
			DeleteContents();
			if (_hFont)
			{
				DeleteObject(_hFont);
			}
			break;
		case WM_NOTIFY:
			switch (((NMHDR*)lParam)->code)
			{
			case NM_RCLICK:
				switch (((NMHDR*)lParam)->idFrom)
				{
				case ID_TV:
					OnRclickTv(hwnd, ((NMHDR*)lParam)->hwndFrom);
					break;
				}
				break;
			case TVN_ITEMEXPANDING:
				switch (((LPNMTREEVIEW)lParam)->action)
				{
				case TVE_EXPAND:
					return ((REG_NODE*)((LPNMTREEVIEW)lParam)->itemNew.lParam)->ExpandNode(
						_hwndTV, ((LPNMTREEVIEW)lParam)->itemNew.hItem);
				}
				return FALSE;

			case TVN_ITEMEXPANDED:
				switch (((LPNMTREEVIEW)lParam)->action)
				{
				case TVE_COLLAPSE:
					DoCollapse(_hwndTV, ((LPNMTREEVIEW)lParam)->itemNew.hItem);
					break;
				}
				return FALSE;

			case TVN_DELETEITEM:
				if (lParam = ((LPNMTREEVIEW)lParam)->itemOld.lParam)
				{
					delete (REG_NODE*)lParam;
				}
				break;

			case TVN_GETDISPINFO:
				((REG_NODE*)((LPNMTVDISPINFO) lParam )->item.lParam)->GetNodeText(&((LPNMTVDISPINFO) lParam )->item);
				break;

			case TVN_SELCHANGED:
				DisplayNode(_hwndLV, (REG_NODE*)(((LPNMTREEVIEW)lParam)->itemNew.lParam));
				break;

			case LVN_GETDISPINFO:
				wParam = ((NMLVDISPINFO*)lParam)->item.lParam;
				mask = ((NMLVDISPINFO*)lParam)->item.mask;
				break;

			case LVN_COLUMNCLICK:
				i = ((LPNMLISTVIEW)lParam)->iSubItem;
				break;

			case LVN_GETINFOTIP:
				{
					NMLVGETINFOTIP* p = (LPNMLVGETINFOTIP)lParam;
					SetTipText(p->hdr.hwndFrom, p->iItem, p->pszText, p->cchTextMax);
				}
				break;

			case LVN_ITEMCHANGED:
				if ((((LPNMLISTVIEW)lParam)->uNewState & LVIS_SELECTED) && !(((LPNMLISTVIEW)lParam)->uOldState & LVIS_SELECTED))
				{
					//if (!m_item)
					//{
					//	pWnd->EnableCmd(ID_COPY, TRUE);
					//}
					//if (m_hash != item->hash)
					//{
					//	SendMessage(pWnd->ZStatusBar::getHWND(), SB_SETICON, (WPARAM)-1, (LPARAM)m_hi[GetImageIndexByHash(m_hash = item->hash)]);
					//}
				}
				break;
			}
			break;
		case WM_COMMAND:
			if (!lParam)
			{
				OnMenuCmd(hwnd, m_item, wParam);
			}
			break;
		}
		return ZSplitWnd::WindowProc(hwnd, uMsg, wParam, lParam);
	}

public:

	void Refresh()
	{
		DeleteContents();

		ZSDIFrameWnd* pWnd = ZGLOBALS::getMainFrame();
		pWnd->SetStatusText(0, L"");
		pWnd->SetStatusText(1, L"");
	}

	MySplit(int t) : ZSplitWnd(true, t, (HBRUSH)(1+COLOR_WINDOW))
	{
		_hFont = 0;
		m_item = 0;
		m_nSelColum = -1;
		RtlZeroMemory(m_hi, sizeof(m_hi));
	
		static WORD id[] = { 101, 102, 103, 104, 105, 106, 107, 108, 109 };
		
		_himl = CreateIL((HINSTANCE)&__ImageBase,
			GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 
			ILC_COLOR32|ILC_MASK, RTL_NUMBER_OF(id), id, m_hi);
	}
};

void MySplit::DisplayNode(HWND hwnd, REG_NODE* item)
{
	m_item = item;

	HANDLE hKey = item->_hKey;

	ListView_DeleteAllItems(hwnd);

	NTSTATUS status;
	PVOID stack = alloca(sizeof(WCHAR));
	ULONG cb = 0, rcb = sizeof(KEY_VALUE_FULL_INFORMATION)+0x32;
	union {
		PVOID buf;
		PWSTR text;
		PKEY_VALUE_FULL_INFORMATION pkvfi;
	};
	WCHAR raw[0x61];
	ULONG Index = 0;
	LVITEM lv = { LVIF_TEXT|LVIF_IMAGE };
	lv.iItem = MAXLONG;

	if (!hKey)
	{
		goto __status;
	}

	do 
	{
		do 
		{
			if (cb < rcb)
			{
				cb = RtlPointerToOffset(buf = alloca(rcb - cb), stack);
			}

			if (0 <= (status = ZwEnumerateValueKey(hKey, Index, KeyValueFullInformation, buf, cb, &rcb)))
			{
				lv.pszText = pkvfi->Name;
				PWSTR sz = (PWSTR)RtlOffsetToPointer(lv.pszText, pkvfi->NameLength);
				WCHAR c = *sz;
				*sz = 0;

				switch(pkvfi->Type)
				{
				case REG_SZ:
				case REG_EXPAND_SZ:
				case REG_MULTI_SZ:
					lv.iImage = 3;
					break;
				case REG_LINK:
					lv.iImage = 7;
					break;
				case REG_DWORD:
				case REG_QWORD:
					lv.iImage = 4;
					break;
				default: 					
					lv.iImage = 2;
				}

				lv.iItem = ListView_InsertItem(hwnd, &lv);

				*sz = c;

				if (0 <= (lv.iItem))
				{
					lv.iSubItem = 1;

					switch(pkvfi->Type)
					{
					case REG_NONE:
						lv.pszText = L"REG_NONE";
						break;
					case REG_SZ:
						lv.pszText = L"REG_SZ";
						break;
					case REG_EXPAND_SZ:
						lv.pszText = L"REG_EXPAND_SZ";
						break;
					case REG_MULTI_SZ:
						lv.pszText = L"REG_MULTI_SZ";
						break;
					case REG_BINARY:
						lv.pszText = L"REG_BINARY";
						break;
					case REG_LINK:
						lv.pszText = L"REG_LINK";
						break;
					case REG_DWORD:
						lv.pszText = L"REG_DWORD";
						break;
					case REG_QWORD:
						lv.pszText = L"REG_QWORD";
						break;
					default:
						WCHAR sztype[9];
						swprintf(lv.pszText = sztype, L"%x", pkvfi->Type);
					}
					ListView_SetItem(hwnd, &lv);

					if (ULONG DataLength = pkvfi->DataLength)
					{
						lv.iSubItem = 2;
						PBYTE data = (PBYTE)RtlOffsetToPointer(pkvfi, pkvfi->DataOffset);
						*(PWSTR)(data + DataLength) = 0;

						switch (pkvfi->Type)
						{
						case REG_QWORD:
							if (DataLength == sizeof(ULONGLONG))
							{
								swprintf(raw, L"0x%016I64x", *(PULONGLONG)data);
								lv.pszText = raw;
								break;
							}
							goto __default;
						case REG_DWORD:
							if (DataLength == sizeof(ULONG))
							{
								swprintf(raw, L"0x%08x", *(PULONG)data);
								lv.pszText = raw;
								break;
							}
							goto __default;
						case REG_LINK:
						case REG_MULTI_SZ:
						case REG_SZ:
						case REG_EXPAND_SZ:
							lv.pszText = (PWSTR)data;
							break;
						default:
__default:
							lv.pszText = 0;

							if (DataLength)
							{
								lv.pszText = raw;

								if (DataLength > 0x20)
								{
									DataLength = 0x20;
								}

								do 
								{
									lv.pszText += swprintf(lv.pszText, L"%02x ", *data++);
								} while (--DataLength);

								lv.pszText = raw;
							}
						}
						if (lv.pszText) ListView_SetItem(hwnd, &lv);
					}

					lv.iSubItem = 0;
				}
			}

		} while (status == STATUS_BUFFER_OVERFLOW);
		Index++;
	} while (status != STATUS_NO_MORE_ENTRIES);
__status:
	rcb = item->get_FullName(text, cb);
	if (cb < rcb)
	{
		buf = alloca(rcb - cb);
		item->get_FullName(text, rcb);
	}
	ZSDIFrameWnd* pWnd = ZGLOBALS::getMainFrame();

	if (0 > item->_status)
	{
		swprintf(raw, L"%x", item->_status);
	}
	else
	{
		TIME_FIELDS tf;
		RtlTimeToTimeFields(&item->_LastWriteTime, &tf);
		swprintf(raw, L"%u-%02u-%02u %02u:%02u:%02u", tf.Year, tf.Month, tf.Day, tf.Hour, tf.Minute, tf.Second);
	}
	pWnd->SetStatusText(0, raw);
	pWnd->SetStatusText(1, text);

	hwnd = pWnd->ZStatusBar::getHWND();
	SendMessage(hwnd, SB_SETICON, 0, (LPARAM)m_hi[0]);
	SendMessage(hwnd, SB_SETICON, 1, (LPARAM)m_hi[item->_bVolatile ? 8 : 6]);
}

class ZMainWnd : public ZSDIFrameWnd
{
	HWND _hwnd;

	virtual PCUNICODE_STRING getPosName()
	{
		STATIC_UNICODE_STRING_(MainWnd);
		return &MainWnd;
	}

	virtual BOOL CreateClient(HWND hwnd, int x, int y, int nWidth, int nHeight)
	{
		if (MySplit* p = new MySplit(nWidth>>2))
		{
			_hwnd = p->Create(0, 0, WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN, x, y, nWidth, nHeight, hwnd, 0, 0);
			p->Release();
		}

		return _hwnd != 0;
	}

	virtual BOOL CreateSB(HWND hwnd)
	{
		if (hwnd = ZStatusBar::Create(hwnd))
		{
			TEXTMETRICW tm;
			if (HDC hdc = GetDC(hwnd))
			{
				if (GetTextMetrics(hdc, &tm) )
				{
					int pp[] = { tm.tmAveCharWidth * sizeof(" 2000-00-00 00:00:00") + GetSystemMetrics(SM_CXSMICON), -1 };
					SetParts(pp, RTL_NUMBER_OF(pp));
				}
				ReleaseDC(hwnd, hdc);
			}
			return TRUE;
		}
		return FALSE;
	}

	//BOOL ZMainWnd::CreateTB(HWND hwnd)
	//{
	//	static TBBUTTON g_btns[] = {
	//		{IMAGE_BITMAP, ID_REFRESH, TBSTATE_ENABLED, BTNS_AUTOSIZE, {}, (DWORD_PTR)L" Refresh ", -1},
	//		{IMAGE_BITMAP, ID_GRID, TBSTATE_ENABLED, BTNS_AUTOSIZE, {}, (DWORD_PTR)L" Display Grid ", -1},
	//		{IMAGE_BITMAP, ID_COPY, 0, BTNS_AUTOSIZE, {}, (DWORD_PTR)L" Copy Name ", -1},
	//		{IMAGE_BITMAP, ID_TOGGLE, 0, BTNS_AUTOSIZE, {}, (DWORD_PTR)L" Display Full path", -1},
	//	};

	//	return ZToolBar::Create(hwnd, (HINSTANCE)&__ImageBase, 0, 0, 16, 16, g_btns, RTL_NUMBER_OF(g_btns), TRUE) != 0;
	//}

	virtual LRESULT WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		switch (uMsg)
		{
		case WM_NOTIFY:
			if (AFX_IDW_STATUS_BAR == ((NMHDR*)lParam)->idFrom)
			{
				switch (((NMHDR*)lParam)->code)
				{
				case NM_RCLICK:
				case NM_CLICK:
					if (HMENU hmenu = CreatePopupMenu())
					{
						UINT wIDCopy = IDM_COPY_TIME;
						MENUITEMINFO mii = { sizeof(mii), MIIM_ID|MIIM_STRING };
						switch (((NMMOUSE*)lParam)->dwItemSpec)
						{
						case 1:
							mii.wID = IDM_SSD;
							mii.dwTypeData = L"SecurityDescriptor";
							InsertMenuItem(hmenu, 0, TRUE, &mii);
							wIDCopy = IDM_COPY_NAME;
						case 0:
							mii.wID = wIDCopy;
							mii.dwTypeData = L"Copy";
							InsertMenuItem(hmenu, 0, TRUE, &mii);
							break;
						}

						ClientToScreen(((NMHDR*)lParam)->hwndFrom, &((LPNMMOUSE)lParam)->pt);
						TrackPopupMenu(hmenu, TPM_NONOTIFY, 
							((LPNMMOUSE)lParam)->pt.x, ((LPNMMOUSE)lParam)->pt.y, 0, _hwnd, 0);

						DestroyMenu(hmenu);
					}

					break;
				}
			}
			break;
		}
		return ZSDIFrameWnd::WindowProc(hwnd, uMsg, wParam, lParam);
	}
public:
	ZMainWnd()
	{
		_hwnd = 0;
	}
};

void zmain()
{
	ZGLOBALS globals;
	ZApp app;
	ZRegistry reg;
	ZFont font(TRUE);

	if (0 <= reg.Create(L"Software\\{A6537AEC-ABE6-4637-8194-361DCAB08001}"))
	{
		HWND hwnd = 0;

		if (ZMainWnd* p = new ZMainWnd)
		{
			hwnd = p->ZSDIFrameWnd::Create(L"NtRegView", (HINSTANCE)&__ImageBase, MAKEINTRESOURCE(1), TRUE);// && font.Init()

			p->Release();
		}

		if (hwnd)
		{
			app.Run();
		}
	}
}

BOOL AdjustPrivileges()
{
	BOOL f = FALSE;
	HANDLE hToken;
	if (0 <= NtOpenProcessToken(NtCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &hToken))
	{
		BEGIN_PRIVILEGES(tp, 2)
			LAA(SE_RESTORE_PRIVILEGE),
			LAA(SE_BACKUP_PRIVILEGE),
		END_PRIVILEGES	
		f = !NtAdjustPrivilegesToken(hToken, FALSE, const_cast<PTOKEN_PRIVILEGES>(&tp), 0, 0, 0);
		NtClose(hToken);
	}
	return f;
}

void epR(void*)
{
	initterm();

	AdjustPrivileges();

	zmain();

	destroyterm(); 

	ExitProcess(0);
}

_NT_END
