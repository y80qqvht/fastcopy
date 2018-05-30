﻿static char *install_id = 
	"@(#)Copyright (C) 2005-2018 H.Shirouzu		install.cpp	Ver3.50";
/* ========================================================================
	Project  Name			: Installer for FastCopy
	Module Name				: Installer Application Class
	Create					: 2005-02-02(Wed)
	Update					: 2018-05-28(Mon)
	Copyright				: H.Shirouzu
	License					: GNU General Public License version 3
	======================================================================== */

#include "../tlib/tlib.h"
#include "stdio.h"
#include "resource.h"
#include "install.h"
#include "../../external/zlib/zlib.h"
#include "../version.h"
#include <regex>

using namespace std;

WCHAR *current_shell = TIsWow64() ? CURRENT_SHEXTDLL_EX : CURRENT_SHEXTDLL;

#define SETUPFILES	FASTCOPY_EXE, INSTALL_EXE, CURRENT_SHEXTDLL, CURRENT_SHEXTDLL_EX, \
					README_TXT, README_ENG_TXT, GPL_TXT, XXHASH_TXT, HELP_CHM
WCHAR *SetupFiles [] = { SETUPFILES };
WCHAR *SetupFilesEx [] = { SETUPFILES,
	FASTCOPY_INI, FASTCOPY_LINK, /*FASTCOPY_LOG, FASTCOPY_LOGDIR,*/
};

BOOL ConvertToX86Dir(WCHAR *target);
BOOL ConvertVirtualStoreConf(WCHAR *execDir, WCHAR *userDir, WCHAR *virtualDir);

#define TEMPDIR_OPT	L"/TEMPDIR"
#define RUNAS_OPT	L"/runas="
#define UNINST_OPT	L"/r"

int ExecInTempDir();

/*
	WinMain
*/
int WINAPI WinMain(HINSTANCE hI, HINSTANCE, LPSTR cmdLine, int nCmdShow)
{
	::SetDllDirectory("");
	::SetCurrentDirectoryW(TGetExeDirW());

	TLibInit();
//	TSetDefaultLCID(0x409); // for English Dialog Test

	if (!TSetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32)) {
		TLoadLibraryExW(L"iertutil.dll", TLT_SYSDIR);
		TLoadLibraryExW(L"cryptbase.dll", TLT_SYSDIR);
		TLoadLibraryExW(L"urlmon.dll", TLT_SYSDIR);
		TLoadLibraryExW(L"cscapi.dll", TLT_SYSDIR);

		WCHAR	*cmdLineW = ::GetCommandLineW();
		if (!wcsstr(cmdLineW, TEMPDIR_OPT) &&
			!wcsstr(cmdLineW, RUNAS_OPT)   &&
			!wcsstr(cmdLineW, UNINST_OPT)) {
			return	ExecInTempDir();
		}
	}

	return	TInstApp(hI, cmdLine, nCmdShow).Run();
}

int ExecInTempDir()
{
// テンポラリディレクトリ作成
	WCHAR	temp[MAX_PATH] = L"";
	if (::GetTempPathW(wsizeof(temp), temp) == 0) {
		return	-1;
	}

	WCHAR	dir[MAX_PATH] = L"";
	int	dlen = MakePathW(dir, temp, L"");	// 末尾に \\ が無ければ追加

	while (1) {
		int64	dat;
		TGenRandomMT(&dat, sizeof(dat));
		snwprintfz(dir + dlen, wsizeof(dir) - dlen, L"fcinst-%llx", dat);
		if (::CreateDirectoryW(dir, NULL)) {
			break;
		}
		if (GetLastError() != ERROR_ALREADY_EXISTS) {
			return -1;
		}
	}

// テンポラリディレクトリにインストーラをコピー
	WCHAR	orgFile[MAX_PATH] = L"";
	::GetModuleFileNameW(NULL, orgFile, wsizeof(orgFile));

	WCHAR	dstFile[MAX_PATH];
	MakePathW(dstFile, dir, wcsrchr(orgFile, '\\') + 1);

	if (!::CopyFileW(orgFile, dstFile, TRUE)) {
		TMessageBoxW(dstFile, LoadStrW(IDS_NOTCREATEFILE), MB_OK);
		return	-1;
	}

// テンポラリディレクトリのインストーラを実行
	WCHAR	cmdline[MAX_PATH * 2] = L"";
	snwprintfz(cmdline, wsizeof(cmdline), L"\"%s\" ", dstFile);

	int		argc = 0;
	WCHAR	**argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
	for (int i=1; i < argc; i++) {
		wcsncatz(cmdline, argv[i], wsizeof(cmdline));
		wcsncatz(cmdline, L" ", wsizeof(cmdline));
	}
	wcsncatz(cmdline, TEMPDIR_OPT, wsizeof(cmdline));

	STARTUPINFOW		sui = { sizeof(sui) };
	PROCESS_INFORMATION pi = {};

	if (!::CreateProcessW(dstFile, cmdline, 0, 0, 0, 0, 0, dir, &sui, &pi)) {
		return	-1;
	}

	::WaitForSingleObject(pi.hProcess, INFINITE);
	::CloseHandle(pi.hThread);
	::CloseHandle(pi.hProcess);

// テンポラリインストーラ終了後に、テンポラリファイル＆ディレクトリを削除
	for (int i=0; i < 100; i++) {
		if (::GetFileAttributesW(dstFile) != 0xffffffff) {
			::DeleteFileW(dstFile);
		}
		if (::GetFileAttributesW(dir) != 0xffffffff) {
			::RemoveDirectoryW(dir);
		}
		else {
			break;
		}
		Sleep(500);
	}

	return 0;
}

/*
	インストールアプリケーションクラス
*/
TInstApp::TInstApp(HINSTANCE _hI, LPSTR _cmdLine, int _nCmdShow) : TApp(_hI, _cmdLine, _nCmdShow)
{
}

TInstApp::~TInstApp()
{
}

void TInstApp::InitWindow(void)
{
/*	if (IsWinNT() && TIsWow64()) {
		DWORD	val = 0;
		TWow64DisableWow64FsRedirection(&val);
	}
*/
	if (TIsLowIntegrity()) {
		TMessageBox(LoadStr(IDS_LOWINTEGRITY), "FastCopy");
		WCHAR path[MAX_PATH];
		::GetModuleFileNameW(NULL, path, wsizeof(path));
		TOpenExplorerSelOneW(path);
		TApp::Exit(-1);
		return;
	}

	TDlg *maindlg = new TInstDlg(cmdLine);
	mainWnd = maindlg;
	maindlg->Create();
}


/*
	メインダイアログクラス
*/
TInstDlg::TInstDlg(char *cmdLine) : TDlg(INSTALL_DIALOG),
	staticText(this), extractBtn(this), startBtn(this)
{
	cfg.mode = strcmp(cmdLine, "/r") ? SETUP_MODE : UNINSTALL_MODE;
	cfg.programLink	= TRUE;
	cfg.desktopLink	= TRUE;

	cfg.hOrgWnd   = NULL;
	cfg.runImme   = FALSE;
	cfg.appData   = NULL;
	cfg.setupDir  = NULL;
	cfg.startMenu = NULL;
	cfg.deskTop   = NULL;
	cfg.virtualDir= NULL;

	if (::IsUserAnAdmin()) {
		WCHAR	*p = wcsstr((WCHAR *)GetCommandLineW(), RUNAS_OPT);

		if (p) {
			if (p && (p = wcstok(p+7,  L",")))  cfg.hOrgWnd	= (HWND)(LONG_PTR)wcstoull(p, 0, 16);
			if (p && (p = wcstok(NULL, L",")))  cfg.mode	= (InstMode)wcstoul(p, 0, 10);
			if (p && (p = wcstok(NULL, L",")))  cfg.runImme	= wcstoul(p, 0, 10);
			if (p && (p = wcstok(NULL, L",")))  cfg.programLink	= wcstoul(p, 0, 10);
			if (p && (p = wcstok(NULL, L",")))  cfg.desktopLink	= wcstoul(p, 0, 10);
			if (p && (p = wcstok(NULL, L"\""))) cfg.startMenu = p; if (p) p = wcstok(NULL, L"\"");
			if (p && (p = wcstok(NULL, L"\""))) cfg.deskTop   = p; if (p) p = wcstok(NULL, L"\"");
			if (p && (p = wcstok(NULL, L"\""))) cfg.setupDir  = p; if (p) p = wcstok(NULL, L"\"");
			if (p && (p = wcstok(NULL, L"\""))) cfg.appData   = p; if (p) p = wcstok(NULL, L"\"");
			if (p && (p = wcstok(NULL, L"\""))) cfg.virtualDir= p;
			if (p) {
				::ShowWindow(cfg.hOrgWnd, SW_HIDE);
			}
			else {
				cfg.runImme = FALSE;
				TApp::Exit(-1);
			}
		}
	}
}

TInstDlg::~TInstDlg()
{
}

BOOL GetShortcutPath(InstallCfg *cfg)
{
// スタートメニュー＆デスクトップに登録
	TRegistry	reg(HKEY_CURRENT_USER);
	if (reg.OpenKey(REGSTR_SHELLFOLDERS)) {
		WCHAR	buf[MAX_PATH] = L"";

		reg.GetStrMW(REGSTR_PROGRAMS, buf, sizeof(buf)); // wsizeofではない
		cfg->startMenu = wcsdup(buf);

		reg.GetStrMW(REGSTR_DESKTOP,  buf, sizeof(buf));
		cfg->deskTop   = wcsdup(buf);

		reg.CloseKey();
		return	TRUE;
	}
	return	FALSE;
}

/*
	メインダイアログ用 WM_INITDIALOG 処理ルーチン
*/
BOOL TInstDlg::EvCreate(LPARAM lParam)
{
	GetWindowRect(&rect);
	int		cx = ::GetSystemMetrics(SM_CXFULLSCREEN);
	int		cy = ::GetSystemMetrics(SM_CYFULLSCREEN);
	int		xsize = rect.right - rect.left;
	int		ysize = rect.bottom - rect.top;

	::SetClassLong(hWnd, GCL_HICON,
					(LONG_PTR)::LoadIcon(TApp::GetInstance(), (LPCSTR)SETUP_ICON));
	MoveWindow((cx - xsize)/2, (cy - ysize)/2, xsize, ysize, TRUE);
	Show();

	TChangeWindowMessageFilter(WM_CLOSE, 1);

	extractBtn.AttachWnd(GetDlgItem(EXTRACT_BTN));
	extractBtn.CreateTipWnd(LoadStrW(IDS_EXTRACT));

	startBtn.AttachWnd(GetDlgItem(IDOK));
	startBtn.CreateTipWnd(LoadStrW(IDS_START));

	GetIPDictBySelf(&ipDict);
	ipDict.get_str(VER_KEY, &ver);

	SetWindowTextU8(Fmt("FastCopy Setup %s %s", GetVersionStr(),
#ifdef _WIN64
		"(x64)"
#else
		""
#endif
	));

	// プロパティシートの生成
	staticText.AttachWnd(GetDlgItem(INSTALL_STATIC));
	propertySheet = new TInstSheet(this, &cfg);

// 現在ディレクトリ設定
	WCHAR	buf[MAX_PATH] = L"";
	WCHAR	setupDir[MAX_PATH] = L"";

// タイトル設定
	if (IsWinVista() && ::IsUserAnAdmin()) {
		GetWindowTextW(buf, wsizeof(buf));
		wcscat(buf, L" (Admin)");
		SetWindowTextW(buf);
	}

// 既にセットアップされている場合は、セットアップディレクトリを読み出す
	TRegistry	reg(HSTOOLS_STR);
	if (reg.OpenKeyW(FASTCOPY)) {
		if (reg.GetStrW(L"Path", setupDir, sizeof(setupDir))) {
			cfg.setuped = setupDir;
		}
		reg.CloseKey();
	}

// Program Filesのパス取り出し
	if (!*setupDir) {
		if (::SHGetSpecialFolderPathW(NULL, buf, CSIDL_PROFILE, FALSE)) {
			MakePathW(setupDir, buf, FASTCOPY);
		}
		else {
			TRegistry	reg(HKEY_LOCAL_MACHINE, BY_MBCS);
			if (reg.OpenKey(REGSTR_PATH_SETUP)) {
				if (reg.GetStrMW(REGSTR_PROGRAMFILES, buf, sizeof(buf)))
					MakePathW(setupDir, buf, FASTCOPY);
				reg.CloseKey();
			}
		}
	}

	if (!cfg.startMenu || !cfg.deskTop) {
		GetShortcutPath(&cfg);
	}

	SetDlgItemTextW(FILE_EDIT, cfg.setupDir ? cfg.setupDir : setupDir);

	CheckDlgButton(cfg.mode == SETUP_MODE ? SETUP_RADIO : UNINSTALL_RADIO, 1);
	ChangeMode();

	if (cfg.runImme) {
		PostMessage(WM_COMMAND, MAKEWORD(IDOK, 0), 0);
	}

	return	TRUE;
}

BOOL TInstDlg::EvNcDestroy(void)
{
	return	TRUE;
}

/*
	メインダイアログ用 WM_COMMAND 処理ルーチン
*/
BOOL TInstDlg::EvCommand(WORD wNotifyCode, WORD wID, LPARAM hwndCtl)
{
	switch (wID) {
	case IDOK:
		propertySheet->GetData();
		if (cfg.mode == UNINSTALL_MODE) {
			UnInstall();
		}
		else {
			Install();
		}
		return	TRUE;

	case IDCANCEL:
		Exit(0);
		return	TRUE;

	case FILE_BUTTON:
		BrowseDirDlg(this, FILE_EDIT, L"Select Install Directory");
		return	TRUE;

	case EXTRACT_BTN:
		Extract();
		return	TRUE;

	case SETUP_RADIO:
	case UNINSTALL_RADIO:
		if (wNotifyCode == BN_CLICKED) {
			ChangeMode();
		}
		return	TRUE;
	}
	return	FALSE;
}

void TInstDlg::Extract(void)
{
	if (ipDict.key_num() == 0 || ver.Len() == 0) return;

	WCHAR	wDir[MAX_PATH] = L"C:\\";
	::SHGetSpecialFolderPathW(0, wDir, CSIDL_DESKTOPDIRECTORY, FALSE);
	SetDlgItemTextW(EXTRACT_EDIT, wDir);

	if (!BrowseDirDlg(this, EXTRACT_EDIT, LoadStrW(IDS_EXTRACTDIR))) { return; }

	if (!GetDlgItemTextW(EXTRACT_EDIT, wDir, wsizeof(wDir))) return;

	AddPathW(wDir, FmtW(L"FastCopy%s%s", U8toWs(ver.s()),
#ifdef _WIN64
			L"_x64"
#else
			L""
#endif
		));
	::CreateDirectoryW(wDir, 0);

	for (auto &fname : SetupFiles) {
		WCHAR	wPath[MAX_PATH];
		BOOL	is_rotate = FALSE;
		
		MakePathW(wPath, wDir, fname);

		if (!IPDictCopy(&ipDict, fname, wPath, &is_rotate)) {
			MessageBoxW(wPath, LoadStrW(IDS_NOTCREATEFILE));
			return;
		}
	}
	::ShellExecuteW(0, NULL, wDir, NULL, NULL, SW_SHOW);
	Exit(0);
}

void TInstDlg::ChangeMode(void)
{
	if (!propertySheet) return;

	cfg.mode = IsDlgButtonChecked(SETUP_RADIO) ? SETUP_MODE : UNINSTALL_MODE;
	::EnableWindow(GetDlgItem(FILE_EDIT), cfg.mode == SETUP_MODE);
	extractBtn.Show((cfg.mode == SETUP_MODE && ipDict.key_num()) ? SW_SHOW : SW_HIDE);
	propertySheet->Paste();

	::EnableWindow(GetDlgItem(IDOK), cfg.mode == SETUP_MODE || cfg.setuped.Len());
}

BOOL IsSameFile(const WCHAR *src, const WCHAR *dst)
{
	WIN32_FIND_DATAW	src_dat;
	WIN32_FIND_DATAW	dst_dat;
	HANDLE				hFind;

	if ((hFind = ::FindFirstFileW(src, &src_dat)) == INVALID_HANDLE_VALUE)
		return	FALSE;
	::FindClose(hFind);

	if ((hFind = ::FindFirstFileW(dst, &dst_dat)) == INVALID_HANDLE_VALUE)
		return	FALSE;
	::FindClose(hFind);

	if (src_dat.nFileSizeLow != dst_dat.nFileSizeLow
	||  src_dat.nFileSizeHigh != dst_dat.nFileSizeHigh)
		return	FALSE;

	return
		(*(int64 *)&dst_dat.ftLastWriteTime == *(int64 *)&src_dat.ftLastWriteTime)
	||  ( (*(int64 *)&src_dat.ftLastWriteTime % 10000000) == 0 ||
		  (*(int64 *)&dst_dat.ftLastWriteTime % 10000000) == 0 )
	 &&	*(int64 *)&dst_dat.ftLastWriteTime + 20000000 >= *(int64 *)&src_dat.ftLastWriteTime
	 &&	*(int64 *)&dst_dat.ftLastWriteTime - 20000000 <= *(int64 *)&src_dat.ftLastWriteTime;
}

BOOL RotateFile(const WCHAR *path, int max_cnt, BOOL with_delete)
{
	BOOL	ret = TRUE;

	for (int i=max_cnt-1; i >= 0; i--) {
		WCHAR	src[MAX_PATH];
		WCHAR	dst[MAX_PATH];

		snwprintfz(src, wsizeof(src), (i == 0) ? L"%s" : L"%s.%d", path, i);
		snwprintfz(dst, wsizeof(dst), L"%s.%d", path, i+1);

		if (::GetFileAttributesW(src) == 0xffffffff) {
			continue;
		}
		if (::MoveFileExW(src, dst, MOVEFILE_REPLACE_EXISTING)) {
			if (with_delete) {
				::MoveFileExW(dst, NULL, MOVEFILE_DELAY_UNTIL_REBOOT); // require admin priv...
			}
		}
		else {
			ret = FALSE;
		}
	}
	return	ret;
}

BOOL CleanupRotateFile(const WCHAR *path, int max_cnt)
{
	BOOL	ret = TRUE;

	for (int i=max_cnt; i > 0; i--) {
		WCHAR	targ[MAX_PATH];

		snwprintfz(targ, wsizeof(targ), L"%s.%d", path, i);

		if (::GetFileAttributesW(targ) == 0xffffffff) {
			continue;
		}
		if (!::DeleteFileW(targ)) {
			ret = FALSE;
		}
	}
	return	ret;
}

BOOL MiniCopy(WCHAR *src, WCHAR *dst, BOOL *is_rotate=NULL)
{
	HANDLE	hSrc;
	HANDLE	hDst;
	BOOL	ret = FALSE;
	DWORD	srcSize = 0;
	DWORD	dstSize;

	if ((hSrc = ::CreateFileW(src, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0))
				== INVALID_HANDLE_VALUE) {
		DebugW(L"src(%s) open err(%x)\n", src, GetLastError());
		return	FALSE;
	}
	srcSize = ::GetFileSize(hSrc, 0);

#define MAX_ROTATE	10
	CleanupRotateFile(dst, MAX_ROTATE);
	hDst = ::CreateFileW(dst, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
	if (hDst == INVALID_HANDLE_VALUE) {
		RotateFile(dst, MAX_ROTATE, TRUE);
		if (is_rotate) {
			*is_rotate = TRUE;
		}
		hDst = ::CreateFileW(dst, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
	}

	if (hDst == INVALID_HANDLE_VALUE) {
		DebugW(L"dst(%s) open err(%x)\n", dst, GetLastError());
		return	FALSE;
	}
	if (HANDLE hMap = ::CreateFileMapping(hSrc, 0, PAGE_READONLY, 0, 0, 0)) {
		if (void *view = ::MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0)) {
			if (::WriteFile(hDst, view, srcSize, &dstSize, 0) && srcSize == dstSize) {
				FILETIME	ct;
				FILETIME	la;
				FILETIME	ft;

				if (::GetFileTime(hSrc, &ct, &la, &ft)) {
					::SetFileTime(hDst, &ct, &la, &ft);
				}
				ret = TRUE;
			}
			::UnmapViewOfFile(view);
		}
		::CloseHandle(hMap);
	}
	::CloseHandle(hDst);
	::CloseHandle(hSrc);

	return	ret;
}

BOOL TInstDlg::RunAsAdmin(BOOL is_imme)
{
	SHELLEXECUTEINFOW	sei = {0};
	WCHAR				buf[MAX_PATH * 2] = {};
	WCHAR				exe_path[MAX_PATH] = {};
	WCHAR				setup_path[MAX_PATH] = {};
	WCHAR				app_data[MAX_PATH] = {};
	WCHAR				virtual_store[MAX_PATH] = {};
	WCHAR				fastcopy_dir[MAX_PATH] = {};
	WCHAR				*fastcopy_dirname = NULL;
	int					len;

	::GetModuleFileNameW(NULL, exe_path, wsizeof(exe_path));

	if (!(len = GetDlgItemTextW(FILE_EDIT, setup_path, MAX_PATH))) return FALSE;
	if (setup_path[len-1] == '\\') setup_path[len-1] = 0;
	if (!::GetFullPathNameW(setup_path, MAX_PATH, fastcopy_dir, &fastcopy_dirname)) return FALSE;

	if (!::SHGetSpecialFolderPathW(NULL, buf, CSIDL_APPDATA, FALSE)) return FALSE;
	MakePathW(app_data, buf, fastcopy_dirname);

	wcscpy(buf, fastcopy_dir);
	if (cfg.mode == SETUP_MODE) {
#ifdef _WIN64
		ConvertToX86Dir(buf);
#endif
		if (!TMakeVirtualStorePathW(buf, virtual_store)) return FALSE;
	}
	else {
		wcscpy(virtual_store, L"dummy");
	}

	snwprintfz(buf, wsizeof(buf), L"/runas=%p,%d,%d,%d,%d,\"%s\",\"%s\",\"%s\",\"%s\",\"%s\"",
		hWnd, cfg.mode, is_imme, cfg.programLink, cfg.desktopLink,
		cfg.startMenu, cfg.deskTop,
		setup_path, app_data, virtual_store);

	sei.cbSize = sizeof(SHELLEXECUTEINFO);
	sei.lpVerb = L"runas";
	sei.lpFile = exe_path;
	sei.lpDirectory = NULL;
	sei.nShow = SW_NORMAL;
	sei.lpParameters = buf;

	EnableWindow(FALSE);
	BOOL ret = ::ShellExecuteExW(&sei);
	EnableWindow(TRUE);

	return	ret;
}

BOOL TInstDlg::Install(void)
{
	WCHAR	buf[MAX_PATH];
	WCHAR	setupDir[MAX_PATH];
	WCHAR	setupPath[MAX_PATH];
	BOOL	is_delay_copy = FALSE;
	BOOL	is_rotate = FALSE;
	int		len;

// インストールパス設定
	len = GetDlgItemTextW(FILE_EDIT, setupDir, wsizeof(setupDir));
	if (setupDir[len-1] == '\\') {
		setupDir[len-1] = 0;
	}

	if (IsWinVista() && TIsVirtualizedDirW(setupDir)) {
		if (!::IsUserAnAdmin()) {
			return	RunAsAdmin(TRUE);
		}
		else if (cfg.runImme && cfg.setupDir && lstrcmpiW(setupDir, cfg.setupDir)) {
			return	MessageBox(LoadStr(IDS_ADMINCHANGE)), FALSE;
		}
	}

	MakeDirAllW(setupDir);
	DWORD	attr = ::GetFileAttributesW(setupDir);

	if (attr == 0xffffffff || (attr & FILE_ATTRIBUTE_DIRECTORY) == 0)
		return	MessageBox(LoadStr(IDS_NOTCREATEDIR)), FALSE;
	MakePathW(setupPath, setupDir, FASTCOPY_EXE);

	if (MessageBoxW(LoadStrW(IDS_START), INSTALL_STR, MB_OKCANCEL|MB_ICONINFORMATION) != IDOK)
		return	FALSE;

// ファイルコピー
	if (cfg.mode == SETUP_MODE) {
		WCHAR	installPath[MAX_PATH];
		WCHAR	orgDir[MAX_PATH];
		BOOL	use_dict = ipDict.key_num() ? TRUE : FALSE;

		::GetModuleFileNameW(NULL, orgDir, wsizeof(orgDir));
		GetParentDirW(orgDir, orgDir);

		for (auto &fname: SetupFiles) {
			MakePathW(installPath, setupDir, fname);

			if (use_dict) {
				if (IPDictCopy(&ipDict, fname, installPath, &is_rotate) ||
					IsSameFileDict(&ipDict, fname, installPath))
					continue;
			}
			else {
				MakePathW(buf, orgDir, fname);

				if (MiniCopy(buf, installPath, &is_rotate) || IsSameFile(buf, installPath)) {
					continue;
				}
			}
			return	MessageBoxW(installPath, LoadStrW(IDS_NOTCREATEFILE)), FALSE;
		}
		TRegistry	reg(HSTOOLS_STR);
		if (reg.CreateKeyW(FASTCOPY)) {
			reg.SetStrW(L"Path", setupDir);
			reg.CloseKey();
		}
	}

// スタートメニュー＆デスクトップに登録
	WCHAR	*linkPath[]	= { cfg.startMenu, cfg.deskTop, NULL };
	BOOL	execFlg[]	= { cfg.programLink,  cfg.desktopLink };

	for (int i=0; linkPath[i]; i++) {
		auto	&fname = linkPath[i];
		wcscpy(buf, fname);
		if (i != 0 || RemoveSameLink(fname, buf) == FALSE) {
			swprintf(buf + wcslen(buf), L"\\%s", FASTCOPY_SHORTCUT);
		}
		if (execFlg[i]) {
			ShellLinkW(setupPath, buf);
		}
		else {
			DeleteLinkW(buf);
		}
	}

// レジストリにアンインストール情報を登録
	TRegistry	reg(HKEY_CURRENT_USER);
	if (reg.OpenKey(REGSTR_PATH_UNINSTALL)) {
		if (reg.CreateKeyW(FASTCOPY)) {
			reg.SetStrMW(REGSTR_VAL_UNINSTALLER_DISPLAYNAME, FASTCOPY);

			MakePathW(buf, setupDir, INSTALL_EXE);
			wcscat(buf, L" /r");
			reg.SetStrMW(REGSTR_VAL_UNINSTALLER_COMMANDLINE, buf);

			MakePathW(buf, setupDir, FASTCOPY_EXE);
			reg.SetStrMW(REGSTR_VAL_UNINSTALLER_DISPLAYICON, buf);

			reg.SetStr(REGSTR_VAL_UNINSTALLER_DISPLAYVER, GetVersionStr() + 3);
			reg.SetStr(REGSTR_VAL_UNINSTALLER_PUBLISHER, "H.Shirouzu");
			reg.SetInt(REGSTR_VAL_UNINSTALLER_ESTIMATESIZE, 1500); // KB
			reg.SetStr(REGSTR_VAL_UNINSTALLER_HELPLINK, LoadStrU8(IDS_HELPURL));
			reg.SetStr(REGSTR_VAL_UNINSTALLER_URLUPDATEINFO, LoadStrU8(IDS_URL));
			reg.SetStr(REGSTR_VAL_UNINSTALLER_URLINFOABOUT, LoadStrU8(IDS_SUPPORTBBS));
			reg.SetStr(REGSTR_VAL_UNINSTALLER_COMMENTS, "shirouzu@ipmsg.org");

			reg.CloseKey();
		}
		reg.CloseKey();
	}

	if (IsWinVista() && TIsVirtualizedDirW(setupDir)) {
		WCHAR	usr_path[MAX_PATH] = L"";
		WCHAR	tmp[MAX_PATH];
		WCHAR	*fastcopy_dirname = NULL;

		::GetFullPathNameW(setupDir, MAX_PATH, tmp, &fastcopy_dirname);

		if (cfg.appData) {
			wcscpy(usr_path, cfg.appData);
		}
		else {
			WCHAR	wbuf[MAX_PATH] = L"";
			::SHGetSpecialFolderPathW(NULL, wbuf, CSIDL_APPDATA, FALSE);
			MakePathW(usr_path, wbuf, fastcopy_dirname);
		}

		ConvertVirtualStoreConf(setupDir, usr_path, cfg.virtualDir);
	}

// コピーしたアプリケーションを起動
	const WCHAR *msg = LoadStrW(is_delay_copy ? IDS_DELAYSETUPCOMPLETE :
							  is_rotate ? IDS_REPLACECOMPLETE : IDS_SETUPCOMPLETE);
	int			flg = MB_OKCANCEL|MB_ICONINFORMATION;

	TLaunchDlg	dlg(msg, this);

	if (dlg.Exec() == IDOK) {
		::ShellExecuteW(NULL, L"open", setupPath, L"" /*L"/INSTALL"*/, setupDir, SW_SHOW);
	}

	Exit(0);

	return	TRUE;
}

/*
	Vista以降
*/
#ifdef _WIN64
BOOL ConvertToX86Dir(WCHAR *target)
{
	WCHAR	buf[MAX_PATH];
	WCHAR	buf86[MAX_PATH];
	ssize_t	len;

	if (!::SHGetSpecialFolderPathW(NULL, buf, CSIDL_PROGRAM_FILES, FALSE)) return FALSE;
	len = wcslen(buf);
	buf[len++] = '\\';
	buf[len]   = 0;

	if (wcsnicmp(buf, target, len)) return FALSE;

	if (!::SHGetSpecialFolderPathW(NULL, buf86, CSIDL_PROGRAM_FILESX86, FALSE)) return FALSE;
	MakePathW(buf, buf86, target + len);
	wcscpy(target, buf);

	return	 TRUE;
}
#endif

BOOL ConvertVirtualStoreConf(WCHAR *execDir, WCHAR *userDir, WCHAR *virtualDir)
{
#define FASTCOPY_INI_W			L"FastCopy.ini"
	WCHAR	buf[MAX_PATH];
	WCHAR	org_ini[MAX_PATH];
	WCHAR	usr_ini[MAX_PATH];
	BOOL	is_admin = ::IsUserAnAdmin();
	BOOL	is_exists;

	MakePathW(usr_ini, userDir, FASTCOPY_INI_W);
	MakePathW(org_ini, execDir, FASTCOPY_INI_W);

#ifdef _WIN64
	ConvertToX86Dir(org_ini);
#endif

	is_exists = ::GetFileAttributesW(usr_ini) != 0xffffffff;
	 if (!is_exists) {
		::CreateDirectoryW(userDir, NULL);
	}

	if (virtualDir && virtualDir[0]) {
		WCHAR	vs_ini[MAX_PATH];
		MakePathW(vs_ini,  virtualDir, FASTCOPY_INI_W);
		if (::GetFileAttributesW(vs_ini) != 0xffffffff) {
			if (!is_exists) {
				is_exists = ::CopyFileW(vs_ini, usr_ini, TRUE);
			}
			MakePathW(buf, userDir, L"to_OldDir(VirtualStore).lnk");
			ShellLinkW(virtualDir, buf);
			swprintf(buf, L"%s.obsolete", vs_ini);
			::MoveFileW(vs_ini, buf);
			if (::GetFileAttributesW(vs_ini) != 0xffffffff) {
				::DeleteFileW(vs_ini);
			}
		}
	}

	if ((is_admin || !is_exists) && ::GetFileAttributesW(org_ini) != 0xffffffff) {
		if (!is_exists) {
			::CopyFileW(org_ini, usr_ini, TRUE);
		}
		if (is_admin) {
			swprintf(buf, L"%s.obsolete", org_ini);
			::MoveFileW(org_ini, buf);
			if (::GetFileAttributesW(org_ini) != 0xffffffff) {
				::DeleteFileW(org_ini);
			}
		}
	}

	MakePathW(buf, userDir, L"to_ExeDir.lnk");
//	if (GetFileAttributesW(buf) == 0xffffffff) {
		ShellLinkW(execDir, buf);
//	}

	return	TRUE;
}

// シェル拡張を解除
enum ShellExtOpe { CHECK_SHELLEXT, UNREGISTER_SHELLEXT };

int ShellExtFunc(WCHAR *setup_dir, ShellExtOpe kind, BOOL isAdmin)
{
	WCHAR	buf[MAX_PATH];
	int		ret = FALSE;

	MakePathW(buf, setup_dir, CURRENT_SHEXTDLL);

	if (HMODULE hShellExtDll = TLoadLibraryW(buf)) {
		BOOL (WINAPI *IsRegisterDll)(BOOL) = (BOOL (WINAPI *)(BOOL))
			GetProcAddress(hShellExtDll, "IsRegistServer");
		HRESULT (WINAPI *UnRegisterDll)(void) = (HRESULT (WINAPI *)(void))
			GetProcAddress(hShellExtDll, "DllUnregisterServer");
		BOOL (WINAPI *SetAdminMode)(BOOL) = (BOOL (WINAPI *)(BOOL))
			GetProcAddress(hShellExtDll, "SetAdminMode");

		if (IsRegisterDll && UnRegisterDll && SetAdminMode) {
			switch (kind) {
			case CHECK_SHELLEXT:
				ret = IsRegisterDll(isAdmin);
				break;
			case UNREGISTER_SHELLEXT:
				SetAdminMode(isAdmin);
				ret = UnRegisterDll();
				break;
			}
		}
		::FreeLibrary(hShellExtDll);
	}
	return	ret;
}

BOOL IsKindOfFile(const WCHAR *org, const WCHAR *exists)
{
	WCHAR	reg_buf[MAX_PATH];

	swprintf(reg_buf, L"^(%s|%s\\..+)$", org, org);

	auto re = make_unique<wregex>(reg_buf, regex_constants::icase);

	wcmatch	wm;

	return	regex_search(exists, wm, *re);
}

// ユーザファイルが無いことの確認
BOOL ReadyToRmFolder(const WCHAR *dir)
{
	WCHAR				path[MAX_PATH];
	WIN32_FIND_DATAW	fdat;

	MakePathW(path, dir, L"*");

	HANDLE	fh = ::FindFirstFileW(path, &fdat);
	if (fh == INVALID_HANDLE_VALUE) {
		return	FALSE;
	}

	BOOL ret = TRUE;

	do {
		if (wcscmp(fdat.cFileName, L".") && wcscmp(fdat.cFileName, L"..")) {
			BOOL	found = FALSE;
			for (auto &fname: SetupFilesEx) {
				if (IsKindOfFile(fname, fdat.cFileName)) {
					MakePathW(path, dir, fdat.cFileName);
					if (!::DeleteFileW(path) &&
						wcscmp(fname, INSTALL_EXE) && wcscmp(fname, FASTCOPY_LOGDIR)) {
						ret = FALSE;
					}
					found = TRUE;
					break;
				}
			}
			if (!found) {
				ret = FALSE;
			}
		}
	} while (FindNextFileW(fh, &fdat));

	::FindClose(fh);

	return	ret;
}

BOOL TInstDlg::UnInstall(void)
{
	WCHAR	setupDir[MAX_PATH] = L"";

	TRegistry	reg(HSTOOLS_STR);
	if (reg.OpenKeyW(FASTCOPY)) {
		reg.GetStrW(L"Path", setupDir, sizeof(setupDir));
		reg.CloseKey();
	}
	if (!*setupDir) {
		::GetModuleFileNameW(NULL, setupDir, wsizeof(setupDir));
		GetParentDirW(setupDir, setupDir);
	}
	BOOL	is_shext = ShellExtFunc(setupDir, CHECK_SHELLEXT, TRUE);

	if ((is_shext || TIsVirtualizedDirW(setupDir)) && IsWinVista() && !::IsUserAnAdmin()) {
		RunAsAdmin(TRUE);
		return	TRUE;
	}

	if (MessageBoxW(LoadStrW(IDS_START), UNINSTALL_STR, MB_OKCANCEL | MB_ICONINFORMATION) != IDOK)
		return	FALSE;

	// スタートメニュー＆デスクトップから削除
	reg.ChangeTopKey(HKEY_CURRENT_USER);
	if (reg.OpenKey(REGSTR_SHELLFOLDERS)) {
		char	*regStr[] = { REGSTR_PROGRAMS, REGSTR_DESKTOP, NULL };

		for (int i=0; regStr[i]; i++) {
			auto	&reg_key = regStr[i];
			WCHAR	buf[MAX_PATH];

			if (reg.GetStrMW(reg_key, buf, sizeof(buf))) {
				if (i == 0) {
					RemoveSameLink(buf);
				}
				::swprintf(buf + wcslen(buf), L"\\%s", FASTCOPY_SHORTCUT);
				DeleteLinkW(buf);
			}
		}
		reg.CloseKey();
	}

	ShellExtFunc(setupDir, UNREGISTER_SHELLEXT, FALSE);
	if (::IsUserAnAdmin()) {
		ShellExtFunc(setupDir, UNREGISTER_SHELLEXT, TRUE);
	}

#ifdef _WIN64
	if (1) {
#else
	if (TIsWow64()) {
#endif
		SHELLEXECUTEINFOW	sei = { sizeof(sei) };
		WCHAR				arg[1024];

		swprintf(arg, L"\"%s\\%s\",%s", setupDir, CURRENT_SHEXTDLL_EX, L"DllUnregisterServer");
		sei.lpFile = L"rundll32.exe";
		sei.lpParameters = arg;
		::ShellExecuteExW(&sei);
	}

// レジストリからアンインストール情報を削除
	if (reg.OpenKey("Software")) {
		if (reg.OpenKey(HSTOOLS_STR)) {
			reg.DeleteKeyW(FASTCOPY);
			reg.CloseKey();
		}
		reg.CloseKey();
	}

// レジストリからアンインストール情報を削除
	reg.ChangeTopKey(HKEY_CURRENT_USER);
	if (reg.OpenKey(REGSTR_PATH_UNINSTALL)) {
		reg.DeleteKeyW(FASTCOPY);
		reg.CloseKey();
	}

	WCHAR	path[MAX_PATH];

	GetParentDirW(setupDir, path);
	::SetCurrentDirectoryW(path);

	Sleep(1000);	// for starting rundll
	auto	ret = ReadyToRmFolder(setupDir);
	auto	ret2 = TRUE;

// AppDataディレクトリ内のiniを削除
	WCHAR	upath[MAX_PATH] = L"";

	if (IsWinVista() && TIsVirtualizedDirW(setupDir)) {
		WCHAR	wbuf[MAX_PATH] = L"";
		if (::SHGetSpecialFolderPathW(NULL, wbuf, CSIDL_APPDATA, FALSE)) {
			WCHAR	udir[MAX_PATH] = L"";
			WCHAR	*udirname = NULL;
			::GetFullPathNameW(setupDir, MAX_PATH, udir, &udirname);
			MakePathW(upath, wbuf, udirname);
		}
	}
	if (*upath && ::GetFileAttributesW(upath) != 0xffffffff) {
		if ((ret2 = ReadyToRmFolder(upath))) {
			::RemoveDirectoryW(upath);
		}
	}

// 終了メッセージ
	MessageBox((is_shext || !ret || !ret2) ? LoadStr(IDS_UNINSTSHEXTFIN) : LoadStr(IDS_UNINSTFIN));

// インストールディレクトリを開く
	if (!ret && ::GetFileAttributesW(setupDir) != 0xffffffff) {
		::ShellExecuteW(NULL, NULL, setupDir, 0, 0, SW_SHOW);
	}

	if (*upath && ::GetFileAttributesW(upath) != 0xffffffff) {
		::ShellExecuteW(NULL, NULL, upath, 0, 0, SW_SHOW);
	}

	WCHAR cmd[MAX_PATH];
	MakePathW(cmd, setupDir, UNINST_BAT_W);

	if (FILE *fp = _wfopen(cmd, L"w")) {
		MakePathW(path, setupDir, INSTALL_EXE);
		for (int i=0; i < 10; i++) {
			// fwprintf(fp, L"timeout 1 /NOBREAK\n");
			fwprintf(fp, L"ping 127.0.0.1 -w 1\n");
			if (ret) {
				fwprintf(fp, L"rd /s /q \"%s\"\n", setupDir);
			}
			else {
				fwprintf(fp, L"del /q \"%s\" \"%s\"\n", path, cmd);
			}
		}
		fclose(fp);

		STARTUPINFOW		sui = { sizeof(sui) };
		PROCESS_INFORMATION	pi = {};

		WCHAR opt[MAX_PATH];
		swprintf(opt, L"cmd.exe /c \"%s\"", cmd);
		if (::CreateProcessW(NULL, opt, 0, 0, 0, CREATE_NO_WINDOW, 0, 0, &sui, &pi)) {
			::CloseHandle(pi.hThread);
			::CloseHandle(pi.hProcess);
		}
	}

	Exit(0);

	return	TRUE;
}

void TInstDlg::Exit(DWORD exit_code)
{
	if (cfg.hOrgWnd) {
		::PostMessage(cfg.hOrgWnd, WM_CLOSE, 0, 0);
	}
	EndDialog(exit_code);
}


BOOL ReadLink(WCHAR *src, WCHAR *dest, WCHAR *arg=NULL)
{
	IShellLinkW		*shellLink;
	IPersistFile	*persistFile;
	BOOL			ret = FALSE;

	if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLinkW,
			(void **)&shellLink))) {
		if (SUCCEEDED(shellLink->QueryInterface(IID_IPersistFile, (void **)&persistFile))) {
			if (SUCCEEDED(persistFile->Load(src, STGM_READ))) {
				if (SUCCEEDED(shellLink->GetPath(dest, MAX_PATH, NULL, 0))) {
					if (arg) {
						shellLink->GetArguments(arg, MAX_PATH);
					}
					ret = TRUE;
				}
			}
			persistFile->Release();
		}
		shellLink->Release();
	}
	return	ret;
}

/*
	同じ内容を持つショートカットを削除（スタートメニューへの重複登録よけ）
*/
BOOL TInstDlg::RemoveSameLink(const WCHAR *dir, WCHAR *remove_path)
{
	WCHAR			path[MAX_PATH];
	WCHAR			dest[MAX_PATH];
	WCHAR			arg[MAX_PATH];
	HANDLE			fh;
	WIN32_FIND_DATAW data;
	BOOL			ret = FALSE;

	::swprintf(path, L"%s\\*.*", dir);
	if ((fh = ::FindFirstFileW(path, &data)) == INVALID_HANDLE_VALUE)
		return	FALSE;

	do {
		::swprintf(path, L"%s\\%s", dir, data.cFileName);
		if (ReadLink(path, dest, arg) && *arg == 0) {
			int		dest_len = (int)wcslen(dest);
			int		fastcopy_len = (int)wcslen(FASTCOPY_EXE);
			if (dest_len > fastcopy_len
				&& wcsnicmp(dest + dest_len - fastcopy_len, FASTCOPY_EXE, fastcopy_len) == 0) {
				ret = ::DeleteFileW(path);
				if (remove_path != NULL) {
					wcscpy(remove_path, path);
				}
			}
		}

	} while (::FindNextFileW(fh, &data));

	::FindClose(fh);
	return	ret;
}

TInstSheet::TInstSheet(TWin *_parent, InstallCfg *_cfg) : TDlg(INSTALL_SHEET, _parent)
{
	cfg = _cfg;
}

void TInstSheet::GetData(void)
{
	if (resId == UNINSTALL_SHEET) {
	}
	else {
		cfg->programLink	= IsDlgButtonChecked(PROGRAM_CHECK);
		cfg->desktopLink	= IsDlgButtonChecked(DESKTOP_CHECK);
	}
}

void TInstSheet::PutData(void)
{
	if (resId == UNINSTALL_SHEET) {
	}
	else {
		CheckDlgButton(PROGRAM_CHECK, cfg->programLink);
		CheckDlgButton(DESKTOP_CHECK, cfg->desktopLink);
	}
}

void TInstSheet::Paste(void)
{
	if (hWnd) {
		if ((resId == UNINSTALL_SHEET) == (cfg->mode == UNINSTALL_MODE)) {
			return;
		}
		GetData();
		Destroy();
	}
	resId = (cfg->mode == UNINSTALL_MODE) ? UNINSTALL_SHEET : INSTALL_SHEET;

	Create();
	PutData();
}

BOOL TInstSheet::EvCommand(WORD wNotifyCode, WORD wID, LPARAM hwndCtl)
{
	switch (wID)
	{
	case IDOK:	case IDCANCEL:
		{
			parent->EvCommand(wNotifyCode, wID, hwndCtl);
		}
		return	TRUE;
	}
	return	FALSE;
}

BOOL TInstSheet::EvCreate(LPARAM lParam)
{
	RECT	rc;
	POINT	pt;
	::GetWindowRect(parent->GetDlgItem(INSTALL_STATIC), &rc);
	pt.x = rc.left;
	pt.y = rc.top;
	ScreenToClient(parent->hWnd, &pt);
	SetWindowPos(0, pt.x, pt.y, 0, 0, SWP_NOSIZE|SWP_NOZORDER);

	SetWindowLong(GWL_EXSTYLE, GetWindowLong(GWL_EXSTYLE)|WS_EX_CONTROLPARENT);

	if (cfg->mode == UNINSTALL_MODE) {
		if (cfg->setuped.Len()) {
			::SetWindowTextW(GetDlgItem(INST_STATIC), cfg->setuped.s());
		}
	}

	Show();

	return	TRUE;
}

/*
	ディレクトリダイアログ用汎用ルーチン
*/
BOOL BrowseDirDlg(TWin *parentWin, UINT editCtl, const WCHAR *title)
{ 
	IMalloc			*iMalloc = NULL;
	BROWSEINFOW		brInfo;
	LPITEMIDLIST	pidlBrowse;
	WCHAR			fileBuf[MAX_PATH];

	parentWin->GetDlgItemTextW(editCtl, fileBuf, wsizeof(fileBuf));
	if (!SUCCEEDED(SHGetMalloc(&iMalloc)))
		return FALSE;

	/*if (IsWin7()) {
		WCHAR			buf[MAX_PATH];
		GetParentDirW(fileBuf, buf);
		vector<Wstr>	wvec;
		Wstr			dir(buf);
		TFileDlg(parentWin, &wvec, &dir, FDOPT_DIR);

		if (wvec.size() == 0) return FALSE;
		MakePathW(fileBuf, wvec[0].s(), L"");
		parentWin->SetDlgItemTextW(editCtl, fileBuf);
		return	TRUE;
	}*/

	TBrowseDirDlg	dirDlg(fileBuf);
	brInfo.hwndOwner = parentWin->hWnd;
	brInfo.pidlRoot = 0;
	brInfo.pszDisplayName = fileBuf;
	brInfo.lpszTitle = title;
	brInfo.ulFlags = BIF_RETURNONLYFSDIRS|BIF_SHAREABLE|BIF_BROWSEINCLUDEFILES|BIF_USENEWUI;
	brInfo.lpfn = BrowseDirDlg_Proc;
	brInfo.lParam = (LPARAM)&dirDlg;
	brInfo.iImage = 0;

	BOOL ret = FALSE;
	do {
		if ((pidlBrowse = ::SHBrowseForFolderW(&brInfo)) != NULL) {
			if (::SHGetPathFromIDListW(pidlBrowse, fileBuf)) {
				::SetDlgItemTextW(parentWin->hWnd, editCtl, fileBuf);
				ret = TRUE;
			}
			iMalloc->Free(pidlBrowse);
			break;
		}
	} while (dirDlg.IsDirty());

	iMalloc->Release();
	return	ret;
}

/*
	BrowseDirDlg用コールバック
*/
int CALLBACK BrowseDirDlg_Proc(HWND hWnd, UINT uMsg, LPARAM lParam, LPARAM data)
{
	TBrowseDirDlg *dlg = (TBrowseDirDlg *)data;
	if (!dlg) return 0;

	switch (uMsg) {
	case BFFM_INITIALIZED:
		dlg->AttachWnd(hWnd);
		break;

	case BFFM_SELCHANGED:
		if (dlg->hWnd) {	// hWndがNULLの間に \Users\(User) が来るのを避ける
			dlg->SetFileBuf(lParam);
		}
		break;
	}
	return 0;
}

/*
	BrowseDlg用サブクラス生成
*/
BOOL TBrowseDirDlg::AttachWnd(HWND _hWnd)
{
	TSubClass::AttachWnd(_hWnd);
	dirtyFlg = FALSE;

// ディレクトリ設定
	DWORD	attr = ::GetFileAttributesW(fileBuf);
	if (attr == 0xffffffff || (attr & FILE_ATTRIBUTE_DIRECTORY) == 0) {
		GetParentDirW(fileBuf, fileBuf);
	}
	SendMessageW(BFFM_SETSELECTIONW, TRUE, (LPARAM)fileBuf);
	SetWindowTextW(FASTCOPY);

// ボタン作成
#if 0
	RECT	tmp_rect;
	::GetWindowRect(GetDlgItem(IDOK), &tmp_rect);
	POINT	pt = { tmp_rect.left, tmp_rect.top };
	::ScreenToClient(hWnd, &pt);
	int		cx = (pt.x - 30) / 2, cy = tmp_rect.bottom - tmp_rect.top;

	::CreateWindow(BUTTON_CLASS, LoadStr(IDS_MKDIR), WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 10,
		pt.y, cx, cy, hWnd, (HMENU)MKDIR_BUTTON, TApp::GetInstance(), NULL);
	::CreateWindow(BUTTON_CLASS, LoadStr(IDS_RMDIR), WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
		18 + cx, pt.y, cx, cy, hWnd, (HMENU)RMDIR_BUTTON, TApp::GetInstance(), NULL);

	HFONT	hDlgFont = (HFONT)SendDlgItemMessage(IDOK, WM_GETFONT, 0, 0L);
	if (hDlgFont) {
		SendDlgItemMessage(MKDIR_BUTTON, WM_SETFONT, (LPARAM)hDlgFont, 0L);
		SendDlgItemMessage(RMDIR_BUTTON, WM_SETFONT, (LPARAM)hDlgFont, 0L);
	}
#endif

	return	TRUE;
}

/*
	BrowseDlg用 WM_COMMAND 処理
*/
BOOL TBrowseDirDlg::EvCommand(WORD wNotifyCode, WORD wID, LPARAM hwndCtl)
{
	switch (wID) {
	case MKDIR_BUTTON:
		{
			WCHAR	dirBuf[MAX_PATH];
			WCHAR	path[MAX_PATH];
			TInputDlg	dlg(dirBuf, this);
			if (dlg.Exec() != IDOK)
				return	TRUE;

			MakePathW(path, fileBuf, dirBuf);
			if (::CreateDirectoryW(path, NULL)) {
				wcscpy(fileBuf, path);
				dirtyFlg = TRUE;
				PostMessage(WM_CLOSE, 0, 0);
			}
		}
		return	TRUE;

	case RMDIR_BUTTON:
		if (::RemoveDirectoryW(fileBuf)) {
			GetParentDirW(fileBuf, fileBuf);
			dirtyFlg = TRUE;
			PostMessage(WM_CLOSE, 0, 0);
		}
		return	TRUE;
	}
	return	FALSE;
}

HWND FindSysTree(HWND hWnd)
{
	for (HWND hChild=::GetWindow(hWnd, GW_CHILD);
		hChild; hChild=::GetWindow(hChild, GW_HWNDNEXT)) {
		for (HWND hSubChild = ::GetWindow(hChild, GW_CHILD); hSubChild;
			hSubChild = ::GetWindow(hSubChild, GW_HWNDNEXT)) {
			WCHAR	wbuf[MAX_PATH];
			if (GetClassNameW(hSubChild, wbuf, wsizeof(wbuf))
				&& wcscmp(wbuf, WC_TREEVIEWW) == 0) {
				return	hSubChild;
			}
		}
	}
	return	NULL;
}

BOOL TBrowseDirDlg::SetFileBuf(LPARAM list)
{
	if (hWnd) {
		if (auto hSysTree = FindSysTree(hWnd)) {
			auto hRoot = TreeView_GetRoot(hSysTree);
			auto hCur = TreeView_GetNextItem(hSysTree, hRoot, TVGN_CARET);
			TreeView_EnsureVisible(hSysTree, hCur);
		}
	}
	return	::SHGetPathFromIDListW((LPITEMIDLIST)list, fileBuf);
}

/*
	一行入力
*/
BOOL TInputDlg::EvCommand(WORD wNotifyCode, WORD wID, LPARAM hwndCtl)
{
	switch (wID) {
	case IDOK:
		GetDlgItemTextW(INPUT_EDIT, dirBuf, MAX_PATH);
		EndDialog(wID);
		return	TRUE;

	case IDCANCEL:
		EndDialog(wID);
		return	TRUE;
	}
	return	FALSE;
}

/*
	ファイルの保存されているドライブ識別
*/
UINT GetDriveTypeEx(const WCHAR *file)
{
	if (file == NULL) {
		return	GetDriveType(NULL);
	}

	if (IsUncFile(file))
		return	DRIVE_REMOTE;

	WCHAR	buf[MAX_PATH];
	int		len = (int)wcslen(file), len2;

	wcscpy(buf, file);
	do {
		len2 = len;
		GetParentDirW(buf, buf);
		len = (int)wcslen(buf);
	} while (len != len2);

	return	GetDriveTypeW(buf);
}

/*
	起動ダイアログ
*/
TLaunchDlg::TLaunchDlg(const WCHAR *_msg, TWin *_win) : TDlg(LAUNCH_DIALOG, _win)
{
	msg = wcsdup(_msg);
}

TLaunchDlg::~TLaunchDlg()
{
	free(msg);
}

/*
	メインダイアログ用 WM_INITDIALOG 処理ルーチン
*/
BOOL TLaunchDlg::EvCreate(LPARAM lParam)
{
	SetDlgItemTextW(MESSAGE_STATIC, msg);
	Show();
	return	TRUE;
}

BOOL TLaunchDlg::EvCommand(WORD wNotifyCode, WORD wID, LPARAM hwndCtl)
{
	switch (wID)
	{
	case IDOK: case IDCANCEL:
		EndDialog(wID);
		return	TRUE;
	}
	return	FALSE;
}


BOOL DeflateData(BYTE *buf, DWORD size, DynBuf *dbuf)
{
	BOOL	ret = FALSE;
	z_stream z = {};

	if (inflateInit(&z) != Z_OK) return FALSE;
	z.next_out = dbuf->Buf();
	z.avail_out = (uInt)dbuf->Size();
	z.next_in = buf;
	z.avail_in = size;
	if (inflate(&z, Z_NO_FLUSH) == Z_STREAM_END) {
		dbuf->SetUsedSize(dbuf->Size() - z.avail_out);
		ret = TRUE;
	}
	inflateEnd(&z);
	return	ret;
}

#define MAX_FSIZE	(5 * 1024 * 1024)
BOOL IPDictCopy(IPDict *dict, const WCHAR *fname, const WCHAR *dst, BOOL *is_rotate)
{
	HANDLE	hDst;
	BOOL	ret = FALSE;
	DWORD	dstSize = 0;
	int64	mtime;
	int64	fsize;
	IPDict	fdict;
	DynBuf	zipData;
	DynBuf	data(MAX_FSIZE);

	if (!dict->get_dict(WtoU8s(fname), &fdict)) {
		return	FALSE;
	}
	if (!fdict.get_bytes(FDATA_KEY, &zipData)) {
		return	FALSE;
	}
	if (!fdict.get_int(MTIME_KEY, &mtime)) {
		return	FALSE;
	}
	if (!fdict.get_int(FSIZE_KEY, &fsize)) {
		return	FALSE;
	}
	if (!DeflateData(zipData.Buf(), (DWORD)zipData.UsedSize(), &data)
	|| fsize != data.UsedSize()) {
		return FALSE;
	}

#define MAX_ROTATE	10
	CleanupRotateFile(dst, MAX_ROTATE);
	hDst = ::CreateFileW(dst, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
	if (hDst == INVALID_HANDLE_VALUE) {
		RotateFile(dst, MAX_ROTATE, TRUE);
		if (is_rotate) {
			*is_rotate = TRUE;
		}
		hDst = ::CreateFileW(dst, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
	}

	if (hDst == INVALID_HANDLE_VALUE) {
		DebugW(L"dst(%s) open err(%x)\n", dst, GetLastError());
		return	FALSE;
	}

	if (::WriteFile(hDst, data.Buf(), (DWORD)fsize, &dstSize, 0) && fsize == dstSize) {
		FILETIME	ct;
		FILETIME	la;
		FILETIME	ft;

		UnixTime2FileTime(mtime, &ct);
		la = ct;
		ft = ct;
		::SetFileTime(hDst, &ct, &la, &ft);
		ret = TRUE;
	}
	::CloseHandle(hDst);

	return	ret;
}


BOOL IsSameFileDict(IPDict *dict, const WCHAR *fname, const WCHAR *dst)
{
	WIN32_FIND_DATAW dst_dat;
	HANDLE		hFind;
	int64		stime;
	int64		dtime;
	int64		fsize;
	IPDict		fdict;

	if (!dict->get_dict(WtoU8s(fname), &fdict)) {
		return	FALSE;
	}
	if (!fdict.get_int(MTIME_KEY, &stime)) {
		return	FALSE;
	}
	if (!fdict.get_int(FSIZE_KEY, &fsize)) {
		return	FALSE;
	}

	if ((hFind = ::FindFirstFileW(dst, &dst_dat)) == INVALID_HANDLE_VALUE) {
		return	FALSE;
	}
	::FindClose(hFind);

	if (fsize != dst_dat.nFileSizeLow) {
		return	FALSE;
	}
	dtime = FileTime2UnixTime(&dst_dat.ftLastWriteTime);

	return	(stime >> 2) == (dtime >> 2);
}


BOOL GetIPDictBySelf(IPDict *dict)
{
	auto hSelfFile = scope_raii(
			[&]() {
				WCHAR	self_name[MAX_PATH] = {};
				::GetModuleFileNameW(NULL, self_name, wsizeof(self_name));
				return	::CreateFileW(self_name, GENERIC_READ,
						FILE_SHARE_READ|FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, 0);
			}(),
			[&](auto hSelfFile) { ::CloseHandle(hSelfFile); });
	if (hSelfFile == INVALID_HANDLE_VALUE) return FALSE;

	auto hMap = scope_raii(
			::CreateFileMapping(hSelfFile, 0, PAGE_READONLY, 0, 0, 0),
			[&](auto hMap) { ::CloseHandle(hMap); });
	if (!hMap) return FALSE;

	auto data = scope_raii(
			(BYTE *)MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0),
			[&](auto data) { ::UnmapViewOfFile(data); });
	if (!data) return FALSE;

	BYTE	sep[72];
	memset(sep, '=', sizeof(sep));
	sep[0] = sep[sizeof(sep)-1] = '\n';

	auto	selfSize = (size_t)::GetFileSize(hSelfFile, 0);
	auto	max_size = selfSize - sizeof(sep);

	for (int i=0; i < max_size; ) {
		BYTE	&ch = data[i];
		BYTE	&end_ch = data[i+sizeof(sep)-1];

		if (ch == '\n') {
			if (end_ch == '\n') {
				if (memcmp(&ch, sep, sizeof(sep)) == 0 && memcmp(&end_ch+1, "IP2:", 4) == 0) {
					auto	targ = &ch + sizeof(sep);
					auto	remain = selfSize - (targ - data);
					return	(dict->unpack(targ, remain) < remain) ? TRUE : FALSE;
				}
				i += sizeof(sep)-1;
			}
			else if (end_ch == '=') {
				i++;
			}
			else {
				i += sizeof(sep);
			}
		}
		else if (end_ch == '=') {
			i++;
		}
		else {
 			i += sizeof(sep);
		}
	}

	return	FALSE;
}
