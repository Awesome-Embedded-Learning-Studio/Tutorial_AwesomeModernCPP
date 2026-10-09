// e6_acl.cpp —— E6 lpSecurityAttributes:NULL 不等于"没有 DACL",默认 DACL 从令牌来
//
// 编译(要挂 advapi32:SDDL 转换与安全描述符查询都在这库;WSL 里以相对路径调 MSYS2 UCRT64 g++):
//   /mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra e6_acl.cpp -o e6_acl.exe -ladvapi32
// 运行:
//   chmod +x e6_acl.exe && ./e6_acl.exe
//
// 观察点:
//   (1) 拿 NULL 建 mapping,再 GetSecurityInfo 查它的 DACL:照样有一个(默认 DACL,
//       继承自进程令牌)——"不传安全属性"不是"门敞开",只是"用默认门禁"
//   (2) 用显式 SDDL("D:P" 开头:受保护的自定义 DACL,只留 SYSTEM/管理员/当前用户)
//       建另一个 mapping,查它的 DACL:ACE 数量与授权对象都对得上
//   (3) 单用户机器上"另一个用户打不开"没法实测(没有第二个账户可开进程),理论口径:
//       DACL 没给你权限的 OpenFileMappingW 就是 ERROR_ACCESS_DENIED(err=5)——
//       E1 里 Global\ 的 err=5 就是同一个机制(那是特权,这是 DACL,门禁都是 err=5)
//   ACL 深挖(继承/受限令牌/跨会话)是候补清单里的专篇,这里点到为止

#include "../common/shm_util.hpp"
#include <aclapi.h>
#include <sddl.h>

// 数一数安全描述符里的 DACL 有几个 ACE,顺带打印每个 ACE 授权给谁、什么权限
static void dump_dacl(HANDLE h, const char* tag) {
    PSECURITY_DESCRIPTOR sd = nullptr;
    PACL dacl = nullptr;
    DWORD ok = GetSecurityInfo(h, SE_KERNEL_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr,
                               &dacl, nullptr, &sd);
    BOOL present = FALSE, dfd = FALSE;
    if (ok != ERROR_SUCCESS || !sd) {
        printf("  [%s] GetSecurityInfo 失败 err=%ld\n", tag, ok);
        return;
    }
    GetSecurityDescriptorDacl(sd, &present, &dacl, &dfd);
    if (!present || !dacl) {
        printf("  [%s] DACL:%s(没有 DACL = 所有人全权,和默认相反)\n", tag,
               present ? "present 但空" : "不 present");
        LocalFree(sd);
        return;
    }
    ACL_SIZE_INFORMATION ai{};
    GetAclInformation(dacl, &ai, sizeof ai, AclSizeInformation);
    printf("  [%s] DACL:present,AceCount=%lu(", tag, ai.AceCount);
    for (DWORD i = 0; i < ai.AceCount; ++i) {
        void* ace = nullptr;
        if (GetAce(dacl, i, &ace)) {
            ACCESS_ALLOWED_ACE* a = (ACCESS_ALLOWED_ACE*)ace;
            SID* sid = (SID*)&a->SidStart;
            char acct[64]{}, dom[64]{};
            DWORD alen = sizeof acct - 1, dlen = sizeof dom - 1;
            SID_NAME_USE use{};
            LookupAccountSidA(nullptr, sid, acct, &alen, dom, &dlen, &use);
            printf("%s%s%s\\%s(mask=0x%lX)", i ? "," : "",
                   a->Header.AceType == ACCESS_ALLOWED_ACE_TYPE ? "+" : "?", dom, acct,
                   (unsigned long)a->Mask);
        }
    }
    printf(")\n");
    LocalFree(sd);
}

int main() {
    const DWORD pid = GetCurrentProcessId();
    printf("[E6] lpSecurityAttributes:NULL 的默认门禁 vs 显式 SDDL\n");

    // (1) NULL:默认 DACL
    SetLastError(0);
    HANDLE h1 = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 64 * 1024,
                                   local_name(L"E6a", pid).c_str());
    printf("  NULL 安全属性创建 -> h=%p err=%lu,查它的 DACL:\n", h1, GetLastError());
    dump_dacl(h1, "NULL");

    // (2) 显式 SDDL:D:P(受保护,不继承)只留 SY/BA/当前用户
    HANDLE tok = nullptr;
    OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok);
    DWORD need = 0;
    GetTokenInformation(tok, TokenUser, nullptr, 0, &need);
    unsigned char* tub = (unsigned char*)LocalAlloc(0, need);
    GetTokenInformation(tok, TokenUser, tub, need, &need);
    wchar_t* sidw = nullptr;
    ConvertSidToStringSidW(((TOKEN_USER*)tub)->User.Sid, &sidw);
    printf("  当前用户 SID = %ls\n", sidw);
    std::wstring sddl =
        std::wstring(L"D:P(A;;0xF001F;;;SY)(A;;0xF001F;;;BA)(A;;0xF001F;;;") + sidw + L")";
    SECURITY_DESCRIPTOR* sd = nullptr;
    ULONG sdlen = 0;
    BOOL ok = ConvertStringSecurityDescriptorToSecurityDescriptorW(
        sddl.c_str(), SDDL_REVISION_1, (PSECURITY_DESCRIPTOR*)&sd, &sdlen);
    printf("  SDDL=%ls\n  转换 -> ret=%d(ACE:SY/BA/当前用户,0xF001F=SECTION_ALL_ACCESS)\n",
           sddl.c_str(), ok);
    SECURITY_ATTRIBUTES sa{sizeof sa, sd, FALSE};
    SetLastError(0);
    HANDLE h2 = CreateFileMappingW(INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE, 0, 64 * 1024,
                                   local_name(L"E6b", pid).c_str());
    printf("  显式安全属性创建 -> h=%p err=%lu,查它的 DACL:\n", h2, GetLastError());
    dump_dacl(h2, "SDDL");
    LocalFree(sd);
    LocalFree(sidw);
    LocalFree((HLOCAL)tub);
    CloseHandle(tok);
    CloseHandle(h1);
    CloseHandle(h2);
    printf("  理论口径:DACL 没授权的用户 OpenFileMappingW → err=5(单机单用户,实测留给专篇)\n");
    fflush(stdout);
    return 0;
}
