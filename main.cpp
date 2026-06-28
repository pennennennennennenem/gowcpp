#include <windows.h>
#include <shellapi.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cwchar>
#include <cwctype>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "resource.h"


namespace fs = std::filesystem;

constexpr UINT HOTKEY_ID = 1;
constexpr UINT WM_TRAYICON = WM_APP + 1;

HINSTANCE g_hInst;

//メイン
HWND g_hMain = nullptr;
//検索ボックス
HWND g_hSearchWnd = nullptr;
HWND g_hDriveLabel = nullptr;
HWND g_hDescLabel = nullptr;
HWND g_hEdit = nullptr;
WNDPROC g_EditOldProc = nullptr;
//リストボックス
HWND g_hListWnd = nullptr;
HWND g_hListBox = nullptr;
WNDPROC g_ListBoxOldProc = nullptr;
//GDI
HFONT g_hUIFont = nullptr;
HFONT g_hUIFontBold = nullptr;
HBRUSH g_hBgBrush = nullptr;
HICON g_appIcon;
std::vector<HICON>g_hAnimIcons;

constexpr DWORD IDC_LISTBOX = 1001;
constexpr DWORD IDC_TRAYMENU = 2001;
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK SearchWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK ListWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK EditProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK ListBoxProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK DriveLabelProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

fs::path GetExecutableDirectory()
{
	wchar_t path[MAX_PATH];
	GetModuleFileNameW(nullptr, path, MAX_PATH);
	fs::path exePath(path);
	return exePath.parent_path();
}

//executableのディレクトリを取得
fs::path g_path = GetExecutableDirectory();
fs::path g_iniPath = g_path / L"gowcpp.ini";
char g_drive = 'C'; //デフォルトのカレントドライブはC

/*****************************************************************************
* YRZ
*****************************************************************************/

//wstringをANSIエンコードに変換し、std::stringに格納
std::string wstrToANSI(const std::wstring& w, UINT codepage = CP_ACP);

//ANSI stringをwstringへ
std::wstring ANSITowstr(const std::string& s, UINT codepage = CP_ACP);

//std::stringに格納してあるUTF-8文字列をwstringへ
inline std::wstring UTF8Towstr(const std::string& s) { return ANSITowstr(s, CP_UTF8); }

//wstringをUTF-8エンコードに変換しstd::stringに格納
inline std::string wstrToUTF8(const std::wstring& w) { return wstrToANSI(w, CP_UTF8); }

//UTF8またはANSI stringをwstringへ
inline std::wstring L(const std::string& s, UINT codepage = CP_UTF8) { return ANSITowstr(s, codepage); }

//wstringをUTF8へ
inline std::string u8(const std::wstring& w) { return wstrToANSI(w, CP_UTF8); }

//wstringをANSI(など)へ
inline std::string A(const std::wstring& w, UINT codepage = CP_ACP) { return wstrToANSI(w, codepage); }


//wstring(UTF-16 LE → ANSI string)
std::string wstrToANSI(const std::wstring& w, UINT codepage) {
    int iBufferSize = WideCharToMultiByte(codepage, 0, w.c_str(), -1, (char*)NULL, 0, NULL, NULL);
    CHAR* cpMultiByte = new CHAR[iBufferSize];
    WideCharToMultiByte(codepage, 0, w.c_str(), -1, cpMultiByte, iBufferSize, NULL, NULL);
    std::string oRet(cpMultiByte, cpMultiByte + iBufferSize - 1);
    delete[] cpMultiByte;
    return(oRet);
}

//ANSI stringから wstring(UTF-16LE)へ
std::wstring ANSITowstr(const std::string& s, UINT codepage) {
    int iBufferSize = MultiByteToWideChar(codepage, 0, s.c_str(), -1, (wchar_t*)NULL, 0);
    wchar_t* cpUCS2 = new wchar_t[iBufferSize];
    MultiByteToWideChar(codepage, 0, s.c_str(), -1, cpUCS2, iBufferSize);
    std::wstring oRet(cpUCS2, cpUCS2 + iBufferSize - 1);
    delete[] cpUCS2;
    return(oRet);
}

// 任意の basic_string (string/wstring) に対して前後トリム
template<typename T> T TrimStr(T str) {
    T s = str;
    using CharT = typename T::value_type;

    // char → std::isspace / wchar_t → std::iswspace
    auto is_space = [](CharT ch) {
        if constexpr (std::is_same_v<CharT, char>) {
            return std::isspace(static_cast<unsigned char>(ch));
        } else {
            return std::iswspace(ch);
        }
        };

    // 前方の空白をスキップ
    auto start = std::find_if_not(s.begin(), s.end(), is_space);
    // 後方の空白をスキップ
    auto end = std::find_if_not(s.rbegin(), s.rend(), is_space).base();

    if (start == s.end()) {
        // 文字列全体が空白のみならクリア
        s.clear();
    } else {
        // 後ろ側を消してから前側を消す
        s.erase(end, s.end());
        s.erase(s.begin(), start);
    }
    return s;
}

// メッセージボックスで Yes/No を表示し、Yes なら true, No なら false を返す
bool YesNoDlg(const std::wstring& title, const std::wstring& msg)
{
	int ret = MessageBoxW(g_hMain, msg.c_str(), title.c_str(), MB_YESNO | MB_ICONQUESTION);
	return (ret == IDYES);
}

// メッセージボックスで OK を表示するだけ
void OKDlg(const std::wstring& title, const std::wstring& msg)
{
	MessageBoxW(g_hMain, msg.c_str(), title.c_str(), MB_OK | MB_ICONINFORMATION);
}

// クリップボードにテキストをコピーする関数
void CopyTextToClipboard(const std::wstring& text)
{
    if (!OpenClipboard(nullptr)) {
        return;
    }

    EmptyClipboard();
    size_t nByte = text.size() * sizeof(wchar_t) + 2;
    HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, nByte);
    if (!hGlobal) {
        CloseClipboard();
        return;
    }

    memcpy(GlobalLock(hGlobal), text.c_str(), nByte);
    GlobalUnlock(hGlobal);

    SetClipboardData(CF_UNICODETEXT, hGlobal);

    CloseClipboard();
}

//文字列を区切り文字tokで分解してvectorで返す
template<class T, class U>
std::vector<T>SplitStr(const T& s, U tok, bool trim = false) {
    auto ret = std::vector<T>();

    size_t top = 0, len = 1;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == tok) {
            ret.push_back(s.substr(top, len - 1));
            top = i + 1;
            len = 1;
        } else {
            len++;
        }
    }

    if (top < s.size())
        ret.push_back(s.substr(top, len));

    if (trim) {
        for (auto& e : ret)
            e = TrimStr(e);
    }

    return ret;
}

//文字列のうち半角英文字を小文字にする
template<class T> T LowerStr(const T& str) {
    T ret = str;
    std::transform(ret.begin(), ret.end(), ret.begin(), tolower);
    return ret;
}

template<class T> bool ContainsAny(const T& str, const std::vector<T>& subs) {
    return std::find_if(subs.begin(), subs.end(), [&](const T& s) { return str.find(s) != T::npos; }) != subs.end();
}


/*****************************************************************************
* config
*****************************************************************************/

//ホットキー文字列を解析して修飾キーと仮想キーコードに変換する関数
bool ParseHotkeyString(const std::wstring& input, UINT& mod, UINT& vk)
{
    mod = 0;
    vk = 0;

    // --- トークン分割 ---
    std::wstring s = input;
    std::transform(s.begin(), s.end(), s.begin(), ::towlower);

    std::vector<std::wstring> tokens;
    size_t start = 0;
    while (true) {
        size_t pos = s.find(L'+', start);
        if (pos == std::wstring::npos) {
            tokens.push_back(s.substr(start));
            break;
        }
        tokens.push_back(s.substr(start, pos - start));
        start = pos + 1;
    }

    // --- 修飾キーの対応表 ---
    static const std::unordered_map<std::wstring, UINT> modmap = {
        {L"ctrl",  MOD_CONTROL},
        {L"control", MOD_CONTROL},
        {L"shift", MOD_SHIFT},
        {L"alt",   MOD_ALT},
        {L"menu",  MOD_ALT},
        {L"win",   MOD_WIN},
        {L"windows", MOD_WIN},
        {L"super", MOD_WIN},
    };

    // --- VK の対応表（必要に応じて増やせる） ---
    static const std::unordered_map<std::wstring, UINT> vkmap = {
        {L"f1", VK_F1}, {L"f2", VK_F2}, {L"f3", VK_F3}, {L"f4", VK_F4},
        {L"f5", VK_F5}, {L"f6", VK_F6}, {L"f7", VK_F7}, {L"f8", VK_F8},
        {L"f9", VK_F9}, {L"f10", VK_F10}, {L"f11", VK_F11}, {L"f12", VK_F12},

        {L"delete", VK_DELETE},
        {L"del", VK_DELETE},
        {L"backspace", VK_BACK},
        {L"esc", VK_ESCAPE},
        {L"escape", VK_ESCAPE},
        {L"tab", VK_TAB},
        {L"space", VK_SPACE},
    };

    // --- トークン処理 ---
    for (auto& t : tokens) {
        if (t.empty()) continue;

        // 修飾キー？
        auto it = modmap.find(t);
        if (it != modmap.end()) {
            mod |= it->second;
            continue;
        }

        // VK_XXX？
        auto it2 = vkmap.find(t);
        if (it2 != vkmap.end()) {
            vk = it2->second;
            continue;
        }

        // 1文字キー？
        if (t.size() == 1) {
            wchar_t c = towupper(t[0]);
            vk = (UINT)c; // 'A'〜'Z' や '0'〜'9'
            continue;
        }

        // VK_OEM_1 のような形式？
        if (t.rfind(L"vk_", 0) == 0) {
            // 例: vk_oem_1 → VK_OEM_1
            std::wstring upper = t;
            std::transform(upper.begin(), upper.end(), upper.begin(), ::towupper);

            // Win32 の VK_ 定数に変換（自前でマップを作るか、必要なものだけ対応）
            if (upper == L"VK_OEM_1") vk = VK_OEM_1;
            else if (upper == L"VK_OEM_PLUS") vk = VK_OEM_PLUS;
            else if (upper == L"VK_OEM_COMMA") vk = VK_OEM_COMMA;
            else if (upper == L"VK_OEM_MINUS") vk = VK_OEM_MINUS;
            else if (upper == L"VK_OEM_PERIOD") vk = VK_OEM_PERIOD;
            else if (upper == L"VK_OEM_2") vk = VK_OEM_2;
            else if (upper == L"VK_OEM_3") vk = VK_OEM_3;
            else if (upper == L"VK_OEM_4") vk = VK_OEM_4;
            else if (upper == L"VK_OEM_5") vk = VK_OEM_5;
            else if (upper == L"VK_OEM_6") vk = VK_OEM_6;
            else if (upper == L"VK_OEM_7") vk = VK_OEM_7;
            else return false;

            continue;
        }

        // 不明なトークン
        return false;
    }

    return (vk != 0);
}

//ホットキーで起動されるコマンド
struct HotKeyCommand {
	UINT index;      // 設定ファイル内の順序
	UINT vk;         // 仮想キーコード
	UINT mod;        // 修飾キー（MOD_CONTROL, MOD_ALT, MOD_SHIFT, MOD_WIN の組み合わせ）
    std::wstring desc;  // ラベルに表示する名前
	std::wstring file;  // 実行するファイル
    std::wstring param; // パラメータ(マクロ入りの可能性あり)
};

struct Config {
    int LRUCount = 32;      //LRUリストに保存されるアイテムの数
    int listWidth = 320;    //検索ボックスに対して更にどれだけ幅を増やすか
	std::vector<HotKeyCommand> commands;
    std::vector<std::wstring> excludes; //除外リスト
};

std::deque<std::wstring>g_LRUs; //LRUリスト
Config g_cfg;


bool IsValidDriveLetter(wchar_t driveLetter)
{
    wchar_t rootPath[] = { driveLetter, L':', L'\\', L'\0' };
    UINT type = GetDriveTypeW(rootPath);

    // DRIVE_UNKNOWN(0) と DRIVE_NO_ROOT_DIR(1) は無効
    return type != DRIVE_UNKNOWN && type != DRIVE_NO_ROOT_DIR;
}

//コンフィグファイルの読み込み
void LoadConfig()
{
    g_cfg = {};

    constexpr int bufsize = 4096;
    wchar_t buf[bufsize];

    //ドライブ
	GetPrivateProfileStringW(L"config", L"drive", L"C", buf, bufsize, g_iniPath.wstring().c_str());
    
    //前回終了時にリムーバブルメディアが指定されていて、現在は切断されている場合などが考えられるので
    //読み取ったドライブレターをそのまま鵜呑みにしない
    if (IsValidDriveLetter(buf[0]))
        g_drive = toupper(buf[0]);

    //LRUリストの最大保存数
    g_cfg.LRUCount = GetPrivateProfileIntW(L"config", L"LRU", g_cfg.LRUCount, g_iniPath.wstring().c_str());

    //検索結果表示ウィンドウの検索キーワード入力ウィンドウに対する幅
    g_cfg.listWidth = GetPrivateProfileIntW(L"config", L"ListWidth", g_cfg.LRUCount, g_iniPath.wstring().c_str());

    //prefixで始まるセクション名orエントリ名に分解
    auto bunkaiLambda = [&](std::vector<std::wstring>& vec, size_t len, const std::wstring& prefix) {
        vec.clear();
        std::vector<wchar_t> sec;
        for (int i = 0; i < len; i++) {
            wchar_t c = *(buf + i);
            sec.push_back(c);
            if (c == 0) {
                if (!sec.empty()) {
                    std::wstring name = sec.data();
                    if (name.starts_with(prefix))
                        vec.push_back(sec.data());
                }
                sec.clear();
            }
        }
    };

	//itemで始まるセクション名を取得
    int len = GetPrivateProfileSectionNamesW(buf, bufsize, g_iniPath.wstring().c_str());
    std::vector<std::wstring> sections;
    bunkaiLambda(sections, len, L"item");

    //コマンドの読み込み
    for (const auto& s : sections) {
		HotKeyCommand cmd;
		GetPrivateProfileStringW(s.c_str(), L"hotkey", L"", buf, bufsize, g_iniPath.wstring().c_str());
        std::wstring w = buf;
        if (ParseHotkeyString(w, cmd.mod, cmd.vk)) {
            cmd.index = g_cfg.commands.size();
            GetPrivateProfileStringW(s.c_str(), L"desc", L"", buf, bufsize, g_iniPath.wstring().c_str());
            cmd.desc = buf;
            GetPrivateProfileStringW(s.c_str(), L"file", L"", buf, bufsize, g_iniPath.wstring().c_str());
            cmd.file = buf;
            GetPrivateProfileStringW(s.c_str(), L"param", L"", buf, bufsize, g_iniPath.wstring().c_str());
            cmd.param = buf;
            g_cfg.commands.push_back(cmd);
        }
    }

    //無視リストの読み込み
    g_cfg.excludes = {};
    len = GetPrivateProfileSectionW(L"exclude",buf,bufsize, g_iniPath.wstring().c_str());
    bunkaiLambda(g_cfg.excludes, len, L"dir");
    for (auto& ex : g_cfg.excludes) {
        //=の後だけ抽出して小文字にする
        size_t pos = ex.find(L"=");
        if (pos != std::wstring::npos && ex.size() > pos+1) {
            ex = ex.substr(pos+1);
        }
        ex = LowerStr(TrimStr(ex));
    }
}

//configにカレントドライブを保存
void SaveConfig()
{
    wchar_t buf[2] = { (wchar_t)g_drive, 0 };
    WritePrivateProfileStringW(L"config", L"drive", buf, g_iniPath.wstring().c_str());
}



/*****************************************************************************
* drive & scanning
*****************************************************************************/
std::vector<std::wstring> g_dirs;
std::vector<std::wstring> g_scanDirs;   //スキャン用ディレクトリバッファ、スキャンが完了したらg_dirsにコピーされる
std::atomic_int g_scanDirCount = 0; //スキャン中のディレクトリ数

HANDLE g_hScanThread = nullptr;
std::mutex g_scanMutex;

std::atomic<bool> g_scanning = false;  // スキャン中フラグ

void UpdateTrayAnimaton()
{
    NOTIFYICONDATA nicon = {};
    nicon.cbSize = sizeof(nicon);
    nicon.hWnd = g_hMain;
    nicon.uID = 1;
    nicon.uFlags = NIF_ICON;
    if (g_scanning) {
        static int animF = 0;
        nicon.hIcon = g_hAnimIcons[animF];
        animF = (animF + 1) % g_hAnimIcons.size();
    } else {
        nicon.hIcon = g_appIcon;
    }
    Shell_NotifyIcon(NIM_MODIFY, &nicon);
}

void UpdateTrayTooltip()
{
    NOTIFYICONDATA nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hMain;
    nid.uID = 1;
    nid.uFlags = NIF_TIP;

    if (g_scanning) {
        swprintf_s(nid.szTip, sizeof(nid.szTip) / sizeof(nid.szTip[0]), L"GOw C++\nScanning...  prev:%zu , now:%d", g_dirs.size(), g_scanDirCount.load());
    } else {
        swprintf_s(nid.szTip, sizeof(nid.szTip) / sizeof(nid.szTip[0]), L"GOw C++\nScan complete (%d directories)", g_scanDirCount.load());
    }

    Shell_NotifyIcon(NIM_MODIFY, &nid);
}

void ShowNotification(const std::wstring& title, const std::wstring& msg)
{
    static std::wstring prevTitle = L"";
    static ULONGLONG prevTime = 0;

	auto time = GetTickCount64();

	if (title == prevTitle && (time - prevTime) < 5000) {
		// 同じタイトルの通知が5秒以内に表示されている場合はスキップ
		return;
	}

	prevTime = time;
	prevTitle = title;

    NOTIFYICONDATA nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hMain;
    nid.uID = 1;
    nid.uFlags = NIF_INFO;

    // バッファサイズを明示して安全にコピー/書式化
    wcsncpy_s(nid.szInfoTitle, title.data(), min(63, title.size()));
    wcsncpy_s(nid.szInfo, msg.data(), min(255, msg.size()));

    nid.dwInfoFlags = NIIF_INFO;

    Shell_NotifyIcon(NIM_MODIFY, &nid);

}

void ShowScanCompleteNotification()
{
    ShowNotification(L"Scan complete", std::format(L"{} directories loaded", g_scanDirCount.load()));
}

//ドライブのGUIDを文字列で得る(キャッシュファイル名用)
std::wstring GetDriveGUID(char drive)
{
    wchar_t volumeName[MAX_PATH] = { 0 };

    // driveLetter = L"C:" のように渡す
    std::wstring root = std::format(L"{}:\\",drive);

    BOOL ok = GetVolumeNameForVolumeMountPointW(
        root.c_str(),
        volumeName,
        MAX_PATH
    );

    if (!ok) {
        DWORD err = GetLastError();
        wprintf(L"GetVolumeNameForVolumeMountPoint failed: %u\n", err);
        return L"";
    }

    // volumeName は "\\?\Volume{GUID}\" の形式で返る

    std::wstring vol = volumeName;
    size_t start = vol.find(L'{');
    size_t end = vol.find(L'}');

    if (start == std::wstring::npos || end == std::wstring::npos || end <= start)
        return L"";

    vol = vol.substr(start + 1, end - start - 1);
    return vol;
}

//ドライブのポリュームラベルを得る
std::wstring GetDriveVolumeName(char drive)
{
    wchar_t volumeName[MAX_PATH];
    GetVolumeInformationW((std::wstring(1, drive) + L":\\").c_str(), volumeName, MAX_PATH, nullptr, nullptr, nullptr, nullptr, 0);
    return volumeName;
}

//キャッシュファイルのパス
fs::path CacheFilePath(char drive)
{
    return g_path / (L"cache\\" + GetDriveGUID(drive) + L".cache");
}

fs::path LRUFilePath(char drive)
{
    return g_path / (L"cache\\" + GetDriveGUID(drive) + L".LRU");
}

//patternにマッチするディレクトリ名一覧をvecに作る
void EnumDirectory(const std::wstring& pattern, std::vector<std::wstring>& vec)
{
    fs::path parent = fs::path(pattern).parent_path();
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE)
        return;

    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {

            // "." と ".." は無視
            if (wcscmp(fd.cFileName, L".") == 0 ||
                wcscmp(fd.cFileName, L"..") == 0)
                continue;

            // ★ 再解析ポイント（Junction / Symlink）を無視
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
                continue;

            std::wstring full = (parent / fd.cFileName).wstring();
            vec.push_back(LowerStr(full).substr(2)); //小文字にしてドライブレター部分を削除して格納
        }

    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);

}


//ディレクトリスキャンスレッド
void ScanDirectory(const std::wstring& root)
{
    std::wstring pattern = root + L"\\*";

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE)
        return;

    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {

            // "." と ".." は無視
            if (wcscmp(fd.cFileName, L".") == 0 ||
                wcscmp(fd.cFileName, L"..") == 0)
                continue;

            // ★ 再解析ポイント（Junction / Symlink）を無視
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
                continue;

            std::wstring full = root + L"\\" + fd.cFileName;

            {
                std::lock_guard<std::mutex> lock(g_scanMutex);
                g_scanDirs.push_back(LowerStr(full).substr(2)); //小文字にしてドライブレター部分を削除して格納
				g_scanDirCount++;
            }

            // 再帰
            ScanDirectory(full);
        }

    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
}

struct ScanParam {
	char drive;
	std::wstring cacheFile;
};

void SaveCache();

//ディレクトリスキャンの実行
void StartScan(char drive)
{
    ShowNotification(L"Start scanning", std::format(L"scanning {}:{}, wait for minutes.",drive, GetDriveVolumeName(drive)));
    g_scanning = true;

    std::lock_guard<std::mutex> lock(g_scanMutex);
	g_scanDirs.clear();
	g_scanDirCount = 0;

    g_hScanThread = CreateThread(
        nullptr, 0,
        [](LPVOID param) -> DWORD {
            auto* p = (ScanParam*)param;

            wchar_t d[2] = { (wchar_t)p->drive,0 };
            ScanDirectory(std::wstring(d) + L":");
            // スキャン完了フラグをクリアしてメインスレッドに終了を知らせる
            g_scanning = false;
            delete p;
            return 0;
        },
        new ScanParam{ drive, CacheFilePath(drive).wstring()},
        0, nullptr
    );
}


//ファイル名部分を取り出すラムダ式
auto yenLambda = [](const std::wstring& s)->wchar_t* {
    size_t idx = s.rfind(L'\\');
    wchar_t* p = (wchar_t*)s.c_str();
    return (idx != std::wstring::npos) ? p + idx + 1 : p;
};

//ディレクトリ名順にソート
void SortDirs()
{
    std::sort(g_dirs.begin(), g_dirs.end(),
        [&](std::wstring a,std::wstring b) {
			auto ya = yenLambda(a);
			auto yb = yenLambda(b);
            return wcscmp(ya, yb) < 0;
        }
    );
}


//ドライブキャッシュの読み込み
void LoadDirCache(char drive)
{
    g_dirs.clear();
    fs::path path = CacheFilePath(drive);
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs)
        return;

    std::string line;
    while (std::getline(ifs, line)) {
        g_dirs.push_back(L(line));
    }
}

//LRU読み込み
void LoadLRU(char drive)
{
    g_LRUs.clear();
    fs::path path = LRUFilePath(drive);
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs)
        return;

    std::string line;
    while (std::getline(ifs, line)) {
        g_LRUs.push_back(L(line));
        if (g_LRUs.size() >= g_cfg.LRUCount)
            break;
    }
}

//ドライブのキャッシュとLRUを読み込み。キャッシュが無い場合はg_dirをクリアするだけ
void LoadCache(char drive)
{
    LoadDirCache(drive);
    LoadLRU(drive);
}

//LRUリストの保存
void SaveLRU()
{
    fs::path path = LRUFilePath(g_drive);
    std::ofstream ofs(path, std::ios::binary);

    if (!ofs) return;
    int nWritten = 0;
    for (const auto& s : g_LRUs) {
        ofs << u8(s) << "\n";
        nWritten++;
        if (nWritten >= g_cfg.LRUCount)
            break;
    }
    ofs.flush();
    ofs.close();
}


//ドライブのキャッシュを保存
void SaveCache()
{
    fs::path path = CacheFilePath(g_drive);
    std::ofstream ofs(path, std::ios::binary);

    if (!ofs) return;

    int nWritten = 0;
    for (const auto& s : g_dirs) {
        ofs << u8(s) << "\n";
        nWritten++;
    }
    ofs.flush();
    ofs.close();
}


//有効なドライブならカレントドライブの変更
void ChangeCurrentDrive(char drive)
{
    if (IsValidDriveLetter(drive)) {
        g_drive = toupper(drive);
        LoadCache(g_drive);
		std::wstring label = std::format(L"{}:", g_drive);
        SetWindowTextW(g_hDriveLabel, label.c_str());
    }
}

//カレントドライブのスキャン
void ScanCurrentDrive()
{
	if (g_scanning) {
		ShowNotification(L"Scan in progress", L"Please wait until the current scan is complete.");
		return;
	}
    SetTimer(g_hMain, 2, 250, nullptr);    //アニメーションタイマの設定
    g_scanning = true;
    StartScan(g_drive);
}

//ディレクトリ名を前方一致検索してインデックスを返す。
std::pair<size_t,size_t> BinSearch(const std::wstring& str)
{
    // 前方一致の開始位置を二分探索で探す
    size_t left = 0;
    size_t right = (int)g_dirs.size() - 1;
    size_t mid, start, end;
	wchar_t* s = (wchar_t*)str.c_str();
    size_t c = str.size();
	bool found = false;

    while (left <= right) {
        mid = left + (right - left) / 2;
        const wchar_t* v = yenLambda(g_dirs[mid]);

        auto cmp = wcsncmp(v, s, c);
        if (cmp == 0) {
            found = true;
            break;
        } else if (cmp < 0) {
            left = mid + 1;
        } else {
            right = mid - 1;
        }
    }

    //前方一致するアイテムが見つからなかった
    if (!found)
        return { 0,0 };

    //戻って行って前方一致せぬ奴を探す
    start = mid;
	while (start > 0) {
		const wchar_t* v = yenLambda(g_dirs[start - 1]);
		if (wcsncmp(v, s, c) != 0) {
			break;
		}
		start--;
	}

	//進んで行って前方一致せぬ奴を探す
    end = mid;
	while (end + 1 < g_dirs.size()) {
		const wchar_t* v = yenLambda(g_dirs[end + 1]);
		if (wcsncmp(v, s, c) != 0) {
			break;
		}
		end++;
	}

    return { start, end+1 }; // [start, end) が前方一致範囲
}

/*****************************************************************************
* UI
*****************************************************************************/
int g_cmdIndex = 0;     //実行中のコマンド番号
bool g_search = true;   //入力内容に対して検索をするか？
int g_fontHeight;
int g_editHeight;
std::wstring g_directStr;   //入力文字列

void InitUIFont()
{
    NONCLIENTMETRICSW ncm = {};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);

    g_hUIFont = CreateFontIndirectW(&ncm.lfMessageFont);

    LOGFONTW lf = {};
    // 元フォントの LOGFONT を取得
    if (GetObjectW(g_hUIFont, sizeof(lf), &lf) == 0)
        g_hUIFontBold = g_hUIFont;
    else {
        lf.lfWeight = FW_BOLD;
        g_hUIFontBold = CreateFontIndirectW(&lf);
    }
}

int GetFontHeight(HWND hWnd, HFONT hFont)
{
    HDC hdc = GetDC(hWnd);
    HFONT old = (HFONT)SelectObject(hdc, hFont);

    TEXTMETRICW tm;
    GetTextMetricsW(hdc, &tm);

    SelectObject(hdc, old);
    ReleaseDC(hWnd, hdc);

    // tm.tmHeight は内部行間込みの高さ
    return tm.tmHeight;
}

void AdjustLabelSize(HWND hLabel)
{
    SIZE sz;
    HDC hdc = GetDC(hLabel);
    HFONT hFont = (HFONT)SendMessageW(hLabel, WM_GETFONT, 0, 0);
    HFONT hOld = (HFONT)SelectObject(hdc, hFont);

    constexpr int bufsize = 512;
    wchar_t buf[bufsize];
    GetWindowText(hLabel, buf, bufsize);
    GetTextExtentPoint32W(hdc, buf, lstrlenW(buf), &sz);

    POINT pt;
    RECT rc;
    GetWindowRect(hLabel, &rc);
    pt.x = rc.left;
    pt.y = rc.top;

    // 親ウィンドウ座標系へ変換
    MapWindowPoints(nullptr, g_hSearchWnd, &pt, 1);

    SetWindowPos(hLabel, nullptr, pt.x, pt.y, sz.cx, sz.cy, SWP_NOZORDER);

    SelectObject(hdc, hOld);
    ReleaseDC(hLabel, hdc);
}

void CenterSearchWindow()
{
    // デスクトップの作業領域（タスクバー除く）
    RECT rc;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &rc, 0);

    int screenW = rc.right - rc.left;
    int screenH = rc.bottom - rc.top;

    // 現在のウィンドウサイズを取得
    RECT wr;
    GetWindowRect(g_hSearchWnd, &wr);

    int winW = wr.right - wr.left;
    int winH = wr.bottom - wr.top;

    // 中央位置（少し上寄り）
    int x = rc.left + (screenW - winW) / 2;
    int y = rc.top + (screenH - winH) / 3;

    SetWindowPos(g_hSearchWnd, nullptr,
        x, y, winW, winH,
        SWP_NOZORDER | SWP_NOACTIVATE);
}

void ResizeSearchWindow()
{
    const int paddingX = 12;
    const int paddingY = 10;

    // --- DPI対応フォントの高さ ---
    g_fontHeight = GetFontHeight(g_hSearchWnd, g_hUIFont);
    g_editHeight = g_fontHeight;
    int labelWidth = g_fontHeight + 8;
    int labelHeight = g_fontHeight;

    // --- フォントの平均文字幅を取得 ---
    HDC hdc = GetDC(g_hSearchWnd);
    HFONT old = (HFONT)SelectObject(hdc, g_hUIFont);

    TEXTMETRICW tm;
    GetTextMetricsW(hdc, &tm);

    SelectObject(hdc, old);
    ReleaseDC(g_hSearchWnd, hdc);

    int charWidth = tm.tmAveCharWidth;
    int width32 = charWidth * 32;

    // --- 画面幅の1/4 ---
    RECT rc;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &rc, 0);
    int screenW = rc.right - rc.left;

    int quarter = screenW / 4;

    // --- ウィンドウ幅は広い方 ---
    int winWidth = max(quarter, width32);

    // --- EditBox の幅 ---
    int editWidth = winWidth - paddingX * 2 - labelWidth - 8;

    // --- ウィンドウ高さ ---
    int winHeight = g_editHeight + paddingY * 3 + labelHeight;

    // --- ウィンドウサイズを適用 ---
    SetWindowPos(g_hSearchWnd, nullptr, 0, 0, winWidth, winHeight, SWP_NOMOVE | SWP_NOZORDER);

    // ラベル配置
    SetWindowPos(g_hDriveLabel, nullptr, paddingX, paddingY * 2 + labelHeight, labelWidth, g_editHeight, SWP_NOZORDER);
    SetWindowPos(g_hDescLabel, nullptr, paddingX, paddingY, 256, labelHeight, SWP_NOZORDER);

    // --- EditBox の位置とサイズ ---
    SetWindowPos(g_hEdit, nullptr, paddingX + labelWidth + 8, paddingY * 2 + labelHeight, editWidth, g_editHeight, SWP_NOZORDER);

    // --- 中央配置 ---
    CenterSearchWindow();
}


// アプリケーションのエントリポイント
int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int)
{
    g_hInst = hInstance;

    //多重起動のチェック
    HANDLE hMutex = CreateMutexW(nullptr,FALSE,L"Global\\NenemSdmn_GOwCppSingleInstanceMutex");
    
    if (hMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(hMutex);
        OKDlg(L"GOw c++", L"already launched");
        return 0;
    }


    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    InitUIFont();

    g_appIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON1));
    for (int i = 0; i < 16; i++) {
        g_hAnimIcons.push_back(LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON_A0 + i)));
    }

    // メインウィンドウ（非表示）
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.hIcon = g_appIcon;
    wc.hIconSm = g_appIcon;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"GOW_MAIN";
    RegisterClassExW(&wc);

    g_hMain = CreateWindowW(L"GOW_MAIN", L"", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 300, 200,
        nullptr, nullptr, hInstance, nullptr);

    //ドライブラベル描画の紐づけ
    WNDCLASSW w = {};
    w.lpfnWndProc = DriveLabelProc;   // ← ここで関連付ける
    w.hInstance = hInstance;
    w.lpszClassName = L"DriveLabelClass";
    w.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&w);

    // タスクトレイアイコン
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hMain;
    nid.uID = 1;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = g_appIcon;
    wcscpy_s(nid.szTip, L"GOw C++");
    Shell_NotifyIconW(NIM_ADD, &nid);

    // 500ms ごとにチェック
    SetTimer(g_hMain, 1, 500, nullptr); 

	//前回終了時のカレントドライブを読み込む
	LoadConfig();
	ChangeCurrentDrive(g_drive);

    // ホットキー登録
    for (const auto& cmd : g_cfg.commands)
        RegisterHotKey(g_hMain, HOTKEY_ID + cmd.index, cmd.mod, cmd.vk);

    // メッセージループ
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    //設定の保存
    SaveConfig();
    Shell_NotifyIconW(NIM_DELETE, &nid);
    return 0;
}


// 検索ウィンドウを表示
void ShowSearchWindow()
{
    if (!g_hSearchWnd) {
        // 検索ウィンドウ
        WNDCLASSW wc = {};
        wc.lpfnWndProc = SearchWndProc;
        wc.hInstance = g_hInst;
        wc.lpszClassName = L"GOW_SEARCH";
        RegisterClassW(&wc);

        g_hSearchWnd = CreateWindowExW(
            WS_EX_TOOLWINDOW,
            L"GOW_SEARCH", L"",
            WS_POPUP | WS_BORDER,
            100, 100, 300, 60,
            nullptr, nullptr, g_hInst, nullptr);

        g_hEdit = CreateWindowExW(
            0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, // ← WS_BORDER を削除
            0, 0, 0, 0,
            g_hSearchWnd, nullptr, g_hInst, nullptr);

        SendMessageW(g_hEdit, WM_SETFONT, (WPARAM)g_hUIFont, TRUE);

        std::wstring label = std::format(L"{}:", g_drive);

        g_hDriveLabel = CreateWindowExW(
            0, L"DriveLabelClass", label.c_str(),
            WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0,
            g_hSearchWnd, nullptr, g_hInst, nullptr);

        SendMessageW(g_hDriveLabel, WM_SETFONT, (WPARAM)g_hUIFontBold, TRUE);

        g_hDescLabel = CreateWindowExW(
            0, L"STATIC", L"directory search",
            WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0,
            g_hSearchWnd, nullptr, g_hInst, nullptr);

        SendMessageW(g_hDescLabel, WM_SETFONT, (WPARAM)g_hUIFont, TRUE);


        ResizeSearchWindow(); // ← ここでレイアウト

        //影付けと角丸
        BOOL shadowEnable = TRUE;
        DwmSetWindowAttribute(g_hSearchWnd, DWMWA_NCRENDERING_POLICY, &shadowEnable, sizeof(shadowEnable));
        DWM_WINDOW_CORNER_PREFERENCE pref = DWMWCP_ROUND;
        DwmSetWindowAttribute(g_hSearchWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref) );
        DwmSetWindowAttribute(g_hDescLabel, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));

        // EditBox の WndProc を差し替え
        g_EditOldProc = (WNDPROC)SetWindowLongPtrW(
            g_hEdit, GWLP_WNDPROC, (LONG_PTR)EditProc);
    }

    SetWindowText(g_hDescLabel, g_cfg.commands[g_cmdIndex].desc.c_str());
    AdjustLabelSize(g_hDescLabel);
    SetWindowText(g_hEdit, L"");
    ShowWindow(g_hSearchWnd, SW_SHOW);
    SetForegroundWindow(g_hSearchWnd);
    SetFocus(g_hEdit);
}

// ListBox 専用ウィンドウを作成
void CreateListWindow()
{
    if (g_hListWnd) return;

    WNDCLASSW wc = {};
    wc.lpfnWndProc = ListWndProc;
    wc.hInstance = g_hInst;
    wc.lpszClassName = L"GOW_LISTWND";
    RegisterClassW(&wc);

    g_hListWnd = CreateWindowExW(
         WS_EX_TOPMOST,
        L"GOW_LISTWND", L"",
        WS_POPUP | WS_BORDER,
        0, 0, 200, 200,
        nullptr, nullptr, g_hInst, nullptr);

    //影付けと角丸
    BOOL shadowEnable = TRUE;
    DwmSetWindowAttribute(g_hListWnd, DWMWA_NCRENDERING_POLICY, &shadowEnable, sizeof(shadowEnable));
    DWM_WINDOW_CORNER_PREFERENCE pref = DWMWCP_ROUND;
    DwmSetWindowAttribute(g_hListWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));

    g_hListBox = CreateWindowExW(
        0, L"LISTBOX", nullptr,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY,
        0, 0, 0, 0,
        g_hListWnd, (HMENU)IDC_LISTBOX, g_hInst, nullptr);

    SendMessageW(g_hListBox, WM_SETFONT, (WPARAM)g_hUIFont, TRUE);


    g_ListBoxOldProc = (WNDPROC)SetWindowLongPtrW(
        g_hListBox, GWLP_WNDPROC, (LONG_PTR)ListBoxProc);
}

// ListBox 専用ウィンドウにアイテムを表示
void ShowListWindow(const std::vector<std::wstring>& items)
{
    if (!g_hListWnd)
        CreateListWindow();

    SendMessageW(g_hListBox, LB_RESETCONTENT, 0, 0);

    for (auto& s : items)
        SendMessageW(g_hListBox, LB_ADDSTRING, 0, (LPARAM)s.c_str());

    if (items.empty()) {
        ShowWindow(g_hListWnd, SW_HIDE);
        return;
    }

    // まず選択解除してから先頭にカーソル
    SendMessageW(g_hListBox, LB_SETCURSEL, (WPARAM)-1, 0);
    SendMessageW(g_hListBox, LB_SETCURSEL, 0, 0);
    SendMessageW(g_hListBox, LB_SETTOPINDEX, 0, 0); //スクロール位置を先頭に

    // --- 検索ウィンドウの下に配置 ---
    RECT rc;
    GetWindowRect(g_hSearchWnd, &rc);

    int itemHeight = (int)SendMessageW(g_hListBox, LB_GETITEMHEIGHT, 0, 0);

    //横幅を少し増やす
    rc.left -= g_cfg.listWidth/2;
    rc.right += g_cfg.listWidth/2;
    int x = rc.left;
    int y = rc.bottom;
    int w = rc.right - rc.left;
    int h = min(32 * itemHeight, (int)items.size() * itemHeight) + GetSystemMetrics(SM_CYHSCROLL);

    SetWindowPos(g_hListWnd, HWND_TOPMOST, x, y, w, h, SWP_SHOWWINDOW);
	SetFocus(g_hListBox);

    //SetFocusした後キー入力が処理できずに残っている場合、音がするので、メッセージの消化を待ってから進む
    MSG msg;
    while (PeekMessage(&msg, nullptr, WM_KEYFIRST, WM_KEYLAST, PM_REMOVE)) {}
}

// タスクトレイの右クリックメニューを表示
void ShowTrayMenu(HWND hWnd)
{
    HMENU hMenu = CreatePopupMenu();
    AppendMenu(hMenu, MF_STRING, IDC_TRAYMENU, L"Quit(&X)");

    POINT pt;
    GetCursorPos(&pt);

    SetForegroundWindow(hWnd);

    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hWnd, NULL);

    PostMessage(hWnd, WM_NULL, 0, 0);
    DestroyMenu(hMenu);
}

//アプリケーションのメインウィンドウプロシージャ
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_TIMER:
    {
        if (wParam == 1) {
            if (g_hScanThread && !g_scanning) {
                // スレッド終了を確認
                DWORD result = WaitForSingleObject(g_hScanThread, 0);
                if (result == WAIT_OBJECT_0) {
                    CloseHandle(g_hScanThread);
                    g_hScanThread = nullptr;
                    // スキャン結果をコピーして、スキャンバッファをクリア
                    {
                        std::lock_guard<std::mutex> lock(g_scanMutex);
                        g_dirs = g_scanDirs;
                        g_dirs.shrink_to_fit();
                        g_scanDirs.clear();
                        g_scanDirs.shrink_to_fit();
                    }
                    SortDirs();     //スキャン結果はファイル名部分でソート
                    SaveCache();    //キャッシュへ保存

                    UpdateTrayTooltip();
                    ShowScanCompleteNotification();
                    KillTimer(hWnd, 2); //アニメーションタイマ終了
                    UpdateTrayAnimaton();
                }
            } else if (g_scanning) {
                // スキャン中 → 進行度を更新
                UpdateTrayTooltip();
            }
        } else if (wParam == 2) {
            //スキャン用トレイアイコンアニメ
            UpdateTrayAnimaton();
        }
        return 0;
    }
    case WM_HOTKEY: {
        if (HOTKEY_ID <= wParam && wParam < HOTKEY_ID + g_cfg.commands.size()) {
            //コマンドのパースと起動
            g_cmdIndex = wParam - HOTKEY_ID;
            const auto& cmd = g_cfg.commands[g_cmdIndex];
            if (cmd.file == L"*SCAN") {
                ScanCurrentDrive();
            } else {
                //パラメータかファイルに入力が必要なマクロが含まれているか
                if (ContainsAny(cmd.param, { L"%s", L"%c", L"%d", L"%u" }) || ContainsAny(cmd.file, { L"%s", L"%c", L"%d", L"%u" })) {
                    ShowSearchWindow();
                } else {
                    //マクロが無い場合は単純に起動
                    ShellExecuteW(nullptr, L"open", cmd.file.c_str(), cmd.param.c_str(), nullptr, SW_SHOWNORMAL);
                }
            }

        }
        break;
    }

    case WM_TRAYICON:
        if (lParam == WM_LBUTTONUP) {
            //0番のコマンドを起動
            g_cmdIndex = 0;
            ShowSearchWindow();
        } if (lParam == WM_RBUTTONUP)
            ShowTrayMenu(hWnd);

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_TRAYMENU:  // 終了
            PostQuitMessage(0);
            break;
        }
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

//検索ウィンドウのプロシージャ
LRESULT CALLBACK SearchWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE:
        // 背景色（薄いグレー）を作成
        g_hBgBrush = CreateSolidBrush(RGB(245, 245, 245));
        break;

    case WM_SIZE:
    {
        int w = LOWORD(lParam);
        int h = HIWORD(lParam);
        int editHeight = g_editHeight; // EditBox の高さ
        break;
    }

    case WM_CTLCOLOREDIT:
    {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, RGB(245, 245, 245));
        return (LRESULT)g_hBgBrush;
    }
    case WM_DPICHANGED:
    {
        // 推奨の新しいウィンドウ矩形
        RECT* suggested = (RECT*)lParam;
        SetWindowPos(hWnd, nullptr, suggested->left, suggested->top,suggested->right - suggested->left, suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
        // フォントサイズも DPI に合わせて再作成
        InitUIFont();
        SendMessageW(g_hEdit, WM_SETFONT, (WPARAM)g_hUIFont, TRUE);

        // レイアウトを再計算
        ResizeSearchWindow();
        return 0;
    }
    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE) {
            HWND hNew = (HWND)lParam;
            ShowWindow(hWnd, SW_HIDE);
        }
        break;
    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_CLOSE) {
            return 0;
        }
        break;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

std::wstring URLEncode(const std::wstring& str)
{
    std::string utf8 = u8(str);
    std::string out;

    for (unsigned char c : utf8) {
        // 英数字と -_.~ はそのまま
        if (('A' <= c && c <= 'Z') ||
            ('a' <= c && c <= 'z') ||
            ('0' <= c && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            out += c;
        } else {
            char buf[4];
            sprintf_s(buf, "%%%02X", c);
            out += buf;
        }
    }

    return L(out);
}

//マクロを展開
// 入力
//searched : 検索結果文字列
//src : cmd.paramかcmd.file
// 出力
//copy : %cマクロを含むのでitemをコピーすべし
//search : %s,%cマクロのどれかを含むのでLRUリストにitemを入れるべし
std::wstring ExpandMacro(const std::wstring& searched, const std::wstring& src, bool& copy, bool& search)
{
    if (src.find(L"%") == std::wstring::npos)
        return src;

    const auto& cmd = g_cfg.commands[g_cmdIndex];

    //マクロの展開
    bool macro = false; //マクロ展開中フラグ
    std::wstring str;
    for (auto c : src) {
        if (macro) {
            if (c == L's') {
                search = true;
                str += searched;
            } else if (c == L'c') {
                str += searched;
                search = true;
                copy = true;
            } else if (c == L'd') {
                str += g_directStr;
            } else if (c == L'u') {
                str += URLEncode(g_directStr);
            } else if (c == L'%')
                str.push_back(L'%');
            macro = false;
        } else {
            if (c == L'%') {
                macro = true;
            } else {
                str.push_back(c);
            }
        }
    }

    return str;
}

//項目の起動 item : 検索結果
void Launch(const std::wstring& item)
{
    bool copy = false, search = false;
    const auto& cmd = g_cfg.commands[g_cmdIndex];
    std::wstring param = ExpandMacro(item, cmd.param, copy, search);
    std::wstring file = ExpandMacro(item, cmd.file, copy, search);

    //コピーモードの場合はクリップボードにコピー
    if (copy)
        CopyTextToClipboard(item);

    //LRU更新・保存
    if (search) {
        std::wstring s = item.substr(2);    //ドライブレターを省いた部分を保持する
        auto it = std::find(g_LRUs.begin(), g_LRUs.end(), s);
        if (it != g_LRUs.end())
            g_LRUs.erase(it);
        g_LRUs.push_front(s);
        if (g_LRUs.size() > g_cfg.LRUCount) {
            g_LRUs.pop_back();
        }
        SaveLRU();
    }

    ShellExecuteW(nullptr, L"open", file.c_str(), param.c_str(), nullptr, SW_SHOWNORMAL);
}


// EditBox のプロシージャ
LRESULT CALLBACK EditProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CHAR:
        if (wParam == VK_ESCAPE || wParam == VK_RETURN)
            return 0;
    break;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            ShowWindow(GetParent(hWnd), SW_HIDE);
            return 0; // 処理済み
        } else if (wParam == VK_TAB) {
            //TAB : \から始まっている場合、絶対パス指定とみなす。その場合の補間処理
            wchar_t buf[256];
            GetWindowTextW(hWnd, buf, 256);
            std::wstring str = TrimStr(std::wstring(buf));
            std::wstring exdr;
            char dr = g_drive;
            //ドライブレター部分を抽出
            if (str.size() >= 2 && str[1] == L':') {
                exdr = str.substr(2);
                dr = toupper(str[0]);
            } else {
                exdr = str;
            }
            //絶対パス指定の場合、補完
            if (exdr.starts_with(L"\\")) {
                std::vector<std::wstring>v;
                EnumDirectory(std::format(L"{}:{}*", dr, exdr), v);
                if (v.size() == 1) {
                    str = v[0];
                    if (dr != g_drive)
                        str = std::format(L"{}:{}", dr, str);
                    SetWindowTextW(hWnd, str.c_str());
                    auto len = str.size();
                    SendMessageW(hWnd, EM_SETSEL, len, len);
                }
            }
            return 0;
        } else if (wParam == VK_RETURN) {
            bool close = true; //閉じるフラグ
            wchar_t buf[256];
            GetWindowTextW(hWnd, buf, 256);
            g_directStr = buf;   //入力文字列そのまま

            const auto& cmd = g_cfg.commands[g_cmdIndex];
            if (!ContainsAny(cmd.param, { L"%s",L"%c" }) && !ContainsAny(cmd.file, { L"%s",L"%c" })) {
                //検索の必要が無い場合
                Launch(g_directStr);
                ShowWindow(GetParent(hWnd), SW_HIDE);
                return 0;
            }

            //検索実行
            std::vector<std::wstring> foundDirs;
            std::wstring str = TrimStr(LowerStr(g_directStr));   //小文字にして前後の余白は剥いでおく
            
			//ドライブ指定がある場合は、カレントドライブを変更
            if (str.size() >= 2 && str[1] == L':') {
				char drive = (char)str[0];
				ChangeCurrentDrive(drive);
				str = TrimStr(str.substr(2)); //ドライブ指定を削除
            }

            if (!str.empty()) {
                if (str.starts_with(L"\\")) {
                    // \ で始まっている場合、絶対パスをそのまま開こうとする
                    fs::path p = std::format(L"{}:{}", g_drive, str);
                    if (fs::exists(p)) {
                        Launch(p.wstring());
                    } else {
                        //パスが無い場合は音をならすだけ
                        MessageBeep(MB_ICONASTERISK);
                        close = false;
                    }
                } else if (g_dirs.empty()) {
                    //スキャンされてないかも
                    if (g_scanning)
                        OKDlg(L"Scanning in progress", L"Please wait until the current scan is complete.");
                    else if (YesNoDlg(L"Directory cache is empty", L"Directory cache is empty. Do you want to scan the drive now?")) {
                        ScanCurrentDrive();
                    }
                } else {
                    //strの中に\が入ってる場合、絞り込み検索
                    std::wstring senzo = L"";
                    {
                        size_t idx = str.rfind(L'\\');
                        if (idx != std::wstring::npos) {
                            senzo = str.substr(0, idx);
                            str = str.substr(idx + 1);
                        }
                    }
                    //検索する
                    if (!str.empty()) {
                        auto [start, end] = BinSearch(str);
                        for (size_t i = start; i < end; ++i) {
                            foundDirs.push_back(g_dirs[i]);
                        }
                    }
                    //除外リストに入っているなら除外する
                    std::vector<size_t> excluded;
                    for (size_t i = 0; const auto& f : foundDirs) {
                        for (const auto& ex : g_cfg.excludes) {
                            std::wstring item = ex;
                            //ドライブレターあり
                            if (ex.size() >= 2 && ex[1] == L':') {
                                if (toupper(ex[0]) != toupper(g_drive)) {
                                    //ドライブ不一致
                                    continue;
                                }
                                item = item.substr(2);  //ドライブレターを除去
                            }
                            if (f.starts_with(item)) {
                                if (f.size() > item.size() && f[item.size()] != L'\\')
                                    continue;
                                excluded.push_back(i);
                                break;
                            }
                        }
                        i++;
                    }
                    while (!excluded.empty()) {
                        foundDirs.erase(foundDirs.begin() + excluded.back());
                        excluded.pop_back();
                    }

                    //先祖名で絞り込む
                    if (!senzo.empty()) {
                        auto senzos = SplitStr(senzo, L'\\');
                        std::erase_if(foundDirs, [&](const std::wstring& dir) {
                            auto parts = SplitStr(dir, L'\\');
                            parts.pop_back(); //最後のディレクトリ名を削除
                            if (senzos.size() > parts.size()) {
                                return true;    //そもそも先祖の数が多い
                            }
                            size_t isenzo = 0;
                            for (int i = 0; i < parts.size(); i++) {
                                if (parts[i].starts_with(senzos[isenzo]))
                                    isenzo++;
                                if (isenzo >= senzos.size()) {
                                    return false;   //全てマッチしたのでのこす
                                }
                            }
                            return true; //senzosのどれかがマッチせず残った
                            });
                    }

                    //ソート
                    std::sort(foundDirs.begin(), foundDirs.end());

                    //LRUリストに入っているならage
                    for (int i = g_LRUs.size() - 1; i >= 0; i--) {
                        const auto& L = g_LRUs[i];
                        auto it = std::find(foundDirs.begin(), foundDirs.end(), L);
                        if (it != foundDirs.end()) {
                            std::rotate(foundDirs.begin(), it, it + 1);
                        }
                    }

                    //ドライブレターを追加
                    for (auto&& f : foundDirs) {
                        f = std::format(L"{}:{}", g_drive, f);
                    }

                    if (foundDirs.size() >= 2) {
                        //絞り込んだ結果から選択
                        ShowListWindow(foundDirs);
                    } else if (foundDirs.size() == 1) {
                        //1に絞られる
                        Launch(foundDirs[0]);
                    } else {
                        //0個だった
                        //MessageBeep(MB_ICONASTERISK);
                        close = false;
                    }
                }
                    
            }

            if (close)
                ShowWindow(GetParent(hWnd), SW_HIDE);
            return 0; // 処理済み
        }
        break;
    }

    return CallWindowProcW(g_EditOldProc, hWnd, msg, wParam, lParam);
}

// ListBox 専用ウィンドウのプロシージャ
LRESULT CALLBACK ListWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_SIZE:
        MoveWindow(g_hListBox, 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE);
        break;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_LISTBOX) {
            switch (HIWORD(wParam)) {
            case LBN_DBLCLK: {
                int idx = (int)SendMessageW(g_hListBox, LB_GETCURSEL, 0, 0);
                if (idx != LB_ERR) {
                    wchar_t buf[MAX_PATH];
                    SendMessageW(g_hListBox, LB_GETTEXT, idx, (LPARAM)buf);
                    Launch(buf);
                }
                ShowWindow(g_hListWnd, SW_HIDE);
                return 0;
            }
			case LBN_KILLFOCUS:
				ShowWindow(g_hListWnd, SW_HIDE);
				return 0;
            }
        }
        break;

    /*
    case WM_KILLFOCUS:
        ShowWindow(hWnd, SW_HIDE);
        break;
        */
    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE) {
            HWND hNew = (HWND)lParam;
            ShowWindow(hWnd, SW_HIDE);
        }
        break;

    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_CLOSE) {
            return 0;
        }
        break;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ListBox のプロシージャ
LRESULT CALLBACK ListBoxProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CHAR:
        if (wParam == VK_ESCAPE || wParam == VK_RETURN)
            return 0;
    break;    case WM_KEYDOWN:
        if (wParam == VK_RETURN) {
            int idx = (int)SendMessageW(hWnd, LB_GETCURSEL, 0, 0);
            if (idx != LB_ERR) {
                wchar_t buf[MAX_PATH];
                SendMessageW(hWnd, LB_GETTEXT, idx, (LPARAM)buf);
                Launch(buf);
            }
            ShowWindow(g_hListWnd, SW_HIDE);
            ShowWindow(g_hSearchWnd, SW_HIDE);
            return 0;
        } else if (wParam == VK_ESCAPE) {
            ShowWindow(g_hListWnd, SW_HIDE);
            return 0;
        }
        break;
    }

    return CallWindowProcW(g_ListBoxOldProc, hWnd, msg, wParam, lParam);
}


//ラベル描画
LRESULT CALLBACK DriveLabelProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static HFONT hFont = nullptr;
    switch (msg) {
    case WM_SETFONT:
        hFont = (HFONT)wParam;
        // 再描画
        InvalidateRect(hWnd, nullptr, TRUE);
        return 0;
    case WM_GETFONT:
        return (LRESULT)hFont;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        RECT rc;
        GetClientRect(hWnd, &rc);

        HFONT old = nullptr;
        if (hFont)
            old = (HFONT)SelectObject(hdc, hFont);

        // 背景色
        COLORREF bg = RGB(230, 230, 230); // Windows 11 風の薄グレー
        HBRUSH hBrush = CreateSolidBrush(bg);

        // 丸い矩形（角丸）
        int radius = (rc.bottom - rc.top) / 2; // ピル型
        HRGN rgn = CreateRoundRectRgn(
            rc.left, rc.top, rc.right, rc.bottom,
            radius, radius
        );
        FillRgn(hdc, rgn, hBrush);

        // テキスト
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(50, 50, 50));
        wchar_t label[3] = { (wchar_t)g_drive, L':', 0};
        DrawTextW(hdc, label, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        DeleteObject(hBrush);
        DeleteObject(rgn);
        EndPaint(hWnd, &ps);
        return 0;
    }
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}
