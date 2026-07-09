#ifndef WINVER
#define WINVER 0x0601
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef _WIN32_IE
#define _WIN32_IE 0x0700
#endif

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <cwchar>
#include <cstdio>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

#define IDI_APPICON 101

namespace
{
    constexpr wchar_t kAppTitle[] = L"Offset Porter v1.00.8";
    constexpr UINT kLargeTextLimit = 0x7FFFFFFEu;

    enum class GSheetsLayout { AcrossColumns, DownRows };
    enum class CodeTypeAwarenessMode { Off, PS1, PS2 };
    enum class PendingCodeLineMode { None, Ps2Type5Destination, Ps2Type6Setup, Ps1CarrierLow24 };

    struct CodeTypeState
    {
        int SkipDataCodeLines = 0;
        PendingCodeLineMode Pending = PendingCodeLineMode::None;
    };

    struct HexToken
    {
        size_t Index = 0;
        size_t Length = 8;
        std::wstring Value;
    };

    struct CiLess
    {
        bool operator()(const std::wstring& a, const std::wstring& b) const
        {
            return CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()), b.c_str(), static_cast<int>(b.size()), TRUE) == CSTR_LESS_THAN;
        }
    };

    std::wstring ToLowerKey(const std::wstring& s)
    {
        std::wstring out = s;
        std::transform(out.begin(), out.end(), out.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        return out;
    }

    bool IEquals(const std::wstring& a, const std::wstring& b)
    {
        return CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()), b.c_str(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
    }

    std::wstring Trim(const std::wstring& s)
    {
        size_t first = 0;
        while (first < s.size() && std::iswspace(s[first])) ++first;
        size_t last = s.size();
        while (last > first && std::iswspace(s[last - 1])) --last;
        return s.substr(first, last - first);
    }

    std::wstring TrimStart(const std::wstring& s)
    {
        size_t first = 0;
        while (first < s.size() && std::iswspace(s[first])) ++first;
        return s.substr(first);
    }

    bool StartsWith(const std::wstring& s, const wchar_t* prefix)
    {
        const size_t n = std::wcslen(prefix);
        return s.size() >= n && s.compare(0, n, prefix) == 0;
    }

    std::vector<std::wstring> SplitLines(const std::wstring& text)
    {
        std::vector<std::wstring> lines;
        size_t start = 0;
        for (size_t i = 0; i < text.size(); ++i)
        {
            if (text[i] == L'\r' || text[i] == L'\n')
            {
                lines.push_back(text.substr(start, i - start));
                if (text[i] == L'\r' && i + 1 < text.size() && text[i + 1] == L'\n') ++i;
                start = i + 1;
            }
        }
        if (start <= text.size())
            lines.push_back(text.substr(start));
        return lines;
    }

    std::vector<std::wstring> SplitTabs(const std::wstring& line)
    {
        std::vector<std::wstring> parts;
        size_t start = 0;
        for (size_t i = 0; i <= line.size(); ++i)
        {
            if (i == line.size() || line[i] == L'\t')
            {
                parts.push_back(line.substr(start, i - start));
                start = i + 1;
            }
        }
        return parts;
    }

    std::wstring JoinLines(const std::vector<std::wstring>& lines)
    {
        std::wstring out;
        for (size_t i = 0; i < lines.size(); ++i)
        {
            if (i) out += L"\r\n";
            out += lines[i];
        }
        return out;
    }

    bool IsHexChar(wchar_t c)
    {
        return (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f') || (c >= L'A' && c <= L'F');
    }

    int HexNibble(wchar_t c)
    {
        c = static_cast<wchar_t>(std::towupper(c));
        if (c >= L'0' && c <= L'9') return c - L'0';
        if (c >= L'A' && c <= L'F') return c - L'A' + 10;
        return -1;
    }

    int HexNibble(const std::wstring& text, size_t index)
    {
        if (index >= text.size()) return -1;
        return HexNibble(text[index]);
    }

    bool TryParseHex32(std::wstring hex, uint32_t& value)
    {
        hex = Trim(hex);
        if (hex.size() >= 2 && hex[0] == L'0' && (hex[1] == L'x' || hex[1] == L'X'))
            hex = hex.substr(2);
        if (hex.empty() || hex.size() > 8) return false;
        uint32_t v = 0;
        for (wchar_t c : hex)
        {
            int n = HexNibble(c);
            if (n < 0) return false;
            v = static_cast<uint32_t>((v << 4) | static_cast<uint32_t>(n));
        }
        value = v;
        return true;
    }

    std::wstring Hex8(uint32_t value)
    {
        wchar_t buf[9] = {};
        swprintf_s(buf, 9, L"%08X", value);
        return buf;
    }

    bool HasAnyHex8(const std::wstring& s)
    {
        int run = 0;
        for (wchar_t c : s)
        {
            if (IsHexChar(c))
            {
                ++run;
                if (run >= 8) return true;
            }
            else
            {
                run = 0;
            }
        }
        return false;
    }

    bool Hex8AtLineStart(const std::wstring& s)
    {
        size_t i = 0;
        if (i < s.size() && s[i] == L'$') ++i;
        if (i + 8 > s.size()) return false;
        for (size_t j = 0; j < 8; ++j)
            if (!IsHexChar(s[i + j])) return false;
        size_t end = i + 8;
        return end == s.size() || !IsHexChar(s[end]);
    }

    std::vector<HexToken> GetHex8Tokens(const std::wstring& line)
    {
        std::vector<HexToken> tokens;
        const size_t n = line.size();
        for (size_t i = 0; i < n; ++i)
        {
            if (line[i] == L'$')
            {
                const size_t start = i + 1;
                if (start + 8 <= n && (i == 0 || !IsHexChar(line[i - 1])))
                {
                    bool ok = true;
                    for (size_t j = 0; j < 8; ++j)
                    {
                        if (!IsHexChar(line[start + j])) { ok = false; break; }
                    }
                    if (ok && (start + 8 == n || !IsHexChar(line[start + 8])))
                    {
                        tokens.push_back({ start, 8, line.substr(start, 8) });
                        i = start + 7;
                        continue;
                    }
                }
            }

            if (i + 8 <= n && (i == 0 || !IsHexChar(line[i - 1])))
            {
                bool ok = true;
                for (size_t j = 0; j < 8; ++j)
                {
                    if (!IsHexChar(line[i + j])) { ok = false; break; }
                }
                if (ok && (i + 8 == n || !IsHexChar(line[i + 8])))
                {
                    tokens.push_back({ i, 8, line.substr(i, 8) });
                    i += 7;
                }
            }
        }
        return tokens;
    }

    bool FindFirstHex8Anywhere(const std::wstring& line, HexToken& token)
    {
        int run = 0;
        for (size_t i = 0; i < line.size(); ++i)
        {
            if (IsHexChar(line[i]))
            {
                ++run;
                if (run == 8)
                {
                    const size_t start = i - 7;
                    token = { start, 8, line.substr(start, 8) };
                    return true;
                }
            }
            else
            {
                run = 0;
            }
        }
        return false;
    }

    std::wstring ReplaceToken(const std::wstring& line, const HexToken& token, const std::wstring& replacement)
    {
        std::wstring out = line;
        out.replace(token.Index, token.Length, replacement);
        return out;
    }

    std::wstring OffsetFull32(const std::wstring& hexToken, int delta)
    {
        uint32_t addr = 0;
        if (!TryParseHex32(hexToken, addr)) return hexToken;
        const uint32_t conv = static_cast<uint32_t>(static_cast<int32_t>(addr) - delta);
        return Hex8(conv);
    }

    std::wstring OffsetLower28KeepTypeNibble(const std::wstring& hexToken, int delta)
    {
        uint32_t word = 0;
        if (!TryParseHex32(hexToken, word)) return hexToken;
        const uint32_t type = word & 0xF0000000u;
        const uint32_t addr = word & 0x0FFFFFFFu;
        const uint32_t conv = static_cast<uint32_t>(static_cast<int32_t>(addr) - delta) & 0x0FFFFFFFu;
        return Hex8(type | conv);
    }

    std::wstring OffsetLower24KeepTypeByte(const std::wstring& hexToken, int delta)
    {
        uint32_t word = 0;
        if (!TryParseHex32(hexToken, word)) return hexToken;
        const uint32_t type = word & 0xFF000000u;
        const uint32_t addr = word & 0x00FFFFFFu;
        const uint32_t conv = static_cast<uint32_t>(static_cast<int32_t>(addr) - delta) & 0x00FFFFFFu;
        return Hex8(type | conv);
    }

    bool IsZeroToken(const HexToken& token)
    {
        return IEquals(token.Value, L"00000000");
    }

    int CeilDiv(int value, int divisor)
    {
        if (value <= 0) return 0;
        return (value + divisor - 1) / divisor;
    }

    bool TryReadFollowingHexValue(const std::wstring& line, const HexToken& token, uint32_t& value)
    {
        value = 0;
        size_t pos = token.Index + token.Length;
        if (pos >= line.size()) return false;
        if (pos < line.size() && !std::iswspace(line[pos])) return false;
        while (pos < line.size() && std::iswspace(line[pos])) ++pos;
        if (pos < line.size() && line[pos] == L'$') ++pos;
        size_t start = pos;
        int count = 0;
        while (pos < line.size() && IsHexChar(line[pos]) && count < 8)
        {
            ++pos;
            ++count;
        }
        if (count == 0) return false;
        if (pos < line.size() && IsHexChar(line[pos])) return false;
        return TryParseHex32(line.substr(start, count), value);
    }

    std::wstring ConvertTokenLower28(const std::wstring& line, const HexToken& token, int delta)
    {
        return ReplaceToken(line, token, OffsetLower28KeepTypeNibble(token.Value, delta));
    }

    std::wstring ConvertTokenLower24(const std::wstring& line, const HexToken& token, int delta)
    {
        return ReplaceToken(line, token, OffsetLower24KeepTypeByte(token.Value, delta));
    }

    std::wstring ConvertPs2Line(const std::wstring& line, int delta, CodeTypeState& state, const std::vector<HexToken>& tokens)
    {
        if (state.Pending == PendingCodeLineMode::Ps2Type5Destination)
        {
            state.Pending = PendingCodeLineMode::None;
            return IsZeroToken(tokens[0]) ? line : ConvertTokenLower28(line, tokens[0], delta);
        }

        if (state.Pending == PendingCodeLineMode::Ps2Type6Setup)
        {
            state.Pending = PendingCodeLineMode::None;
            const std::wstring setup = tokens[0].Value;
            uint32_t levels = 0;
            if (setup.size() == 8 && TryParseHex32(setup.substr(4, 4), levels))
            {
                const int remainingOffsets = std::max(0, static_cast<int>(levels) - 1);
                state.SkipDataCodeLines = CeilDiv(remainingOffsets, 2);
            }
            return line;
        }

        std::wstring first = tokens[0].Value;
        std::transform(first.begin(), first.end(), first.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towupper(c)); });
        if (first.size() != 8 || IsZeroToken(tokens[0])) return line;

        switch (first[0])
        {
        case L'0': case L'1': case L'2': case L'7': case L'8': case L'9':
        case L'A': case L'C': case L'D': case L'F':
            return ConvertTokenLower28(line, tokens[0], delta);

        case L'3':
        {
            std::wstring converted = line;
            if (tokens.size() >= 2 && !IsZeroToken(tokens[1]))
                converted = ConvertTokenLower28(line, tokens[1], delta);
            const int subtype = HexNibble(first, 2);
            if (subtype == 4 || subtype == 5)
                state.SkipDataCodeLines = 1;
            return converted;
        }

        case L'4':
            state.SkipDataCodeLines = 1;
            return ConvertTokenLower28(line, tokens[0], delta);

        case L'5':
            state.Pending = PendingCodeLineMode::Ps2Type5Destination;
            return ConvertTokenLower28(line, tokens[0], delta);

        case L'6':
            state.Pending = PendingCodeLineMode::Ps2Type6Setup;
            return ConvertTokenLower28(line, tokens[0], delta);

        case L'E':
            if (tokens.size() >= 2 && !IsZeroToken(tokens[1]))
                return ConvertTokenLower28(line, tokens[1], delta);
            return line;

        default:
            return line;
        }
    }

    std::wstring ConvertPs1Line(const std::wstring& line, int delta, CodeTypeState& state, const std::vector<HexToken>& tokens)
    {
        if (state.Pending == PendingCodeLineMode::Ps1CarrierLow24)
        {
            state.Pending = PendingCodeLineMode::None;
            return IsZeroToken(tokens[0]) ? line : ConvertTokenLower24(line, tokens[0], delta);
        }

        std::wstring first = tokens[0].Value;
        std::transform(first.begin(), first.end(), first.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towupper(c)); });
        if (first.size() != 8 || IsZeroToken(tokens[0])) return line;

        const std::wstring prefix2 = first.substr(0, 2);
        const wchar_t type = first[0];

        static const wchar_t* low24Prefixes[] = {
            L"10", L"11", L"20", L"21", L"30", L"31", L"32", L"80", L"81", L"82",
            L"90", L"91", L"92", L"A4", L"C0", L"C3", L"C4", L"C5", L"C6",
            L"D0", L"D1", L"D2", L"D3", L"D7", L"E0", L"E1", L"E2", L"E3", L"F6"
        };
        for (const wchar_t* p : low24Prefixes)
        {
            if (prefix2 == p)
                return ConvertTokenLower24(line, tokens[0], delta);
        }

        if (prefix2 == L"50" || prefix2 == L"53")
        {
            state.Pending = PendingCodeLineMode::Ps1CarrierLow24;
            return line;
        }
        if (prefix2 == L"C2")
        {
            state.Pending = PendingCodeLineMode::Ps1CarrierLow24;
            return ConvertTokenLower24(line, tokens[0], delta);
        }
        if (prefix2 == L"C1" || prefix2 == L"D4" || prefix2 == L"D5" || prefix2 == L"D6")
            return line;

        switch (type)
        {
        case L'0': case L'3': case L'7': case L'8': case L'9': case L'E': case L'F':
            return ConvertTokenLower28(line, tokens[0], delta);

        case L'5':
        {
            std::wstring converted = ConvertTokenLower28(line, tokens[0], delta);
            uint32_t byteCount = 0;
            if (TryReadFollowingHexValue(line, tokens[0], byteCount))
                state.SkipDataCodeLines = CeilDiv(static_cast<int>(byteCount), 6);
            return converted;
        }

        case L'6':
        {
            std::wstring converted = ConvertTokenLower28(line, tokens[0], delta);
            uint32_t sizeField = 0;
            if (TryReadFollowingHexValue(line, tokens[0], sizeField))
                state.SkipDataCodeLines = CeilDiv(static_cast<int>(sizeField) + 2, 6);
            return converted;
        }

        case L'B':
            state.Pending = PendingCodeLineMode::Ps1CarrierLow24;
            return line;

        default:
            return line;
        }
    }

    std::wstring ConvertFirstHexToken(const std::wstring& line, int delta)
    {
        HexToken token;
        if (!FindFirstHex8Anywhere(line, token)) return line;
        return ReplaceToken(line, token, OffsetFull32(token.Value, delta));
    }

    std::wstring ConvertLineForMode(const std::wstring& line, int delta, CodeTypeState& state, CodeTypeAwarenessMode mode)
    {
        if (line.find(L"//") != std::wstring::npos)
            return line;

        if (mode == CodeTypeAwarenessMode::Off)
            return ConvertFirstHexToken(line, delta);

        const auto tokens = GetHex8Tokens(line);
        if (tokens.empty())
            return line;

        if (state.SkipDataCodeLines > 0)
        {
            --state.SkipDataCodeLines;
            return line;
        }

        if (mode == CodeTypeAwarenessMode::PS2)
            return ConvertPs2Line(line, delta, state, tokens);
        return ConvertPs1Line(line, delta, state, tokens);
    }

    std::string WideToUtf8(const std::wstring& text)
    {
        if (text.empty()) return {};
        const int needed = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        if (needed <= 0) return {};
        std::string out(static_cast<size_t>(needed), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), &out[0], needed, nullptr, nullptr);
        return out;
    }

    bool WriteUtf8File(const std::wstring& path, const std::wstring& text)
    {
        FILE* file = nullptr;
        if (_wfopen_s(&file, path.c_str(), L"wb") != 0 || file == nullptr) return false;
        const std::string bytes = WideToUtf8(text);
        bool ok = true;
        if (!bytes.empty())
            ok = fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
        fclose(file);
        return ok;
    }

    std::wstring CsvEscape(const std::wstring& s)
    {
        if (s.find_first_of(L"\"\r\n,") == std::wstring::npos)
            return s;
        std::wstring out = L"\"";
        for (wchar_t c : s)
        {
            if (c == L'\"') out += L"\"\"";
            else out += c;
        }
        out += L"\"";
        return out;
    }

    std::wstring ToCsv(const std::vector<std::vector<std::wstring>>& table)
    {
        std::vector<std::wstring> lines;
        for (const auto& row : table)
        {
            std::wstring line;
            for (size_t i = 0; i < row.size(); ++i)
            {
                if (i) line += L',';
                line += CsvEscape(row[i]);
            }
            lines.push_back(line);
        }
        return JoinLines(lines);
    }

    std::wstring SafeFileName(std::wstring name)
    {
        const std::wstring invalid = L"<>:\"/\\|?*";
        for (wchar_t& c : name)
        {
            if (c < 32 || invalid.find(c) != std::wstring::npos)
                c = L'_';
        }
        if (Trim(name).empty()) name = L"Region";
        return name;
    }

    std::wstring GetWindowTextString(HWND hwnd)
    {
        const int len = GetWindowTextLengthW(hwnd);
        if (len <= 0) return L"";
        std::wstring text(static_cast<size_t>(len + 1), L'\0');
        GetWindowTextW(hwnd, &text[0], len + 1);
        text.resize(static_cast<size_t>(len));
        return text;
    }

    void ApplyWindowFont(HWND hwnd, HFONT font)
    {
        SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }

    void ComboReset(HWND combo)
    {
        SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    }

    int ComboAdd(HWND combo, const std::wstring& text)
    {
        return static_cast<int>(SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str())));
    }

    int ComboGetSel(HWND combo)
    {
        return static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
    }

    void ComboSetSel(HWND combo, int index)
    {
        SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(index), 0);
    }

    std::wstring ComboGetTextAt(HWND combo, int index)
    {
        if (index < 0) return L"";
        const int len = static_cast<int>(SendMessageW(combo, CB_GETLBTEXTLEN, static_cast<WPARAM>(index), 0));
        if (len < 0) return L"";
        std::wstring text(static_cast<size_t>(len + 1), L'\0');
        SendMessageW(combo, CB_GETLBTEXT, static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(&text[0]));
        text.resize(static_cast<size_t>(len));
        return text;
    }

    std::wstring ComboGetSelectedText(HWND combo)
    {
        return ComboGetTextAt(combo, ComboGetSel(combo));
    }

    void ListViewSetCheck(HWND lv, int index, bool checked)
    {
        ListView_SetItemState(lv, index, INDEXTOSTATEIMAGEMASK(checked ? 2 : 1), LVIS_STATEIMAGEMASK);
    }

    bool ListViewGetCheck(HWND lv, int index)
    {
        const UINT state = ListView_GetItemState(lv, index, LVIS_STATEIMAGEMASK);
        return ((state >> 12) & 0xF) == 2;
    }

    std::wstring ListViewGetItemTextString(HWND lv, int row, int subItem = 0)
    {
        wchar_t buf[1024] = {};
        ListView_GetItemText(lv, row, subItem, buf, 1024);
        return buf;
    }

    HFONT CreateUIFont(int pointSize, const wchar_t* face)
    {
        HDC hdc = GetDC(nullptr);
        const int height = -MulDiv(pointSize, GetDeviceCaps(hdc, LOGPIXELSY), 72);
        ReleaseDC(nullptr, hdc);
        return CreateFontW(height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
    }

    std::wstring ShowSaveCsvDialog(HWND owner, const wchar_t* defaultName)
    {
        wchar_t fileName[MAX_PATH] = {};
        wcscpy_s(fileName, defaultName);
        OPENFILENAMEW ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = owner;
        ofn.lpstrFilter = L"CSV files (*.csv)\0*.csv\0All files (*.*)\0*.*\0";
        ofn.lpstrFile = fileName;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
        ofn.lpstrDefExt = L"csv";
        if (GetSaveFileNameW(&ofn)) return fileName;
        return L"";
    }

    std::wstring ShowFolderDialog(HWND owner)
    {
        BROWSEINFOW bi = {};
        bi.hwndOwner = owner;
        bi.lpszTitle = L"Choose folder to write region .txt files";
        bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
        PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
        if (!pidl) return L"";
        wchar_t path[MAX_PATH] = {};
        const BOOL ok = SHGetPathFromIDListW(pidl, path);
        CoTaskMemFree(pidl);
        return ok ? std::wstring(path) : std::wstring();
    }

    struct NamedCodeGroup
    {
        std::wstring Name;
        std::vector<std::wstring> Codes;
    };

    class TextPreviewWindow;
    class GridPreviewWindow;

    class MainWindow
    {
    public:
        MainWindow() = default;
        ~MainWindow()
        {
            if (monoFont_) DeleteObject(monoFont_);
            if (uiFont_) DeleteObject(uiFont_);
        }

        bool Create(HINSTANCE hInstance)
        {
            hInstance_ = hInstance;
            WNDCLASSEXW wc = {};
            wc.cbSize = sizeof(wc);
            wc.lpfnWndProc = MainWindow::WndProc;
            wc.hInstance = hInstance;
            wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APPICON));
            wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
            wc.lpszClassName = L"OffsetPorterMainWindow";
            wc.hIconSm = wc.hIcon;
            RegisterClassExW(&wc);

            hwnd_ = CreateWindowExW(0, wc.lpszClassName, kAppTitle,
                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                CW_USEDEFAULT, CW_USEDEFAULT, 516, 790,
                nullptr, nullptr, hInstance, this);
            return hwnd_ != nullptr;
        }

        void Show(int nCmdShow)
        {
            ShowWindow(hwnd_, nCmdShow);
            UpdateWindow(hwnd_);
        }

    private:
        enum : int
        {
            IDM_GSHEETS = 1001,
            IDM_LAYOUT_ACROSS = 1002,
            IDM_LAYOUT_DOWN = 1003,
            IDC_MAP = 2001,
            IDC_BASE = 2002,
            IDC_CODETYPE = 2003,
            IDC_SELECTALL = 2004,
            IDC_INCLUDEBASE = 2005,
            IDC_TARGETS = 2006,
            IDC_CODES = 2007,
            IDC_CONVERT = 2008,
            IDC_EXPORT = 2009,
            IDC_EXPORT_GO = 2010,
            IDC_STATUS = 2011
        };

        static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
        {
            MainWindow* self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (msg == WM_NCCREATE)
            {
                auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
                self = reinterpret_cast<MainWindow*>(cs->lpCreateParams);
                self->hwnd_ = hwnd;
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            }
            if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);
            return self->HandleMessage(msg, wParam, lParam);
        }

        LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
        {
            switch (msg)
            {
            case WM_CREATE:
                OnCreate();
                return 0;
            case WM_SIZE:
                LayoutControls();
                return 0;
            case WM_COMMAND:
                OnCommand(LOWORD(wParam), HIWORD(wParam), reinterpret_cast<HWND>(lParam));
                return 0;
            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;
            default:
                return DefWindowProcW(hwnd_, msg, wParam, lParam);
            }
        }

        HWND MakeLabel(const wchar_t* text)
        {
            HWND h = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE,
                0, 0, 0, 0, hwnd_, nullptr, hInstance_, nullptr);
            ApplyWindowFont(h, uiFont_);
            return h;
        }

        HWND MakeButton(const wchar_t* text, int id, DWORD extraStyle = BS_PUSHBUTTON)
        {
            HWND h = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | extraStyle,
                0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hInstance_, nullptr);
            ApplyWindowFont(h, uiFont_);
            return h;
        }

        void OnCreate()
        {
            uiFont_ = CreateUIFont(9, L"Segoe UI");
            monoFont_ = CreateUIFont(10, L"Consolas");

            BuildMenu();

            lblMap_ = MakeLabel(L"Region Base Addresses (HEX = Sorting Name)");
            txtMap_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | WS_VSCROLL | WS_HSCROLL,
                0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(IDC_MAP), hInstance_, nullptr);
            ApplyWindowFont(txtMap_, monoFont_);
            SendMessageW(txtMap_, EM_SETLIMITTEXT, kLargeTextLimit, 0);

            lblBase_ = MakeLabel(L"Base Region:");
            cboBase_ = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                0, 0, 0, 200, hwnd_, reinterpret_cast<HMENU>(IDC_BASE), hInstance_, nullptr);
            ApplyWindowFont(cboBase_, uiFont_);

            lblCodeType_ = MakeLabel(L"Code Type:");
            btnCodeType_ = MakeButton(L"Off", IDC_CODETYPE);

            lblTargets_ = MakeLabel(L"Targets:");
            chkSelectAll_ = MakeButton(L"Select all", IDC_SELECTALL, BS_AUTOCHECKBOX);
            chkIncludeBase_ = MakeButton(L"Show base region", IDC_INCLUDEBASE, BS_AUTOCHECKBOX);

            lvTargets_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_NOCOLUMNHEADER | LVS_SINGLESEL,
                0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(IDC_TARGETS), hInstance_, nullptr);
            ApplyWindowFont(lvTargets_, uiFont_);
            ListView_SetExtendedListViewStyle(lvTargets_, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);
            LVCOLUMNW col = {};
            col.mask = LVCF_WIDTH | LVCF_TEXT;
            col.cx = 420;
            col.pszText = const_cast<LPWSTR>(L"Targets");
            ListView_InsertColumn(lvTargets_, 0, &col);

            lblCodes_ = MakeLabel(L"Base-Region Codes");
            txtCodes_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | WS_VSCROLL | WS_HSCROLL,
                0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(IDC_CODES), hInstance_, nullptr);
            ApplyWindowFont(txtCodes_, monoFont_);
            SendMessageW(txtCodes_, EM_SETLIMITTEXT, kLargeTextLimit, 0);

            btnConvert_ = MakeButton(L"Convert -> Preview", IDC_CONVERT);

            lblExport_ = MakeLabel(L"Export To:");
            cboExport_ = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                0, 0, 0, 200, hwnd_, reinterpret_cast<HMENU>(IDC_EXPORT), hInstance_, nullptr);
            ApplyWindowFont(cboExport_, uiFont_);
            btnExport_ = MakeButton(L"Go", IDC_EXPORT_GO);

            lblStatus_ = MakeLabel(L"Ready.");

            ParseMapAndPopulate();
            RefreshExportChoices();
            LayoutControls();
        }

        void BuildMenu()
        {
            menu_ = CreateMenu();
            HMENU options = CreatePopupMenu();
            AppendMenuW(options, MF_STRING, IDM_GSHEETS, L"G-Sheets Mode (tab-split)");
            AppendMenuW(options, MF_SEPARATOR, 0, nullptr);
            HMENU layout = CreatePopupMenu();
            AppendMenuW(layout, MF_STRING, IDM_LAYOUT_ACROSS, L"Across columns");
            AppendMenuW(layout, MF_STRING | MF_CHECKED, IDM_LAYOUT_DOWN, L"Down rows");
            AppendMenuW(options, MF_POPUP, reinterpret_cast<UINT_PTR>(layout), L"G-Sheets Layout");
            AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(options), L"Options");
            SetMenu(hwnd_, menu_);
        }

        void LayoutControls()
        {
            RECT rc{};
            GetClientRect(hwnd_, &rc);
            const int w = rc.right - rc.left;
            const int margin = 8;
            const int full = w - margin * 2;
            int y = 8;

            MoveWindow(lblMap_, margin, y, full, 20, TRUE); y += 22;
            MoveWindow(txtMap_, margin, y, full, 140, TRUE); y += 148;

            MoveWindow(lblBase_, margin, y + 5, 84, 22, TRUE);
            MoveWindow(cboBase_, margin + 86, y, 170, 200, TRUE);
            MoveWindow(lblCodeType_, margin + 266, y + 5, 74, 22, TRUE);
            MoveWindow(btnCodeType_, margin + 342, y, 58, 25, TRUE);
            y += 32;

            MoveWindow(lblTargets_, margin, y + 5, 64, 22, TRUE);
            MoveWindow(chkSelectAll_, margin + 78, y, 100, 24, TRUE);
            MoveWindow(chkIncludeBase_, margin + 190, y, 150, 24, TRUE);
            y += 28;

            MoveWindow(lvTargets_, margin, y, full, 130, TRUE);
            ListView_SetColumnWidth(lvTargets_, 0, full - 8);
            y += 138;

            MoveWindow(lblCodes_, margin, y, full, 20, TRUE); y += 22;

            const int bottomReserve = 112;
            int codeHeight = (rc.bottom - margin - bottomReserve) - y;
            if (codeHeight < 190) codeHeight = 190;
            MoveWindow(txtCodes_, margin, y, full, codeHeight, TRUE); y += codeHeight + 8;

            MoveWindow(btnConvert_, margin, y, 140, 27, TRUE); y += 36;

            MoveWindow(lblExport_, margin, y + 5, 70, 22, TRUE);
            MoveWindow(cboExport_, margin + 72, y, 240, 200, TRUE);
            MoveWindow(btnExport_, margin + 320, y, 45, 25, TRUE); y += 34;

            MoveWindow(lblStatus_, margin, y, full, 22, TRUE);
        }

        void OnCommand(int id, int notify, HWND from)
        {
            switch (id)
            {
            case IDM_GSHEETS:
                gSheetsMode_ = !gSheetsMode_;
                CheckMenuItem(menu_, IDM_GSHEETS, MF_BYCOMMAND | (gSheetsMode_ ? MF_CHECKED : MF_UNCHECKED));
                RefreshExportChoices();
                SetStatus(gSheetsMode_ ? L"(G-Sheets mode ON)" : L"Ready.");
                return;

            case IDM_LAYOUT_ACROSS:
                layout_ = GSheetsLayout::AcrossColumns;
                CheckMenuItem(menu_, IDM_LAYOUT_ACROSS, MF_BYCOMMAND | MF_CHECKED);
                CheckMenuItem(menu_, IDM_LAYOUT_DOWN, MF_BYCOMMAND | MF_UNCHECKED);
                return;

            case IDM_LAYOUT_DOWN:
                layout_ = GSheetsLayout::DownRows;
                CheckMenuItem(menu_, IDM_LAYOUT_ACROSS, MF_BYCOMMAND | MF_UNCHECKED);
                CheckMenuItem(menu_, IDM_LAYOUT_DOWN, MF_BYCOMMAND | MF_CHECKED);
                return;

            case IDC_MAP:
                if (notify == EN_CHANGE) ParseMapAndPopulate();
                return;

            case IDC_BASE:
                if (notify == CBN_SELCHANGE) PopulateTargetsList();
                return;

            case IDC_CODETYPE:
                if (notify == BN_CLICKED) CycleCodeTypeMode();
                return;

            case IDC_SELECTALL:
                if (notify == BN_CLICKED) SelectAllTargets(Button_GetCheck(chkSelectAll_) == BST_CHECKED);
                return;

            case IDC_CONVERT:
                if (notify == BN_CLICKED) ConvertNow();
                return;

            case IDC_EXPORT_GO:
                if (notify == BN_CLICKED) DoExport();
                return;
            default:
                (void)from;
                return;
            }
        }

        void SetStatus(const std::wstring& status)
        {
            SetWindowTextW(lblStatus_, status.c_str());
        }

        void RefreshExportChoices()
        {
            ComboReset(cboExport_);
            ComboAdd(cboExport_, L"Excel (CSV)");
            if (!gSheetsMode_)
                ComboAdd(cboExport_, L"Text Files (Per Region)");
            ComboAdd(cboExport_, L"Windowed Grid");
            ComboSetSel(cboExport_, 0);
        }

        void CycleCodeTypeMode()
        {
            if (codeTypeMode_ == CodeTypeAwarenessMode::Off)
                codeTypeMode_ = CodeTypeAwarenessMode::PS1;
            else if (codeTypeMode_ == CodeTypeAwarenessMode::PS1)
                codeTypeMode_ = CodeTypeAwarenessMode::PS2;
            else
                codeTypeMode_ = CodeTypeAwarenessMode::Off;

            const wchar_t* text = L"Off";
            if (codeTypeMode_ == CodeTypeAwarenessMode::PS1) text = L"PS1";
            if (codeTypeMode_ == CodeTypeAwarenessMode::PS2) text = L"PS2";
            SetWindowTextW(btnCodeType_, text);

            if (codeTypeMode_ == CodeTypeAwarenessMode::Off)
                SetStatus(L"Code type awareness OFF: simple first-token offsetting.");
            else
            {
                std::wstring s = L"Code type awareness ON: ";
                s += text;
                s += L".";
                SetStatus(s);
            }
        }

        void ParseMapAndPopulate()
        {
            regionBaseAddrs_.clear();
            regionNames_.clear();
            ComboReset(cboBase_);
            ListView_DeleteAllItems(lvTargets_);

            const auto lines = SplitLines(GetWindowTextString(txtMap_));
            for (const auto& raw : lines)
            {
                const std::wstring s = Trim(raw);
                if (s.empty()) continue;
                const size_t eq = s.find(L'=');
                if (eq == std::wstring::npos) continue;
                const std::wstring hex = Trim(s.substr(0, eq));
                const std::wstring name = Trim(s.substr(eq + 1));
                if (name.empty()) continue;
                uint32_t addr = 0;
                if (!TryParseHex32(hex, addr)) continue;
                const std::wstring key = ToLowerKey(name);
                if (regionBaseAddrs_.find(key) == regionBaseAddrs_.end())
                {
                    regionBaseAddrs_[key] = addr;
                    regionNames_.push_back(name);
                    ComboAdd(cboBase_, name);
                }
            }

            if (!regionNames_.empty() && ComboGetSel(cboBase_) < 0)
                ComboSetSel(cboBase_, 0);

            PopulateTargetsList();
        }

        void PopulateTargetsList()
        {
            ListView_DeleteAllItems(lvTargets_);
            std::vector<std::wstring> names = regionNames_;
            std::sort(names.begin(), names.end(), CiLess{});
            const std::wstring baseName = ComboGetSelectedText(cboBase_);

            int row = 0;
            for (const auto& name : names)
            {
                if (IEquals(name, baseName)) continue;
                LVITEMW item = {};
                item.mask = LVIF_TEXT;
                item.iItem = row;
                item.pszText = const_cast<LPWSTR>(name.c_str());
                ListView_InsertItem(lvTargets_, &item);
                ++row;
            }
        }

        void SelectAllTargets(bool checked)
        {
            const int count = ListView_GetItemCount(lvTargets_);
            for (int i = 0; i < count; ++i)
                ListViewSetCheck(lvTargets_, i, checked);
        }

        std::vector<std::wstring> GetRegionOrder(bool includeBase)
        {
            std::vector<std::wstring> list;
            const int count = ListView_GetItemCount(lvTargets_);
            for (int i = 0; i < count; ++i)
            {
                if (ListViewGetCheck(lvTargets_, i))
                    list.push_back(ListViewGetItemTextString(lvTargets_, i));
            }

            const std::wstring baseName = ComboGetSelectedText(cboBase_);
            list.erase(std::remove_if(list.begin(), list.end(), [&](const std::wstring& n) { return IEquals(n, baseName); }), list.end());
            if (includeBase && !baseName.empty())
                list.insert(list.begin(), baseName);
            return list;
        }

        bool TryGetRegionBase(const std::wstring& name, uint32_t& addr) const
        {
            const auto it = regionBaseAddrs_.find(ToLowerKey(name));
            if (it == regionBaseAddrs_.end()) return false;
            addr = it->second;
            return true;
        }

        std::map<std::wstring, int, CiLess> BuildDeltas(const std::vector<std::wstring>& regions)
        {
            std::map<std::wstring, int, CiLess> deltas;
            const std::wstring baseName = ComboGetSelectedText(cboBase_);
            uint32_t baseAddr = 0;
            if (!TryGetRegionBase(baseName, baseAddr)) return deltas;

            for (const auto& r : regions)
            {
                if (IEquals(r, baseName)) deltas[r] = 0;
                else
                {
                    uint32_t targetAddr = 0;
                    if (TryGetRegionBase(r, targetAddr))
                        deltas[r] = static_cast<int>(static_cast<int32_t>(baseAddr - targetAddr));
                    else
                        deltas[r] = 0;
                }
            }
            return deltas;
        }

        std::vector<NamedCodeGroup> ParseTabSplitCodes(const std::vector<std::wstring>& lines)
        {
            std::vector<NamedCodeGroup> groups;
            std::map<std::wstring, size_t, CiLess> indexes;
            std::wstring currentName;
            int groupCounter = 1;

            auto ensureGroup = [&](const std::wstring& requestedName) -> NamedCodeGroup&
            {
                std::wstring name = Trim(requestedName);
                if (name.empty())
                {
                    if (!currentName.empty()) name = currentName;
                    else
                    {
                        wchar_t buf[32] = {};
                        swprintf_s(buf, 32, L"Group %d", groupCounter++);
                        name = buf;
                    }
                }
                currentName = name;
                auto it = indexes.find(name);
                if (it == indexes.end())
                {
                    indexes[name] = groups.size();
                    groups.push_back({ name, {} });
                    return groups.back();
                }
                return groups[it->second];
            };

            for (const auto& raw : lines)
            {
                const std::wstring line = raw;
                if (StartsWith(TrimStart(line), L"//")) continue;

                if (line.find(L'\t') != std::wstring::npos)
                {
                    const auto parts = SplitTabs(line);
                    NamedCodeGroup& group = ensureGroup(parts.empty() ? L"" : parts[0]);
                    for (size_t i = 1; i < parts.size(); ++i)
                    {
                        if (!parts[i].empty()) group.Codes.push_back(parts[i]);
                    }
                    continue;
                }

                const std::wstring trimmed = Trim(line);
                if (trimmed.empty()) continue;

                if (HasAnyHex8(trimmed))
                    ensureGroup(L"").Codes.push_back(trimmed);
                else
                    ensureGroup(trimmed);
            }

            return groups;
        }

        bool HasAnyNameLine(const std::vector<std::wstring>& lines)
        {
            for (const auto& raw : lines)
            {
                const std::wstring s = Trim(raw);
                if (s.empty()) continue;
                if (StartsWith(s, L"//")) continue;
                if (raw.find(L'\t') != std::wstring::npos) return true;
                if (!Hex8AtLineStart(s)) return true;
            }
            return false;
        }

        std::map<std::wstring, std::wstring, CiLess> ConvertLinesForRegions(const std::vector<std::wstring>& lines, const std::map<std::wstring, int, CiLess>& deltas, const std::vector<std::wstring>& regions)
        {
            std::map<std::wstring, std::wstring, CiLess> result;
            for (const auto& r : regions)
            {
                const auto it = deltas.find(r);
                const int delta = it == deltas.end() ? 0 : it->second;
                CodeTypeState state;
                std::vector<std::wstring> outLines;
                outLines.reserve(lines.size());
                for (const auto& raw : lines)
                    outLines.push_back(ConvertLineForMode(raw, delta, state, codeTypeMode_));
                result[r] = JoinLines(outLines);
            }
            return result;
        }

        std::vector<std::vector<std::wstring>> BuildRawLinesGrid(const std::vector<std::wstring>& regions)
        {
            std::vector<std::vector<std::wstring>> table;
            std::vector<std::wstring> header{ L"" };
            header.insert(header.end(), regions.begin(), regions.end());
            table.push_back(header);

            const auto deltas = BuildDeltas(regions);
            std::map<std::wstring, CodeTypeState, CiLess> states;
            for (const auto& r : regions) states[r] = CodeTypeState{};

            const auto lines = SplitLines(GetWindowTextString(txtCodes_));
            for (const auto& raw : lines)
            {
                if (raw.empty() || Trim(raw).empty())
                {
                    table.push_back(std::vector<std::wstring>(regions.size() + 1, L""));
                    continue;
                }

                std::vector<std::wstring> row{ L"" };
                for (const auto& r : regions)
                {
                    const auto it = deltas.find(r);
                    const int delta = it == deltas.end() ? 0 : it->second;
                    row.push_back(ConvertLineForMode(raw, delta, states[r], codeTypeMode_));
                }
                table.push_back(row);
            }
            return table;
        }

        std::vector<std::vector<std::wstring>> BuildCombinedDown(const std::vector<std::wstring>& regions)
        {
            const auto lines = SplitLines(GetWindowTextString(txtCodes_));
            if (!HasAnyNameLine(lines)) return BuildRawLinesGrid(regions);

            const auto groups = ParseTabSplitCodes(lines);
            std::vector<std::vector<std::wstring>> table;
            std::vector<std::wstring> header{ L"Name" };
            header.insert(header.end(), regions.begin(), regions.end());
            table.push_back(header);

            const auto deltas = BuildDeltas(regions);
            for (const auto& group : groups)
            {
                std::vector<std::wstring> nameRow{ group.Name };
                nameRow.resize(regions.size() + 1);
                table.push_back(nameRow);

                std::map<std::wstring, CodeTypeState, CiLess> states;
                for (const auto& r : regions) states[r] = CodeTypeState{};

                for (const auto& rawCell : group.Codes)
                {
                    std::vector<std::wstring> row{ L"" };
                    for (const auto& r : regions)
                    {
                        const auto it = deltas.find(r);
                        const int delta = it == deltas.end() ? 0 : it->second;
                        row.push_back(ConvertLineForMode(rawCell, delta, states[r], codeTypeMode_));
                    }
                    table.push_back(row);
                }
            }
            return table;
        }

        std::vector<std::vector<std::wstring>> BuildCombinedAcross(const std::vector<std::wstring>& regions)
        {
            const auto lines = SplitLines(GetWindowTextString(txtCodes_));
            if (!HasAnyNameLine(lines)) return BuildRawLinesGrid(regions);

            const auto groups = ParseTabSplitCodes(lines);
            size_t maxCells = 0;
            for (const auto& g : groups) maxCells = std::max(maxCells, g.Codes.size());

            std::vector<std::vector<std::wstring>> table;
            std::vector<std::wstring> header{ L"Name" };
            for (const auto& r : regions)
            {
                for (size_t i = 1; i <= maxCells; ++i)
                {
                    header.push_back(r + L" " + std::to_wstring(i));
                }
            }
            table.push_back(header);

            const auto deltas = BuildDeltas(regions);
            for (const auto& group : groups)
            {
                std::vector<std::wstring> row{ group.Name };
                for (const auto& r : regions)
                {
                    const auto it = deltas.find(r);
                    const int delta = it == deltas.end() ? 0 : it->second;
                    CodeTypeState state;
                    for (size_t i = 0; i < maxCells; ++i)
                    {
                        std::wstring cell = i < group.Codes.size() ? group.Codes[i] : L"";
                        row.push_back(ConvertLineForMode(cell, delta, state, codeTypeMode_));
                    }
                }
                table.push_back(row);
            }
            return table;
        }

        void ConvertNow()
        {
            const std::wstring baseName = ComboGetSelectedText(cboBase_);
            if (baseName.empty())
            {
                MessageBoxW(hwnd_, L"Select a Base Region.", L"Offset Porter", MB_ICONWARNING);
                return;
            }
            uint32_t baseAddr = 0;
            if (!TryGetRegionBase(baseName, baseAddr))
            {
                MessageBoxW(hwnd_, L"Base region address missing.", L"Offset Porter", MB_ICONWARNING);
                return;
            }

            const bool includeBase = Button_GetCheck(chkIncludeBase_) == BST_CHECKED;
            const auto regions = GetRegionOrder(includeBase);
            if (regions.empty())
            {
                MessageBoxW(hwnd_, L"Check at least one target region (or enable 'Show base region').", L"Offset Porter", MB_ICONWARNING);
                return;
            }

            if (gSheetsMode_)
            {
                auto table = layout_ == GSheetsLayout::DownRows ? BuildCombinedDown(regions) : BuildCombinedAcross(regions);
                ShowGridPreview(std::move(table));
                SetStatus(L"Preview (grid) ready for " + std::to_wstring(regions.size()) + L" region(s).");
                return;
            }

            const auto deltas = BuildDeltas(regions);
            auto perRegionText = ConvertLinesForRegions(SplitLines(GetWindowTextString(txtCodes_)), deltas, regions);
            ShowTextPreview(std::move(perRegionText));
            SetStatus(L"Preview (text) ready for " + std::to_wstring(regions.size()) + L" region(s).");
        }

        void DoExport()
        {
            const std::wstring opt = ComboGetSelectedText(cboExport_);
            const bool includeBase = Button_GetCheck(chkIncludeBase_) == BST_CHECKED;
            const auto regions = GetRegionOrder(includeBase);

            const std::wstring baseName = ComboGetSelectedText(cboBase_);
            if (baseName.empty())
            {
                MessageBoxW(hwnd_, L"Select a base region.", L"Offset Porter", MB_ICONWARNING);
                return;
            }
            if (regions.empty())
            {
                MessageBoxW(hwnd_, L"Check at least one target region (or enable 'Show base region').", L"Offset Porter", MB_ICONWARNING);
                return;
            }

            if (gSheetsMode_)
            {
                auto table = layout_ == GSheetsLayout::DownRows ? BuildCombinedDown(regions) : BuildCombinedAcross(regions);
                if (StartsWith(opt, L"Excel"))
                {
                    const auto path = ShowSaveCsvDialog(hwnd_, L"RegionPort_GSheets.csv");
                    if (!path.empty())
                    {
                        if (WriteUtf8File(path, ToCsv(table)))
                            MessageBoxW(hwnd_, L"G-Sheets CSV exported.", L"Export", MB_ICONINFORMATION);
                        else
                            MessageBoxW(hwnd_, L"Could not write CSV file.", L"Export", MB_ICONERROR);
                    }
                }
                else
                {
                    ShowGridPreview(std::move(table));
                }
                return;
            }

            uint32_t baseAddr = 0;
            if (!TryGetRegionBase(baseName, baseAddr))
            {
                MessageBoxW(hwnd_, L"Base region address missing.", L"Offset Porter", MB_ICONWARNING);
                return;
            }

            const auto deltas = BuildDeltas(regions);
            if (StartsWith(opt, L"Text"))
            {
                const auto folder = ShowFolderDialog(hwnd_);
                if (!folder.empty())
                {
                    auto perRegion = ConvertLinesForRegions(SplitLines(GetWindowTextString(txtCodes_)), deltas, regions);
                    bool ok = true;
                    for (const auto& kv : perRegion)
                    {
                        const std::wstring path = folder + L"\\" + SafeFileName(kv.first) + L".txt";
                        if (!WriteUtf8File(path, kv.second)) ok = false;
                    }
                    MessageBoxW(hwnd_, ok ? L"Text files exported." : L"Some text files could not be written.", L"Export", ok ? MB_ICONINFORMATION : MB_ICONWARNING);
                }
                return;
            }

            auto tableNormal = BuildCombinedDown(regions);
            if (StartsWith(opt, L"Excel"))
            {
                const auto path = ShowSaveCsvDialog(hwnd_, L"RegionPort_Normal.csv");
                if (!path.empty())
                {
                    if (WriteUtf8File(path, ToCsv(tableNormal)))
                        MessageBoxW(hwnd_, L"CSV exported.", L"Export", MB_ICONINFORMATION);
                    else
                        MessageBoxW(hwnd_, L"Could not write CSV file.", L"Export", MB_ICONERROR);
                }
            }
            else
            {
                ShowGridPreview(std::move(tableNormal));
            }
        }

        void ShowTextPreview(std::map<std::wstring, std::wstring, CiLess> perRegionText);
        void ShowGridPreview(std::vector<std::vector<std::wstring>> table);

        HINSTANCE hInstance_ = nullptr;
        HWND hwnd_ = nullptr;
        HMENU menu_ = nullptr;
        HFONT uiFont_ = nullptr;
        HFONT monoFont_ = nullptr;

        HWND lblMap_ = nullptr;
        HWND txtMap_ = nullptr;
        HWND lblBase_ = nullptr;
        HWND cboBase_ = nullptr;
        HWND lblCodeType_ = nullptr;
        HWND btnCodeType_ = nullptr;
        HWND lblTargets_ = nullptr;
        HWND chkSelectAll_ = nullptr;
        HWND chkIncludeBase_ = nullptr;
        HWND lvTargets_ = nullptr;
        HWND lblCodes_ = nullptr;
        HWND txtCodes_ = nullptr;
        HWND btnConvert_ = nullptr;
        HWND lblExport_ = nullptr;
        HWND cboExport_ = nullptr;
        HWND btnExport_ = nullptr;
        HWND lblStatus_ = nullptr;

        bool gSheetsMode_ = false;
        GSheetsLayout layout_ = GSheetsLayout::DownRows;
        CodeTypeAwarenessMode codeTypeMode_ = CodeTypeAwarenessMode::Off;

        std::vector<std::wstring> regionNames_;
        std::unordered_map<std::wstring, uint32_t> regionBaseAddrs_;
    };

    class TextPreviewWindow
    {
    public:
        explicit TextPreviewWindow(std::map<std::wstring, std::wstring, CiLess> data)
            : data_(std::move(data)) {}

        void Show(HWND owner, HINSTANCE hInstance)
        {
            hInstance_ = hInstance;
            Register();
            hwnd_ = CreateWindowExW(WS_EX_APPWINDOW, ClassName(), L"Offset Porter - Preview (Tab Per Conversion)",
                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 760, 560,
                owner, nullptr, hInstance, this);
            ShowWindow(hwnd_, SW_SHOW);
            UpdateWindow(hwnd_);
        }

    private:
        static const wchar_t* ClassName() { return L"OffsetPorterTextPreview"; }

        static void Register()
        {
            static bool done = false;
            if (done) return;
            WNDCLASSEXW wc = {};
            wc.cbSize = sizeof(wc);
            wc.lpfnWndProc = TextPreviewWindow::WndProc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(IDI_APPICON));
            wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
            wc.lpszClassName = ClassName();
            wc.hIconSm = wc.hIcon;
            RegisterClassExW(&wc);
            done = true;
        }

        static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
        {
            TextPreviewWindow* self = reinterpret_cast<TextPreviewWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (msg == WM_NCCREATE)
            {
                auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
                self = reinterpret_cast<TextPreviewWindow*>(cs->lpCreateParams);
                self->hwnd_ = hwnd;
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            }
            if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);
            return self->HandleMessage(msg, wParam, lParam);
        }

        LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
        {
            switch (msg)
            {
            case WM_CREATE:
                OnCreate();
                return 0;
            case WM_SIZE:
                Layout();
                return 0;
            case WM_NOTIFY:
                if (reinterpret_cast<NMHDR*>(lParam)->hwndFrom == tab_ && reinterpret_cast<NMHDR*>(lParam)->code == TCN_SELCHANGE)
                    UpdateEditText();
                return 0;
            case WM_NCDESTROY:
                if (monoFont_) DeleteObject(monoFont_);
                delete this;
                return 0;
            default:
                return DefWindowProcW(hwnd_, msg, wParam, lParam);
            }
        }

        void OnCreate()
        {
            monoFont_ = CreateUIFont(10, L"Consolas");
            tab_ = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                0, 0, 0, 0, hwnd_, nullptr, hInstance_, nullptr);
            edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL | WS_VSCROLL | WS_HSCROLL,
                0, 0, 0, 0, hwnd_, nullptr, hInstance_, nullptr);
            ApplyWindowFont(edit_, monoFont_);
            SendMessageW(edit_, EM_SETLIMITTEXT, kLargeTextLimit, 0);

            int i = 0;
            for (const auto& kv : data_)
            {
                labels_.push_back(kv.first);
                texts_.push_back(kv.second);
                TCITEMW item = {};
                item.mask = TCIF_TEXT;
                item.pszText = const_cast<LPWSTR>(labels_.back().c_str());
                TabCtrl_InsertItem(tab_, i++, &item);
            }
            if (!texts_.empty()) UpdateEditText();
        }

        void Layout()
        {
            RECT rc{};
            GetClientRect(hwnd_, &rc);
            MoveWindow(tab_, 0, 0, rc.right, rc.bottom, TRUE);
            RECT trc = rc;
            TabCtrl_AdjustRect(tab_, FALSE, &trc);
            MoveWindow(edit_, trc.left + 2, trc.top + 2, (trc.right - trc.left) - 4, (trc.bottom - trc.top) - 4, TRUE);
        }

        void UpdateEditText()
        {
            int sel = TabCtrl_GetCurSel(tab_);
            if (sel < 0 || sel >= static_cast<int>(texts_.size())) sel = 0;
            if (!texts_.empty()) SetWindowTextW(edit_, texts_[static_cast<size_t>(sel)].c_str());
        }

        HINSTANCE hInstance_ = nullptr;
        HWND hwnd_ = nullptr;
        HWND tab_ = nullptr;
        HWND edit_ = nullptr;
        HFONT monoFont_ = nullptr;
        std::map<std::wstring, std::wstring, CiLess> data_;
        std::vector<std::wstring> labels_;
        std::vector<std::wstring> texts_;
    };

    class GridPreviewWindow
    {
    public:
        explicit GridPreviewWindow(std::vector<std::vector<std::wstring>> table)
            : table_(std::move(table)) {}

        void Show(HWND owner, HINSTANCE hInstance)
        {
            hInstance_ = hInstance;
            Register();
            hwnd_ = CreateWindowExW(WS_EX_APPWINDOW, ClassName(), L"Offset Porter - Converted Codes (Grid)",
                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 900, 620,
                owner, nullptr, hInstance, this);
            ShowWindow(hwnd_, SW_SHOW);
            UpdateWindow(hwnd_);
        }

    private:
        static const wchar_t* ClassName() { return L"OffsetPorterGridPreview"; }

        static void Register()
        {
            static bool done = false;
            if (done) return;
            WNDCLASSEXW wc = {};
            wc.cbSize = sizeof(wc);
            wc.lpfnWndProc = GridPreviewWindow::WndProc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(IDI_APPICON));
            wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
            wc.lpszClassName = ClassName();
            wc.hIconSm = wc.hIcon;
            RegisterClassExW(&wc);
            done = true;
        }

        static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
        {
            GridPreviewWindow* self = reinterpret_cast<GridPreviewWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (msg == WM_NCCREATE)
            {
                auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
                self = reinterpret_cast<GridPreviewWindow*>(cs->lpCreateParams);
                self->hwnd_ = hwnd;
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            }
            if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);
            return self->HandleMessage(msg, wParam, lParam);
        }

        LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
        {
            switch (msg)
            {
            case WM_CREATE:
                OnCreate();
                return 0;
            case WM_SIZE:
                Layout();
                return 0;
            case WM_NCDESTROY:
                delete this;
                return 0;
            default:
                return DefWindowProcW(hwnd_, msg, wParam, lParam);
            }
        }

        void OnCreate()
        {
            list_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS,
                0, 0, 0, 0, hwnd_, nullptr, hInstance_, nullptr);
            ListView_SetExtendedListViewStyle(list_, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

            if (table_.empty()) return;
            for (size_t c = 0; c < table_[0].size(); ++c)
            {
                const std::wstring header = table_[0][c].empty() ? (L"C" + std::to_wstring(c)) : table_[0][c];
                LVCOLUMNW col = {};
                col.mask = LVCF_TEXT | LVCF_WIDTH;
                col.pszText = const_cast<LPWSTR>(header.c_str());
                col.cx = c == 0 ? 160 : 180;
                ListView_InsertColumn(list_, static_cast<int>(c), &col);
            }

            for (size_t r = 1; r < table_.size(); ++r)
            {
                LVITEMW item = {};
                item.mask = LVIF_TEXT;
                item.iItem = static_cast<int>(r - 1);
                item.iSubItem = 0;
                const std::wstring first = table_[r].empty() ? L"" : table_[r][0];
                item.pszText = const_cast<LPWSTR>(first.c_str());
                ListView_InsertItem(list_, &item);
                for (size_t c = 1; c < table_[0].size(); ++c)
                {
                    const std::wstring cell = c < table_[r].size() ? table_[r][c] : L"";
                    ListView_SetItemText(list_, static_cast<int>(r - 1), static_cast<int>(c), const_cast<LPWSTR>(cell.c_str()));
                }
            }
        }

        void Layout()
        {
            RECT rc{};
            GetClientRect(hwnd_, &rc);
            MoveWindow(list_, 0, 0, rc.right, rc.bottom, TRUE);
        }

        HINSTANCE hInstance_ = nullptr;
        HWND hwnd_ = nullptr;
        HWND list_ = nullptr;
        std::vector<std::vector<std::wstring>> table_;
    };

    void MainWindow::ShowTextPreview(std::map<std::wstring, std::wstring, CiLess> perRegionText)
    {
        (new TextPreviewWindow(std::move(perRegionText)))->Show(hwnd_, hInstance_);
    }

    void MainWindow::ShowGridPreview(std::vector<std::vector<std::wstring>> table)
    {
        (new GridPreviewWindow(std::move(table)))->Show(hwnd_, hInstance_);
    }
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow)
{
    INITCOMMONCONTROLSEX icc = {};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_WIN95_CLASSES | ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES;
    InitCommonControlsEx(&icc);
    OleInitialize(nullptr);

    MainWindow mainWindow;
    if (!mainWindow.Create(hInstance))
    {
        MessageBoxW(nullptr, L"Fatal error during startup.", L"Offset Porter", MB_ICONERROR);
        OleUninitialize();
        return 1;
    }

    mainWindow.Show(nCmdShow);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    OleUninitialize();
    return static_cast<int>(msg.wParam);
}
