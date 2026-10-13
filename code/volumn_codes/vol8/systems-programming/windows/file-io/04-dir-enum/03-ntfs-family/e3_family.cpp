// e3_family.cpp —— NTFS 链接家族实测:硬链接 / 符号链接(特权门槛)/ junction / 稀疏文件 / exFAT
// 负对照
//
// 编译:
//   cd 03-ntfs-family && /mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra -I ../common
//   e3_family.cpp -o e3_family.exe
// 运行:
//   chmod +x e3_family.exe && ./e3_family.exe
//
// 观察点:
//   [1] 硬链接 CreateHardLinkW:两个名字同 FileIndex(MFT 记录号)、nNumberOfLinks=2;
//       从一边写、另一边读内容共享;DeleteFileW 掉一个名字,另一个还活着且 nNumberOfLinks=1
//   [2] 符号链接 CreateSymbolicLinkW:开发者模式未开 + 非管理员 -> 1314
//       ERROR_PRIVILEGE_NOT_HELD(ALLOW_UNPRIVILEGED_CREATE 也救不了:那个 flag 要
//       开发者模式背书);cmd 侧 mklink /D 的拒绝记录在 e3_mklink.bat 的捕获里
//   [3] junction(cmd mklink /J,免特权):attr=0x410;枚举"穿过"它看到目标内容;
//       悬空 junction 的两种查询姿态(GetFileAttributesW 跟随 -> 报目标不存在,
//       FindFirstFileW 单名查询 -> 只看链接自身,attr 照常);
//       FSCTL_GET_REPARSE_POINT 读 tag=0xA0000003(MOUNT_POINT)与目标路径;
//       跨卷指到 F:\;RemoveDirectoryW 只删链接、目标毫发无损
//   [4] 稀疏文件 FSCTL_SET_SPARSE:同样在 0 与 1 GiB 处各写 1 字节,普通文件实占
//       ~1 GiB,稀疏文件只占 2 簇;FSCTL_QUERY_ALLOCATED_RANGES 把洞画出来;
//       枚举属性里 FILE_ATTRIBUTE_SPARSE_FILE(0x200)可见
//   [5] 家族合影:FindFirstFileW 枚举家族目录,逐个解码 dwFileAttributes
//   [6] exFAT(D:盘)负对照:硬链接/稀疏/junction 全部被拒——家族是 NTFS 的,不是"文件系统"的
//
// W01 补课实验已有的结论这里只引用不重做:Windows 默认(不带 FILE_SHARE_DELETE)打开的
// 文件,DeleteFileW 直接失败 ERROR_SHARING_VIOLATION——与 POSIX"unlink 永远成功、名字
// 立刻消失"是两个世界,数据见 01-win32-file-io 篇。

#include "win_dir.hpp"

#include <winioctl.h> // FSCTL_SET_SPARSE / FSCTL_QUERY_ALLOCATED_RANGES / FSCTL_GET_REPARSE_POINT

#include <cstdio>
#include <string>

// ---- REPARSE_DATA_BUFFER:官方头在 WDK 的 ntifs.h 里,Win32 SDK 没有;ABI 稳定,手抄 ----
typedef struct _MY_REPARSE_DATA_BUFFER {
    DWORD ReparseTag;
    WORD ReparseDataLength;
    WORD Reserved;
    union {
        struct {
            WORD SubstituteNameOffset;
            WORD SubstituteNameLength;
            WORD PrintNameOffset;
            WORD PrintNameLength;
            ULONG Flags;
            BYTE PathBuffer[1];
        } SymbolicLinkReparseBuffer;
        struct {
            WORD SubstituteNameOffset;
            WORD SubstituteNameLength;
            WORD PrintNameOffset;
            WORD PrintNameLength;
            BYTE PathBuffer[1];
        } MountPointReparseBuffer;
    };
} MY_REPARSE_DATA_BUFFER;

static const wchar_t* kFam =
    L"C:\\Users\\CharlieChen114514\\AppData\\Local\\Temp\\sysprog-direnum\\e3fam";

static std::wstring fam() {
    return kFam;
}

static void make_file(const std::wstring& path, const char* data) {
    unique_handle f{check_win32("CreateFileW", CreateFileW, path.c_str(), GENERIC_WRITE, 0, nullptr,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)};
    DWORD w = 0;
    if (data) {
        check_win32("WriteFile", WriteFile, f.get(), data, (DWORD)lstrlenA(data), &w, nullptr);
    }
}

static std::string read_all(const std::wstring& path) {
    unique_handle f{check_win32("CreateFileW", CreateFileW, path.c_str(), GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr)};
    char buf[256] = {};
    DWORD got = 0;
    check_win32("ReadFile", ReadFile, f.get(), buf, sizeof buf - 1, &got, nullptr);
    return std::string(buf, got);
}

static void append(const std::wstring& path, const char* data) {
    unique_handle f{check_win32("CreateFileW", CreateFileW, path.c_str(), FILE_APPEND_DATA, 0,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
    DWORD w = 0;
    check_win32("WriteFile", WriteFile, f.get(), data, (DWORD)lstrlenA(data), &w, nullptr);
}

// BY_HANDLE_FILE_INFORMATION 的身份证三件套:卷序列号 / 文件索引 / 链接计数
static void print_info(const char* label, const std::wstring& path) {
    unique_handle f{check_win32("CreateFileW", CreateFileW, path.c_str(), 0,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr)};
    BY_HANDLE_FILE_INFORMATION bi{};
    check_win32("GetFileInformationByHandle", GetFileInformationByHandle, f.get(), &bi);
    unsigned long long idx =
        ((unsigned long long)bi.nFileIndexHigh << 32) | (unsigned long long)bi.nFileIndexLow;
    printf("    %-16s FileIndex=0x%016llx  nNumberOfLinks=%lu  VolSer=0x%lx\n", label, idx,
           bi.nNumberOfLinks, bi.dwVolumeSerialNumber);
}

static void attr_bits(DWORD a, char* out) {
    struct {
        DWORD bit;
        const char* name;
    } tab[] = {
        {FILE_ATTRIBUTE_READONLY, "READONLY"},     {FILE_ATTRIBUTE_HIDDEN, "HIDDEN"},
        {FILE_ATTRIBUTE_SYSTEM, "SYSTEM"},         {FILE_ATTRIBUTE_DIRECTORY, "DIRECTORY"},
        {FILE_ATTRIBUTE_ARCHIVE, "ARCHIVE"},       {FILE_ATTRIBUTE_SPARSE_FILE, "SPARSE"},
        {FILE_ATTRIBUTE_REPARSE_POINT, "REPARSE"}, {FILE_ATTRIBUTE_COMPRESSED, "COMPRESSED"},
        {FILE_ATTRIBUTE_ENCRYPTED, "ENCRYPTED"},
    };
    out[0] = '\0';
    for (auto& e : tab) {
        if (a & e.bit) {
            if (out[0]) {
                lstrcatA(out, "|");
            }
            lstrcatA(out, e.name);
        }
    }
}

// 在 offset 处写 1 个字节(SetFilePointerEx 绝对定位)
static void poke_byte(const std::wstring& path, long long offset, char c) {
    unique_handle f{check_win32("CreateFileW", CreateFileW, path.c_str(), GENERIC_WRITE, 0, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
    LARGE_INTEGER pos{.QuadPart = offset};
    check_win32("SetFilePointerEx", SetFilePointerEx, f.get(), pos, nullptr, FILE_BEGIN);
    DWORD w = 0;
    check_win32("WriteFile", WriteFile, f.get(), &c, 1, &w, nullptr);
}

static void print_sizes(const char* label, const std::wstring& path) {
    WIN32_FIND_DATAW fd{};
    unique_find q{check_win32("FindFirstFileW", FindFirstFileW, path.c_str(), &fd)}; // 单名查询
    unsigned long long logical =
        ((unsigned long long)fd.nFileSizeHigh << 32) | (unsigned long long)fd.nFileSizeLow;
    DWORD high = 0;
    DWORD alloc = GetCompressedFileSizeW(path.c_str(), &high); // 稀疏/压缩文件:实占
    unsigned long long allocated = ((unsigned long long)high << 32) | alloc;
    char bits[128];
    attr_bits(fd.dwFileAttributes, bits);
    printf("    %-14s 逻辑大小 %12llu 字节   实占 %12llu 字节   attr=%s\n", label, logical,
           allocated, bits);
}

// FSCTL_QUERY_ALLOCATED_RANGES:把已分配区间画出来(洞=区间之间的空隙)
static void print_ranges(const std::wstring& path) {
    unique_handle f{check_win32("CreateFileW", CreateFileW, path.c_str(), GENERIC_READ, 0, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
    FILE_ALLOCATED_RANGE_BUFFER in{};
    in.Length.QuadPart = 0x40010000; // 从 0 查到 1 GiB 零头,覆盖两个写入点
    FILE_ALLOCATED_RANGE_BUFFER out[16];
    DWORD got = 0;
    if (!DeviceIoControl(f.get(), FSCTL_QUERY_ALLOCATED_RANGES, &in, sizeof in, out, sizeof out,
                         &got, nullptr)) {
        printf("    FSCTL_QUERY_ALLOCATED_RANGES 失败 err=%lu\n", GetLastError());
        return;
    }
    int n = (int)(got / sizeof(FILE_ALLOCATED_RANGE_BUFFER));
    printf("    已分配区间 %d 个:", n);
    for (int i = 0; i < n; ++i) {
        printf(" [%llu..%llu)", (unsigned long long)out[i].FileOffset.QuadPart,
               (unsigned long long)(out[i].FileOffset.QuadPart + out[i].Length.QuadPart));
    }
    printf("\n");
}

// 幂等清理:reparse 目录只删链接不下钻(与 e1/e2 同款思路)
static void rm_tree(const std::wstring& dir) {
    WIN32_FIND_DATAW fd{};
    unique_find h{FindFirstFileW((dir + L"\\*").c_str(), &fd)};
    if (!h) {
        return;
    }
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) {
            continue;
        }
        std::wstring p = dir + L"\\" + fd.cFileName;
        bool subdir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        bool reparse = (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
        if (subdir && !reparse) {
            rm_tree(p);
            RemoveDirectoryW(p.c_str());
        } else if (subdir) {
            RemoveDirectoryW(p.c_str());
        } else {
            DeleteFileW(p.c_str());
        }
    } while (FindNextFileW(h.get(), &fd));
    RemoveDirectoryW(dir.c_str());
}

int main() {
    SetCurrentDirectoryW(L"C:\\"); // 子 cmd 别再抱怨 UNC cwd
    rm_tree(kFam);                 // 幂等:上次崩溃的残留先清掉
    CreateDirectoryW(kFam, nullptr);

    printf("== [1] 硬链接:两个名字,一条 MFT 记录 ==\n");
    {
        std::wstring orig = fam() + L"\\original.txt";
        std::wstring hard = fam() + L"\\hard.txt";
        make_file(orig, "hello ");
        check_win32("CreateHardLinkW", CreateHardLinkW, hard.c_str(), orig.c_str(), nullptr);
        print_info("original.txt", orig);
        print_info("hard.txt", hard);
        append(hard, "world"); // 从 hard 这边续写
        printf("    从 hard.txt 续写 \"world\",再从 original.txt 读:%s\n", read_all(orig).c_str());
        DeleteFileW(orig.c_str()); // 拆掉一个名字
        printf("    DeleteFileW(original.txt) 后,hard.txt 读到 \"%s\"\n", read_all(hard).c_str());
        print_info("hard.txt", hard);
    }

    printf("\n== [2] 符号链接:这台机器不给我造 ==\n");
    {
        std::wstring tgt = fam() + L"\\hard.txt";
        std::wstring sl = fam() + L"\\sl_file.txt";
        SetLastError(1234);
        BOOLEAN r1 = CreateSymbolicLinkW(sl.c_str(), tgt.c_str(), 0);
        DWORD e1 = GetLastError();
        SetLastError(1234);
        BOOLEAN r2 = CreateSymbolicLinkW(sl.c_str(), tgt.c_str(),
                                         SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE);
        DWORD e2 = GetLastError();
        printf("    无 flag            -> ret=%d err=%lu(1314=ERROR_PRIVILEGE_NOT_HELD)\n", r1, e1);
        printf("    ALLOW_UNPRIVILEGED -> ret=%d err=%lu(此 flag 需开发者模式背书,本机未开)\n", r2,
               e2);
        printf("    cmd 侧 mklink /D 的拒绝原话见 e3_mklink_capture.txt\n");
    }

    printf("\n== [3] junction:免特权的目录链接 ==\n");
    {
        // 目标目录(两件小货)
        std::wstring tgt = fam() + L"\\target_dir";
        CreateDirectoryW(tgt.c_str(), nullptr);
        make_file(tgt + L"\\t1.txt", "1");
        make_file(tgt + L"\\t2.txt", "2");
        // mklink /J 造 junction(cmd 侧唯一入口;输出静默,结果程序自己验证)
        std::wstring jn = fam() + L"\\jn_dir";
        int rc = _wsystem((L"cmd /c mklink /J \"" + jn + L"\" \"" + tgt + L"\" >nul 2>&1").c_str());
        printf("    mklink /J jn_dir -> target_dir,rc=%d\n", rc);

        WIN32_FIND_DATAW fd{};
        unique_find q{check_win32("FindFirstFileW", FindFirstFileW, jn.c_str(), &fd)};
        printf("    FindFirstFileW 单名查询(不跟随):attr=0x%lx\n",
               (unsigned long)fd.dwFileAttributes);
        DWORD ga = GetFileAttributesW(jn.c_str());
        printf("    GetFileAttributesW:attr=0x%lx(与单名查询同款,直接读链接自身)\n", ga);

        printf("    枚举 jn_dir\\*(打开即穿过):\n");
        unique_find e{check_win32("FindFirstFileW", FindFirstFileW, (jn + L"\\*").c_str(), &fd)};
        do {
            if (wcscmp(fd.cFileName, L".") && wcscmp(fd.cFileName, L"..")) {
                printf("      %s\n", to_utf8(fd.cFileName).c_str());
            }
        } while (FindNextFileW(e.get(), &fd));

        // reparse tag:链接的"身份证号",junction=0xA0000003 MOUNT_POINT
        unique_handle h{
            check_win32("CreateFileW", CreateFileW, jn.c_str(), 0, 0, nullptr, OPEN_EXISTING,
                        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
        BYTE raw[2048] = {}; // PathBuffer[1] 只是骨架,真身要给足
        auto* rdb = (MY_REPARSE_DATA_BUFFER*)raw;
        DWORD got = 0;
        check_win32("DeviceIoControl(FSCTL_GET_REPARSE_POINT)", DeviceIoControl, h.get(),
                    (DWORD)FSCTL_GET_REPARSE_POINT, nullptr, 0, raw, sizeof raw, &got, nullptr);
        const wchar_t* sub =
            (const wchar_t*)(rdb->MountPointReparseBuffer.PathBuffer +
                             rdb->MountPointReparseBuffer.SubstituteNameOffset / 2);
        WORD subLen = rdb->MountPointReparseBuffer.SubstituteNameLength / 2;
        printf("    reparse tag=0x%08lx(0xA0000003=IO_REPARSE_TAG_MOUNT_POINT) 目标=%.*ls\n",
               rdb->ReparseTag, (int)subLen, sub);

        // 悬空 junction:造得出、跟不动、看得见
        std::wstring dn = fam() + L"\\jn_dangling";
        _wsystem((L"cmd /c mklink /J \"" + dn + L"\" \"" + fam() + L"\\never_created\" >nul 2>&1")
                     .c_str());
        SetLastError(1234);
        DWORD gad = GetFileAttributesW(dn.c_str());
        DWORD egad = GetLastError();
        unique_find qd{check_win32("FindFirstFileW", FindFirstFileW, dn.c_str(), &fd)};
        printf(
            "    悬空 junction:GetFileAttributesW -> 0x%lx err=%lu(哨兵未动=调用成功,它没去跟目标);"
            "FindFirstFileW 单名 -> attr=0x%lx(链接自身,活得很好)\n",
            gad, egad, (unsigned long)fd.dwFileAttributes);

        // 跨卷:C: 盘上的链接指到 F: 盘根
        std::wstring xv = fam() + L"\\jn_crossvol";
        int rcx = _wsystem((L"cmd /c mklink /J \"" + xv + L"\" \"F:\\\" >nul 2>&1").c_str());
        unique_find ex{check_win32("FindFirstFileW", FindFirstFileW, (xv + L"\\*").c_str(), &fd)};
        printf("    跨卷 junction(C: -> F:\\):rc=%d,枚举前 3 项:", rcx);
        int shown = 0;
        do {
            if (wcscmp(fd.cFileName, L".") && wcscmp(fd.cFileName, L"..")) {
                printf(" %s", to_utf8(fd.cFileName).c_str());
                ++shown;
            }
        } while (shown < 3 && FindNextFileW(ex.get(), &fd));
        printf("\n");

        // 删链接:目标无损
        RemoveDirectoryW(jn.c_str());
        unique_find ck{
            check_win32("FindFirstFileW", FindFirstFileW, (tgt + L"\\t1.txt").c_str(), &fd)};
        printf("    RemoveDirectoryW(jn_dir) 后目标里的 t1.txt 还在(单名查询成功,size=%lu)\n",
               (unsigned long)fd.nFileSizeLow);
    }

    printf("\n== [4] 稀疏文件:1 GiB 的账面,两簇的身价 ==\n");
    {
        const long long kFar = 0x40000000LL; // 1 GiB
        std::wstring norm = fam() + L"\\normal.bin";
        std::wstring spar = fam() + L"\\sparse.bin";
        make_file(norm, nullptr);
        make_file(spar, nullptr);
        // sparse.bin 先打稀疏旗(免特权,DeviceIoControl 就行)
        {
            unique_handle f{check_win32("CreateFileW", CreateFileW, spar.c_str(), GENERIC_WRITE, 0,
                                        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
            DWORD b = 0;
            check_win32("DeviceIoControl(FSCTL_SET_SPARSE)", DeviceIoControl, f.get(),
                        (DWORD)FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &b, nullptr);
        }
        poke_byte(norm, 0, 'A');
        poke_byte(norm, kFar, 'B');
        poke_byte(spar, 0, 'A');
        poke_byte(spar, kFar, 'B');
        print_sizes("normal.bin", norm);
        print_sizes("sparse.bin", spar);
        printf("    normal.bin 的已分配区间(中间不是洞,是实打实的零):\n");
        print_ranges(norm);
        printf("    sparse.bin 的已分配区间(1 GiB 的洞):\n");
        print_ranges(spar);
    }

    printf("\n== [5] 家族合影:FindFirstFileW 枚举家族目录 ==\n");
    {
        WIN32_FIND_DATAW fd{};
        unique_find e{check_win32("FindFirstFileW", FindFirstFileW, (fam() + L"\\*").c_str(), &fd)};
        do {
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) {
                continue;
            }
            char bits[128];
            attr_bits(fd.dwFileAttributes, bits);
            unsigned long long sz =
                ((unsigned long long)fd.nFileSizeHigh << 32) | (unsigned long long)fd.nFileSizeLow;
            printf("    %-14s attr=%-28s size=%llu\n", to_utf8(fd.cFileName).c_str(), bits, sz);
        } while (FindNextFileW(e.get(), &fd));
    }

    printf("\n== [6] exFAT 负对照(D:盘,同一套家族全部碰壁) ==\n");
    {
        const wchar_t* kd = L"D:\\sysprog-direnum-e3";
        CreateDirectoryW(kd, nullptr);
        std::wstring base = std::wstring(kd) + L"\\a.txt";
        std::wstring lnk = std::wstring(kd) + L"\\b.txt";
        make_file(base, "x");
        SetLastError(1234);
        BOOL rh = CreateHardLinkW(lnk.c_str(), base.c_str(), nullptr);
        printf("    CreateHardLinkW    -> ret=%d err=%lu\n", rh, GetLastError());
        {
            unique_handle f{check_win32("CreateFileW", CreateFileW,
                                        (std::wstring(kd) + L"\\c.txt").c_str(), GENERIC_WRITE, 0,
                                        nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)};
            DWORD b = 0;
            SetLastError(1234);
            BOOL rs =
                DeviceIoControl(f.get(), FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &b, nullptr);
            printf("    FSCTL_SET_SPARSE   -> ret=%d err=%lu\n", rs, GetLastError());
        }
        SetLastError(1234);
        int rj = _wsystem((L"cmd /c mklink /J \"" + std::wstring(kd) + L"\\jn\" \"" +
                           std::wstring(kd) + L"\" >nul 2>&1")
                              .c_str());
        DWORD aj = GetFileAttributesW((std::wstring(kd) + L"\\jn").c_str());
        printf("    mklink /J          -> rc=%d,链接 attr=0x%lx(不存在=0xFFFFFFFF,造不出来)\n", rj,
               aj);
        DeleteFileW(base.c_str());
        DeleteFileW((std::wstring(kd) + L"\\c.txt").c_str());
        RemoveDirectoryW(kd);
    }

    // ---- 清理 C: 侧 ----
    {
        const wchar_t* kill[] = {L"hard.txt",           L"sl_file.txt",       L"normal.bin",
                                 L"sparse.bin",         L"jn_dangling",       L"jn_crossvol",
                                 L"target_dir\\t1.txt", L"target_dir\\t2.txt"};
        for (const wchar_t* k : kill) {
            DeleteFileW((fam() + L"\\" + k).c_str());
        }
        RemoveDirectoryW((fam() + L"\\target_dir").c_str());
        RemoveDirectoryW(kFam);
        printf("\n(测试目录已清理)\n");
    }
    return 0;
}
