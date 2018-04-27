/*
 * Copyright 2015-2018, Bromium, Inc.
 * Author: Tomasz Wroblewski <tomasz.wroblewski@gmail.com>
 * SPDX-License-Identifier: ISC
 */

#include <dm/config.h>
#include "filecrypt_helper.h"
#include "mappings.h"

#include <iprt/alloc.h>
#include <iprt/assert.h>
#include <iprt/fs.h>
#include <iprt/dir.h>
#include <iprt/file.h>
#include <iprt/path.h>
#include <iprt/string.h>

#include <dm/vbox-drivers/rt/rt.h>
#include <dm/debug.h>
#include <windows.h>
#include <err.h>

#define TEMP_SUFFIX L".uxentmp~"
#define ATOMIC_REWRITE

int
fch_query_crypt_by_path(SHFLCLIENTDATA *client,
                        SHFLROOT root,
                        wchar_t *path,
                        int *crypt_mode)
{
    *crypt_mode = 0;
    if (!path)
        return VERR_INVALID_PARAMETER;
    return vbsfMappingsQueryCrypt(client, root, path, crypt_mode);
}

int
fch_query_crypt_by_handle(SHFLCLIENTDATA *client,
                          SHFLROOT root,
                          SHFLHANDLE handle,
                          int *crypt_mode)
{
    return fch_query_crypt_by_path(
        client, root, vbsfQueryHandleGuestPath(client, handle), crypt_mode);
}

int fch_create_crypt_hdr(SHFLCLIENTDATA *pClient, SHFLROOT root, SHFLHANDLE handle)
{
    filecrypt_hdr_t *hdr;
    SHFLFILEHANDLE *pHandle = vbsfQueryFileHandle(pClient, handle);
    int rc;
    int crypt_mode;

    rc = fch_query_crypt_by_handle(pClient, root, handle, &crypt_mode);
    if (RT_FAILURE(rc))
        return VERR_INVALID_PARAMETER;
    if (!crypt_mode)
        return VINF_SUCCESS;

    if (!pHandle)
        return VERR_INVALID_PARAMETER;

    hdr = fc_init_hdr();
    if (!hdr)
        return VERR_NO_MEMORY;
    rc = fc_write_hdr(pHandle->file.Handle, hdr);
    if (rc) {
        fc_free_hdr(hdr);
        return RTErrConvertFromWin32(rc);
    }

    vbsfModifyHandleFlags(pClient, handle, SHFL_HF_ENCRYPTED, 0);
    vbsfSetHandleCrypt(pClient, handle, hdr);

    return VINF_SUCCESS;
}

int
fch_read_crypt_hdr(SHFLCLIENTDATA *pClient, SHFLROOT root, SHFLHANDLE handle,
                   filecrypt_hdr_t **hdr)
{
    SHFLFILEHANDLE *pHandle = vbsfQueryFileHandle(pClient, handle);
    int rc;
    int file_crypted;
    filecrypt_hdr_t *h = NULL;

    if (hdr) *hdr = NULL;

    if (!pHandle)
        return VERR_INVALID_PARAMETER;

    vbsfModifyHandleFlags(pClient, handle, 0, SHFL_HF_ENCRYPTED);
    vbsfSetHandleCrypt(pClient, handle, NULL);

    rc = fc_read_hdr((HANDLE)(pHandle->file.Handle), &file_crypted, &h);
    if (file_crypted && rc)
        return RTErrConvertFromWin32(rc);
    if (file_crypted) {
        vbsfModifyHandleFlags(pClient, handle, SHFL_HF_ENCRYPTED, 0);
        vbsfSetHandleCrypt(pClient, handle, h);
    }

    if (hdr) *hdr = h;

    return VINF_SUCCESS;
}

uint64_t
fch_host_fileoffset(SHFLCLIENTDATA *pClient, SHFLROOT root, SHFLHANDLE handle,
                    uint64_t guest_off)
{
    filecrypt_hdr_t *hdr;

    if (vbsfQueryHandleFlags(pClient, handle) & SHFL_HF_ENCRYPTED) {
        hdr = vbsfQueryHandleCrypt(pClient, handle);
        Assert(hdr);
        return guest_off + hdr->hdrlen;
    }
    return guest_off;
}

static void _guest_fsinfo_common(filecrypt_hdr_t *crypt, RTFSOBJINFO *info)
{
    if (crypt) {
        /* mod file size */
        info->cbObject -= crypt->hdrlen;
    }
}

void fch_guest_fsinfo(SHFLCLIENTDATA *pClient, SHFLROOT root, SHFLHANDLE handle,
                      RTFSOBJINFO *info)
{
    filecrypt_hdr_t *hdr;

    hdr = vbsfQueryHandleCrypt(pClient, handle);
    if (vbsfQueryHandleFlags(pClient, handle) & SHFL_HF_ENCRYPTED) {
        Assert(hdr);
        _guest_fsinfo_common(hdr, info);
    }
}

void fch_guest_fsinfo_path(SHFLCLIENTDATA *pClient, SHFLROOT root, wchar_t *path,
                           RTFSOBJINFO *info)
{
    int iscrypt;
    filecrypt_hdr_t *crypt;

    fc_path_read_hdr(path, &iscrypt, &crypt);
    if (crypt) {
        _guest_fsinfo_common(crypt, info);
        fc_free_hdr(crypt);
    }
}

int fch_writable_file(SHFLCLIENTDATA *pClient, SHFLROOT root,
                      SHFLHANDLE handle, const wchar_t *path,
                      bool *out_fWritable)
{
    int rc;
    bool fWritable = 0;

    Assert(handle != SHFL_HANDLE_NIL || path);

    *out_fWritable = 0;
    rc = vbsfMappingsQueryWritable(pClient, root, &fWritable);
    if (RT_FAILURE(rc))
        return rc;
    *out_fWritable = fWritable;
    return 0;
}

/*
 * get entry filename for dirname and entry name, dirname typically includes
 * wildcards such as c:\temp\* or c:\temp\foo*
 */
static int dir_entry_filename(wchar_t *dir, wchar_t *entry,
                              wchar_t *filename, size_t filename_sz)
{
    size_t len = wcslen(dir);

    if (len + wcslen(entry) + 2 >= filename_sz)
        return VERR_NO_MEMORY;
    wcscpy(filename, dir);
    while (len) {
        if (filename[len-1] == '\\') {
            filename[len] = 0;
            break;
        }
        --len;
    }
    wcscat(filename, entry);
    return 0;
}

int fch_read_dir_entry_crypthdr(SHFLCLIENTDATA *pClient, SHFLROOT root,
                                wchar_t *dir, wchar_t *entry, filecrypt_hdr_t **crypt)
{
    int rc;
    int iscrypt;
    wchar_t filename[RTPATH_MAX];

    *crypt = NULL;
    if ((rc = dir_entry_filename(dir, entry, filename, RTPATH_MAX)))
        return rc;
    fc_path_read_hdr(filename, &iscrypt, crypt);
    return VINF_SUCCESS;
}

void fch_crypt(SHFLCLIENTDATA *pClient, SHFLHANDLE handle, uint8_t *buf, uint64_t off, uint64_t len)
{
    if (vbsfQueryHandleFlags(pClient, handle) & SHFL_HF_ENCRYPTED) {
        filecrypt_hdr_t *hdr = vbsfQueryHandleCrypt(pClient, handle);

        Assert(hdr);
        fc_crypt(hdr, buf, off, len);
    }
}

void fch_decrypt(SHFLCLIENTDATA *pClient, SHFLHANDLE handle,
                 uint8_t *buf, uint64_t off, uint64_t len)
{
    if (vbsfQueryHandleFlags(pClient, handle) & SHFL_HF_ENCRYPTED) {
        filecrypt_hdr_t *hdr = vbsfQueryHandleCrypt(pClient, handle);

        Assert(hdr);
        fc_decrypt(hdr, buf, off, len);
    }
}

static int
chunk_write(filecrypt_hdr_t *hdr, HANDLE h, void *buf, int cnt)
{
    DWORD n = 0;
    uint8_t *p = (uint8_t*)buf;

    while (cnt>0) {
        if (!(hdr ? fc_write(hdr, h, p, cnt, &n)
                    : WriteFile(h, p, cnt, &n, NULL)))
            return GetLastError();
        if (n == 0)
            return ERROR_WRITE_FAULT;
        p += n;
        cnt -= n;
    }
    return 0;
}

static int
re_write_loop(wchar_t *srcname,
              filecrypt_hdr_t *dsthdr, HANDLE dst)
{
    DWORD n, tot=0;
    HANDLE src;
    filecrypt_hdr_t *srchdr = NULL;
    uint8_t buffer[32768];
    int rc = 0;
    int iscrypt;

    src = CreateFileW(srcname, GENERIC_READ,
                      FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE, NULL,
                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (src == INVALID_HANDLE_VALUE) {
        warnx("error opening file for rewrite %d", (int)GetLastError());
        return RTErrConvertFromWin32(GetLastError());
    }
    rc = fc_read_hdr(src, &iscrypt, &srchdr);
    if (iscrypt && rc)
        return RTErrConvertFromWin32(rc);
    else
        rc = 0;
    SetFilePointer(src, srchdr ? srchdr->hdrlen : 0, NULL, FILE_BEGIN);
    for (;;) {
        BOOL read;

        read = srchdr
            ? fc_read(srchdr, src, buffer, sizeof(buffer), &n)
            : ReadFile(src, buffer, sizeof(buffer), &n, NULL);
        if (!read) {
            rc = RTErrConvertFromWin32(GetLastError());
            warnx("read failure %d", rc);
            break;
        }
        if (!n)
            break; //EOF
        if ((rc = chunk_write(dsthdr, dst, buffer, n))) {
            warnx("write failure %d", rc);
            rc = RTErrConvertFromWin32(rc);
            break;
        }
        tot += n;
    }
    CloseHandle(src);
    fc_free_hdr(srchdr);
    LogRel(("rewritten %d bytes\n", (int)tot));
    return rc;
}

static int
get_final_name(wchar_t *name, wchar_t *final, int nchars)
{
    HANDLE h;

    h = CreateFileW(name, GENERIC_READ,
                    FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE, NULL,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        warnx("error opening file for resolving final path name %d", (int)GetLastError());
        return RTErrConvertFromWin32(GetLastError());
    }
    if (!GetFinalPathNameByHandleW(h, final, nchars, FILE_NAME_NORMALIZED)) {
        warnx("error resolving final path name %d", (int)GetLastError());
        CloseHandle(h);
        return RTErrConvertFromWin32(GetLastError());
    }
    CloseHandle(h);
    return 0;
}

static int
create_temp(wchar_t *name, wchar_t *tempname, int maxlen, HANDLE *temp)
{
    HANDLE h;
    int l;

    l = wcslen(name);
    if (l + wcslen(TEMP_SUFFIX) + 1 >= maxlen)
        return VERR_INVALID_PARAMETER;
    wcscpy(tempname, name);
    wcscat(tempname, TEMP_SUFFIX);
    h = CreateFileW(tempname, GENERIC_WRITE,
                    FILE_SHARE_READ, NULL,
                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return RTErrConvertFromWin32(GetLastError());
    *temp = h;
    return 0;
}

#ifdef ATOMIC_REWRITE
struct replace_params {
    wchar_t *from, *to;
};

static int
replace_action(void *opaque)
{
    int rc = 0;
    struct replace_params *p = (struct replace_params*)opaque;

    if (!ReplaceFileW(p->to, p->from, NULL, 0, NULL, NULL)) {
        warnx("replace file failure %ls->%ls err=%x\n", p->from, p->to, (int)GetLastError());
        rc = RTErrConvertFromWin32(GetLastError());
    }
    return rc;
}

#else

static int
_write(HANDLE file, void *buf, int cnt)
{
    DWORD part = 0;
    uint8_t *p = (uint8_t*)buf;

    while (cnt>0) {
        if (!WriteFile(file, p, cnt, &part, NULL))
            return GetLastError();
        if (part == 0)
            return ERROR_WRITE_FAULT;
        p += part;
        cnt -= part;
    }
    return 0;
}

static int
copy(SHFLCLIENTDATA *client, wchar_t *from, SHFLHANDLE to_shfl)
{
    SHFLFILEHANDLE *to_fh = vbsfQueryFileHandle(client, to_shfl);
    HANDLE to_h, from_h;
    DWORD n;
    int rc = 0;
    uint8_t buffer[32768];

    if (!to_fh)
        return VERR_INVALID_PARAMETER;
    to_h = to_fh->file.Handle;
    from_h = CreateFileW(from, GENERIC_READ,
                         FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE, NULL,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (from_h == INVALID_HANDLE_VALUE) {
        warnx("error opening file for rewrite copy %d", (int)GetLastError());
        return RTErrConvertFromWin32(GetLastError());
    }
    if (SetFilePointer(to_h, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER) {
        warnx("seek error %d\n", (int)GetLastError());
        rc = RTErrConvertFromWin32(GetLastError());
        goto out;
    }
    for (;;) {
        if (!ReadFile(from_h, buffer, sizeof(buffer), &n, NULL)) {
            warnx("read error %d\n", (int)GetLastError());
            rc = RTErrConvertFromWin32(GetLastError());
            goto out;
        }
        if (n == 0)
            break;
        rc = _write(to_h, buffer, n);
        if (rc) {
            rc = RTErrConvertFromWin32(rc);
            goto out;
        }
    }
    if (!SetEndOfFile(to_h)) {
        warnx("set eof error %d\n", (int)GetLastError());
        rc = RTErrConvertFromWin32(GetLastError());
        goto out;
    }
    FlushFileBuffers(to_h);
out:
    CloseHandle(from_h);
    return rc;
}
#endif

static int
fch_re_write_path_with_mode(SHFLCLIENTDATA *client, wchar_t *srcname_, int cmode)
{
    filecrypt_hdr_t *dsthdr = NULL;
    wchar_t  srcname[RTPATH_MAX] = { 0 };
    wchar_t  dstname[RTPATH_MAX] = { 0 };
    int rc;
    int temppresent = 0;
    HANDLE dst = INVALID_HANDLE_VALUE;

    /* follow symlinks */
    if (!srcname_)
        return VERR_INVALID_PARAMETER;
    rc = get_final_name(srcname_, srcname, sizeof(srcname) / sizeof(wchar_t));
    if (rc)
        goto out;
    /* desired crypt mode of target file */
    if (cmode) {
        dsthdr = fc_init_hdr();
        if (!dsthdr) {
            rc = VERR_NO_MEMORY;
            goto out;
        }
    }

    /* create temporary output file and possibly write crypt header */
    rc = create_temp(srcname, dstname, sizeof(dstname) / sizeof(wchar_t),
                     &dst);
    if (rc) {
        warnx("create_temp failure %x\n", rc);
        goto out;
    }
    ++temppresent;
    if (dsthdr) {
        rc = fc_write_hdr(dst, dsthdr);
        if (rc) {
            rc = RTErrConvertFromWin32(rc);
            warnx("fc_write_hdr failure %x\n", rc);
            goto out;
        }
    }

    /* re-write file contents with target encryption in mind */
    rc = re_write_loop(srcname, dsthdr, dst);
    if (rc) {
        warnx("re_write_loop failure %x\n", rc);
        goto out;
    }
    FlushFileBuffers(dst);
    CloseHandle(dst);
    dst = INVALID_HANDLE_VALUE;

#ifdef ATOMIC_REWRITE
    {
        struct replace_params rp;
        rp.from = dstname;
        rp.to = srcname;
        rc = vbsfReopenPathHandles(client, srcname_, &rp, replace_action);
        if (rc)
            warnx("reopen handle failed %x\n", rc);
    }
#else
    rc = copy(client, dstname, src);
    if (rc) {
        warnx("copy failed %x\n", rc);
        goto out;
    }
    rc = vbsfReopenPathHandles(client, srcname_, NULL, NULL);
    if (rc)
        warnx("reopen handle failed %x\n", rc);
#endif
out:
    if (dst != INVALID_HANDLE_VALUE)
        CloseHandle(dst);
    if (temppresent)
        RTFileDeleteUcs(dstname);
    if (dsthdr)
        fc_free_hdr(dsthdr);

    return rc;
}

int
fch_rename_via_copy(SHFLCLIENTDATA *client, wchar_t *srcpath, wchar_t *dstpath,
    int crypt_mode, int flags)
{
    filecrypt_hdr_t *dsthdr = NULL;
    HANDLE hdst = INVALID_HANDLE_VALUE;
    int rc = 0;

    /* desired crypt mode of target file */
    if (crypt_mode) {
        dsthdr = fc_init_hdr();
        if (!dsthdr) {
            rc = VERR_NO_MEMORY;
            goto out;
        }
    }

    hdst = CreateFileW(
        dstpath, GENERIC_WRITE,
        FILE_SHARE_READ, NULL,
        (flags & SHFL_RENAME_REPLACE_IF_EXISTS) ? CREATE_ALWAYS : CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL, NULL);
    if (hdst == INVALID_HANDLE_VALUE) {
        rc = RTErrConvertFromWin32(GetLastError());
        goto out;
    }

    if (dsthdr) {
        rc = fc_write_hdr(hdst, dsthdr);
        if (rc) {
            rc = RTErrConvertFromWin32(rc);
            warnx("fc_write_hdr failure %x\n", rc);
            goto out;
        }
    }

    /* re-write file contents with target encryption in mind */
    rc = re_write_loop(srcpath, dsthdr, hdst);
    if (rc) {
        warnx("re_write_loop failure %x\n", rc);
        goto out;
    }
    FlushFileBuffers(hdst);
    CloseHandle(hdst);
    hdst = INVALID_HANDLE_VALUE;

    /* reopen any existing handles on new file */
    rc = vbsfReopenPathHandles(client, srcpath, NULL, NULL);
    if (rc) {
        warnx("reopen handle failed %x\n", rc);
        goto out;
    }

    /* remove source file */
    RTFileDeleteUcs(srcpath);

out:
    if (hdst != INVALID_HANDLE_VALUE)
        CloseHandle(hdst);
    if (dsthdr)
        fc_free_hdr(dsthdr);
    return rc;
}

int
fch_re_write_file(SHFLCLIENTDATA *client, SHFLROOT root, SHFLHANDLE src)
{
    wchar_t *srcname_ = vbsfQueryHandlePath(client, src);
    int cmode = 0;
    int rc;

    /* desired crypt mode of target file */
    rc = fch_query_crypt_by_handle(client, root, src, &cmode);
    if (rc)
        return rc;
    return fch_re_write_path_with_mode(client, srcname_, cmode);
}
