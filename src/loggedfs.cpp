/*******************************************************************************
 * Author:   Remi Flament <rflament at laposte dot net>
 *******************************************************************************
 * Copyright (c) 2005 - 2008, Remi Flament
 *
 * This library is free software; you can distribute it and/or modify it under
 * the terms of the GNU General Public License (GPL), as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GPL in the file COPYING for more
 * details.
 *
 */

/*******************************************************************************
 * Modifications: Victor Itkin <victor.itkin@gmail.com>
 *******************************************************************************
 */

/* Almost the whole code has been recopied from encfs source code and from 
 * fusexmp.c
 */

#ifdef linux
/* For pread()/pwrite() */
#define _X_SOURCE 500
#endif

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/statfs.h>
#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif
#include <rlog/rlog.h>
#include <rlog/Error.h>
#include <rlog/RLogChannel.h>
#include <rlog/SyslogNode.h>
#include <rlog/StdioNode.h>
#include <stdarg.h>
#include <getopt.h>
#include <sys/time.h>
#include <pwd.h>
#include <grp.h>
#include "Config.h"

#define PUSHARG(ARG) \
rAssert(out->fuseArgc < MaxFuseArgs); \
out->fuseArgv[out->fuseArgc++] = ARG

using namespace std;
using namespace rlog;

static RLogChannel* outChannel = DEF_CHANNEL("out", Log_Info);
static RLogChannel* errChannel = DEF_CHANNEL("err", Log_Info);

static Config config;
static int savefd;
static int fileLog = 0;
static StdioNode* fileLogNode = NULL;

const int MaxFuseArgs = 32;

/* I've added all the following constants for the formatting of the rows in 
 * vision of later externalizing them in the XML configuration.
 * There may be a better way but I don't have time for now.
 */

const string DelimStr = "\t";
const char* Delim = DelimStr.data();

const string RowSuffixStr = DelimStr + "%s" + DelimStr + "%d" + DelimStr +
        "%d" + DelimStr + "%s" + DelimStr + "%s";

const char* RowSuffix = RowSuffixStr.data();

const string HeaderStr = "Time" + DelimStr + "Action" + DelimStr + "Path" +
        DelimStr + "Mode" + DelimStr + "From" + DelimStr + "To" + DelimStr +
        "To UID" + DelimStr + "To GID" + DelimStr + "To User Name" +
        DelimStr + "To Group Name" + DelimStr + "Size" + DelimStr + "Offset" +
        DelimStr + "Result" + DelimStr + "UID" + DelimStr + "PID" + DelimStr +
        "PPID" + DelimStr + "Command Line";

const char* Header = HeaderStr.data();

const string RowPattern1 = DelimStr + "%s" + DelimStr + DelimStr + DelimStr +
        DelimStr + DelimStr + DelimStr + DelimStr + DelimStr + DelimStr;

const string RowPattern2 = DelimStr + "%s" + DelimStr + "%o" + DelimStr +
        DelimStr + DelimStr + DelimStr + DelimStr + DelimStr + DelimStr +
        DelimStr;

const string RowPattern3 = DelimStr + "%s" + DelimStr + DelimStr + "%s" +
        DelimStr + DelimStr + DelimStr + DelimStr + DelimStr + DelimStr +
        DelimStr;

const string RowPattern4 = DelimStr + "%s" + DelimStr + DelimStr + DelimStr +
        DelimStr + DelimStr + DelimStr + DelimStr + DelimStr + "%d" + DelimStr +
        "%d";

const string GetAttrRowStr = DelimStr + "getattr" + RowPattern1;
const char* GetAttrRow = GetAttrRowStr.data();

const string AccessRowStr = DelimStr + "access" + RowPattern1;
const char* AccessRow = AccessRowStr.data();

const string ReadLinkRowStr = DelimStr + "readlink" + RowPattern1;
const char* ReadLinkRow = ReadLinkRowStr.data();

const string ReadDirRowStr = DelimStr + "readdir" + RowPattern1;
const char* ReadDirRow = ReadDirRowStr.data();

const string MkFileRowStr = DelimStr + "mkfile" + RowPattern2;
const char* MkFileRow = MkFileRowStr.data();

const string MkFifoRowStr = DelimStr + "mkfifo" + RowPattern2;
const char* MkFifoRow = MkFifoRowStr.data();

const string MkCharRowStr = DelimStr + "mkchar" + RowPattern2;
const char* MkCharRow = MkCharRowStr.data();

const string MkNodeRowStr = DelimStr + "mknod" + RowPattern2;
const char* MkNodeRow = MkNodeRowStr.data();

const string MkDirRowStr = DelimStr + "mkdir" + RowPattern2;
const char* MkDirRow = MkDirRowStr.data();

const string UnlinkRowStr = DelimStr + "unlink" + RowPattern1;
const char* UnlinkRow = UnlinkRowStr.data();

const string RmDirRowStr = DelimStr + "rmdir" + RowPattern1;
const char* RmDirRow = RmDirRowStr.data();

const string SymLinkRowStr = DelimStr + "symlink" + RowPattern3;
const char* SymLinkRow = SymLinkRowStr.data();

const string RenameRowStr = DelimStr + "rename" + DelimStr + "%s" + DelimStr +
        DelimStr + DelimStr + "%s" + DelimStr + DelimStr + DelimStr + DelimStr +
        DelimStr + DelimStr;

const char* RenameRow = RenameRowStr.data();

const string LinkRowStr = DelimStr + "link" + DelimStr + DelimStr + "%s" +
        DelimStr + DelimStr + DelimStr + "%s" + DelimStr + DelimStr + DelimStr +
        DelimStr + DelimStr;

const char* LinkRow = LinkRowStr.data();

const string ChModRowStr = DelimStr + "chmod" + RowPattern2;
const char* ChModRow = ChModRowStr.data();

const string ChOwnWithNamesRowStr = DelimStr + "chown" + DelimStr + "%s" +
        DelimStr + DelimStr + DelimStr + DelimStr + "%d" + DelimStr + "%d" +
        DelimStr + "%s" + DelimStr + "%s" + DelimStr + DelimStr;

const char* ChOwnWithNamesRow = ChOwnWithNamesRowStr.data();

const string ChOwnRowStr = DelimStr + "chown" + DelimStr + "%s" + DelimStr +
        DelimStr + DelimStr + DelimStr + "%d" + DelimStr + "%d" + DelimStr +
        DelimStr + DelimStr + DelimStr;

const char* ChOwnRow = ChOwnRowStr.data();

const string TruncateRowStr = DelimStr + "truncate" + DelimStr + "%s" +
        DelimStr + DelimStr + DelimStr + DelimStr + DelimStr + DelimStr +
        DelimStr + DelimStr + "%d" + DelimStr;

const char* TruncateRow = TruncateRowStr.data();

const string UTimeRowStr = DelimStr + "utime" + RowPattern1;
const char* UTimeRow = UTimeRowStr.data();

const string UTimeNsRowStr = DelimStr + "utimens" + RowPattern1;
const char* UTimeNsRow = UTimeNsRowStr.data();

const string OpenReadOnlyRowStr = DelimStr + "open-readonly" + RowPattern1;
const char* OpenReadOnlyRow = OpenReadOnlyRowStr.data();

const string OpenWriteOnlyRowStr = DelimStr + "open-writeonly" + RowPattern1;
const char* OpenWriteOnlyRow = OpenWriteOnlyRowStr.data();

const string OpenReadWriteRowStr = DelimStr + "open-readwrite" + RowPattern1;
const char* OpenReadWriteRow = OpenReadWriteRowStr.data();

const string OpenRowStr = DelimStr + "open" + RowPattern1;
const char* OpenRow = OpenRowStr.data();

const string ReadRequestRowStr = DelimStr + "read-request" + RowPattern4;
const char* ReadRequestRow = ReadRequestRowStr.data();

const string ReadRowStr = DelimStr + "read" + RowPattern4;
const char* ReadRow = ReadRowStr.data();

const string WriteRequestRowStr = DelimStr + "write-request" + RowPattern4;
const char* WriteRequestRow = WriteRequestRowStr.data();

const string WriteRowStr = DelimStr + "write" + RowPattern4;
const char* WriteRow = WriteRowStr.data();

const string StatFsRowStr = DelimStr + "statfs" + RowPattern1;
const char* StatFsRow = StatFsRowStr.data();

struct LoggedFS_Args
{
    char* mountPoint; // where the users read files
    char* configFilename;
    bool isDaemon; // true == spawn in background, log to syslog
    const char* fuseArgv[MaxFuseArgs];
    int fuseArgc;
};

/* Added the struct as a comment in case if needed for later
 
struct Fields
{
    char* action;
    char* path;
    mode_t mode;
    char* from;
    char* to;
    uid_t toUid;
    gid_t toGid;
    char* toUserName;
    char* toGroupName;
    size_t size;
    off_t offset;
    char* result;
    pid_t pid;
    char* ppid;
    char* commandLine;
    uid_t uid;
};*/

static LoggedFS_Args* loggedfsArgs = new LoggedFS_Args;

/*******************************************************************************
 */
static bool isAbsolutePath(const char* fileName)
{
    if (fileName && fileName[0] != '\0' && fileName[0] == '/')
        return true;
    else
        return false;
}

/*******************************************************************************
 * Return a dynamically allocated char* for the path.
 * Up to the caller to delete it once used.
 */
static char* getAbsolutePath(const char* path)
{
    char* realPath =
            new char[strlen(path) + strlen(loggedfsArgs->mountPoint) + 1];

    strcpy(realPath, loggedfsArgs->mountPoint);

    if (realPath[strlen(realPath) - 1] == '/')
        realPath[strlen(realPath) - 1] = '\0';

    strcat(realPath, path);

    return realPath;
}

/*******************************************************************************
 * Return a dynamically allocated char* for the path.
 * Up to the caller to delete it once used.
 */
static char* getRelativePath(const char* path)
{
    char* rPath = new char[strlen(path) + 2];

    strcpy(rPath, ".");
    strcat(rPath, path);

    return rPath;
}

/*******************************************************************************
 * Returns the command line of the process which accessed the file system.
 */
static char* getcallercmdline()
{
    char filename[100];
    sprintf(filename, "/proc/%d/cmdline", fuse_get_context()->pid);
    FILE* cmdline = fopen(filename, "rb");

    char* arg = 0;
    size_t size = 0;
    string commandLine = "";

    while (getdelim(&arg, &size, 0, cmdline) != -1)
    {
        char* str1, * str2, * token, * subtoken;
        char* saveptr1, * saveptr2;
        int j;

        for (j = 1, str1 = arg;; j++, str1 = NULL)
        {
            token = strtok_r(str1, "\n", &saveptr1);

            if (token == NULL)
                break;

            if (str1 == NULL)
                commandLine.append("~N~");

            for (str2 = token;; str2 = NULL)
            {
                subtoken = strtok_r(str2, Delim, &saveptr2);

                if (subtoken == NULL)
                    break;

                if (str2 == NULL)
                    commandLine.append("~T~");

                commandLine.append(subtoken);
            }
        }

        commandLine.append(" ");
    }

    free(arg);
    fclose(cmdline);

    return strdup(commandLine.data());
}

/*******************************************************************************
 * Returns the PPID of the process which accessed the file system.
 */
static char* getcallerppid()
{
    char filename[100];
    sprintf(filename, "/proc/%d/stat", fuse_get_context()->pid);
    FILE* stat = fopen(filename, "rt");

    char buf[256] = "";
    fread(buf, sizeof (buf), 1, stat);

    char* token, * saveptr;
    strtok_r(buf, " ", &saveptr);
    strtok_r(NULL, " ", &saveptr);
    strtok_r(NULL, " ", &saveptr);
    token = strtok_r(NULL, " ", &saveptr);

    fclose(stat);

    return strdup(token);
}

/*******************************************************************************
 *
 */
static void loggedfs_log(const char* path, const char* action,
                         const int returncode, const char* format, ...)
{
    char* retname;

    if (returncode >= 0)
        retname = "SUCCESS";
    else
        retname = "FAILURE";

    if (config.shouldLog(path, fuse_get_context()->uid, action, retname))
    {
        va_list args;
        char buf[1024];
        va_start(args, format);
        memset(buf, 0, 1024);
        vsprintf(buf, format, args);
        strcat(buf, RowSuffixStr.data());
        bool pNameEnabled = config.isPrintProcessNameEnabled();

        rLog(outChannel, buf, retname,
             fuse_get_context()->uid,
             fuse_get_context()->pid,
             pNameEnabled ? getcallerppid() : "",
             pNameEnabled ? getcallercmdline() : "");

        va_end(args);
    }
}

/*******************************************************************************
 *
 */
static void* loggedFS_init(struct fuse_conn_info* info)
{
    fchdir(savefd);
    close(savefd);

    return NULL;
}

/*******************************************************************************
 *
 */
static int loggedFS_getattr(const char* path, struct stat* stbuf)
{
    int res;

    const char* aPath = getAbsolutePath(path);
    path = getRelativePath(path);
    res = lstat(path, stbuf);
    loggedfs_log(aPath, "getattr", res, GetAttrRow, aPath);
    delete[] aPath;
    delete[] path;

    if (res == -1)
        return -errno;

    return 0;
}

/*******************************************************************************
 *
 */
static int loggedFS_access(const char *path, int mask)
{
    int res;

    const char* aPath = getAbsolutePath(path);
    path = getRelativePath(path);

    res = access(path, mask);
    delete[] path;

    loggedfs_log(aPath, "access", res, AccessRow, aPath);
    delete[] aPath;


    if (res == -1)
        return -errno;

    return 0;
}

/*******************************************************************************
 *
 */
static int loggedFS_readlink(const char* path, char* buf, size_t size)
{
    int res;

    const char* aPath = getAbsolutePath(path);
    path = getRelativePath(path);

    res = readlink(path, buf, size - 1);
    delete[] path;

    loggedfs_log(aPath, "readlink", res, ReadLinkRow, aPath);
    delete[] aPath;

    if (res == -1)
        return -errno;

    buf[res] = '\0';

    return 0;
}

/*******************************************************************************
 *
 */
static int loggedFS_readdir(const char* path, void* buf, fuse_fill_dir_t filler,
                            off_t offset, struct fuse_file_info* fi)
{
    DIR* dp;
    struct dirent* de;
    int res;

    (void) offset;
    (void) fi;

    const char* aPath = getAbsolutePath(path);
    path = getRelativePath(path);

    dp = opendir(path);

    delete[] path;

    if (dp == NULL)
    {
        res = -errno;
        loggedfs_log(aPath, "readdir", -1, ReadDirRow, aPath);
        delete[] aPath;

        return res;
    }

    while ((de = readdir(dp)) != NULL)
    {
        struct stat st;
        memset(&st, 0, sizeof (st));
        st.st_ino = de->d_ino;
        st.st_mode = de->d_type << 12;

        if (filler(buf, de->d_name, &st, 0))
            break;
    }

    closedir(dp);
    loggedfs_log(aPath, "readdir", 0, ReadDirRow, aPath);
    delete[] aPath;

    return 0;
}

/*******************************************************************************
 *
 */
static int loggedFS_mknod(const char* path, mode_t mode, dev_t rdev)
{
    int res;
    const char* aPath = getAbsolutePath(path);
    path = getRelativePath(path);

    if (S_ISREG(mode))
    {
        res = open(path, O_CREAT | O_EXCL | O_WRONLY, mode);
        loggedfs_log(aPath, "mkfile", res, MkFileRow, aPath, mode);

        if (res >= 0)
            res = close(res);
    }
    else if (S_ISFIFO(mode))
    {
        res = mkfifo(path, mode);
        loggedfs_log(aPath, "mkfifo", res, MkFifoRow, aPath, mode);
    }
    else
    {
        res = mknod(path, mode, rdev);

        if (S_ISCHR(mode))
        {
            loggedfs_log(aPath, "mkchar", res, MkCharRow, aPath, mode);
        }
            /*else if (S_IFBLK(mode))
            {
                loggedfs_log(aPath, "mkblk", res,
                             "mkblk %s %o (block device creation)", aPath, mode);
            }*/
        else
            loggedfs_log(aPath, "mknod", res, MkNodeRow, aPath, mode);
    }

    delete[] aPath;

    if (res == -1)
    {
        delete[] path;

        return -errno;
    }
    else
    {
        lchown(path, fuse_get_context()->uid, fuse_get_context()->gid);
    }

    delete[] path;

    return 0;
}

/*******************************************************************************
 *
 */
static int loggedFS_mkdir(const char* path, mode_t mode)
{
    int res;
    const char* aPath = getAbsolutePath(path);
    path = getRelativePath(path);
    res = mkdir(path, mode);
    const char* relPath = getRelativePath(aPath);
    loggedfs_log(relPath, "mkdir", res, MkDirRow, aPath, mode);
    delete[] relPath;
    delete[] aPath;

    if (res == -1)
    {
        delete[] path;

        return -errno;
    }
    else
        lchown(path, fuse_get_context()->uid, fuse_get_context()->gid);

    delete[] path;

    return 0;
}

/*******************************************************************************
 *
 */
static int loggedFS_unlink(const char* path)
{
    int res;
    const char* aPath = getAbsolutePath(path);
    path = getRelativePath(path);
    res = unlink(path);
    loggedfs_log(aPath, "unlink", res, UnlinkRow, aPath);

    delete[] aPath;
    delete[] path;

    if (res == -1)
        return -errno;

    return 0;
}

/*******************************************************************************
 *
 */
static int loggedFS_rmdir(const char* path)
{
    int res;
    const char* aPath = getAbsolutePath(path);
    path = getRelativePath(path);
    res = rmdir(path);
    loggedfs_log(aPath, "rmdir", res, RmDirRow, aPath);

    delete[] aPath;
    delete[] path;

    if (res == -1)
        return -errno;

    return 0;
}

/*******************************************************************************
 *
 */
static int loggedFS_symlink(const char* from, const char* to)
{
    int res;

    const char* aTo = getAbsolutePath(to);
    to = getRelativePath(to);

    res = symlink(from, to);

    loggedfs_log(aTo, "symlink", res, SymLinkRow, aTo, from);

    delete[] aTo;

    if (res == -1)
    {
        delete[] to;

        return -errno;
    }
    else
        lchown(to, fuse_get_context()->uid, fuse_get_context()->gid);

    delete[] to;

    return 0;
}

/*******************************************************************************
 *
 */
static int loggedFS_rename(const char* from, const char* to)
{
    int res;
    const char* aFrom = getAbsolutePath(from);
    const char* aTo = getAbsolutePath(to);
    from = getRelativePath(from);
    to = getRelativePath(to);

    res = rename(from, to);

    delete[] from;
    delete[] to;

    loggedfs_log(aFrom, "rename", res, RenameRow, aFrom, aTo);

    delete[] aFrom;
    delete[] aTo;

    if (res == -1)
        return -errno;

    return 0;
}

/*******************************************************************************
 *
 */
static int loggedFS_link(const char* from, const char* to)
{
    int res;

    const char* aFrom = getAbsolutePath(from);
    const char* aTo = getAbsolutePath(to);
    from = getRelativePath(from);
    to = getRelativePath(to);

    res = link(from, to);

    delete[] from;

    loggedfs_log(aTo, "link", res, LinkRow, aTo, aFrom);

    delete[] aFrom;

    if (res == -1)
    {
        delete[] aTo;
        delete[] to;

        return -errno;
    }
    else
        lchown(to, fuse_get_context()->uid, fuse_get_context()->gid);

    delete[] aTo;
    delete[] to;

    return 0;
}

/*******************************************************************************
 *
 */
static int loggedFS_chmod(const char* path, mode_t mode)
{
    int res;
    const char* aPath = getAbsolutePath(path);
    path = getRelativePath(path);
    res = chmod(path, mode);
    loggedfs_log(aPath, "chmod", res, ChModRow, aPath, mode);

    delete[] aPath;
    delete[] path;

    if (res == -1)
        return -errno;

    return 0;
}

/*******************************************************************************
 *
 */
static char* getusername(uid_t uid)
{
    struct passwd* p = getpwuid(uid);

    if (p != NULL)
        return p->pw_name;

    return NULL;
}

/*******************************************************************************
 *
 */
static char* getgroupname(gid_t gid)
{
    struct group* g = getgrgid(gid);

    if (g != NULL)
        return g->gr_name;

    return NULL;
}

/*******************************************************************************
 *
 */
static int loggedFS_chown(const char* path, uid_t uid, gid_t gid)
{
    int res;
    const char* aPath = getAbsolutePath(path);
    path = getRelativePath(path);
    res = lchown(path, uid, gid);

    delete[] path;

    char* username = getusername(uid);
    char* groupname = getgroupname(gid);

    if (username != NULL && groupname != NULL)
        loggedfs_log(aPath, "chown", res, ChOwnWithNamesRow, aPath, uid, gid,
                     username, groupname);
    else
        loggedfs_log(aPath, "chown", res, ChOwnRow, aPath, uid, gid);

    delete[] aPath;

    if (res == -1)
        return -errno;

    return 0;
}

/*******************************************************************************
 *
 */
static int loggedFS_truncate(const char* path, off_t size)
{
    int res;

    const char* aPath = getAbsolutePath(path);
    path = getRelativePath(path);
    res = truncate(path, size);

    delete[] path;

    loggedfs_log(aPath, "truncate", res, TruncateRow, aPath, size);

    delete[] aPath;

    if (res == -1)
        return -errno;

    return 0;
}

#if (FUSE_USE_VERSION==25)

/*******************************************************************************
 *
 */
static int loggedFS_utime(const char *path, struct utimbuf *buf)
{
    int res;
    const char* aPath = getAbsolutePath(path);
    path = getRelativePath(path);
    res = utime(path, buf);

    delete[] path;

    loggedfs_log(aPath, "utime", res, UTimeRow, aPath);

    delete[] aPath;

    if (res == -1)
        return -errno;

    return 0;
}

#else

/*******************************************************************************
 *
 */
static int loggedFS_utimens(const char* path, const struct timespec ts[2])
{
    int res;
    struct timeval tv[2];

    const char *aPath = getAbsolutePath(path);
    path = getRelativePath(path);

    tv[0].tv_sec = ts[0].tv_sec;
    tv[0].tv_usec = ts[0].tv_nsec / 1000;
    tv[1].tv_sec = ts[1].tv_sec;
    tv[1].tv_usec = ts[1].tv_nsec / 1000;

    res = utimes(path, tv);

    delete[] path;

    loggedfs_log(aPath, "utimens", res, UTimeNsRow, aPath);

    delete[] aPath;

    if (res == -1)
        return -errno;

    return 0;
}

#endif

/*******************************************************************************
 *
 */
static int loggedFS_open(const char* path, struct fuse_file_info* fi)
{
    int res;
    const char* aPath = getAbsolutePath(path);
    path = getRelativePath(path);

    res = open(path, fi->flags);

    delete[] path;

    // what type of open ? read, write, or read-write ?
    if (fi->flags & O_RDONLY)
    {
        loggedfs_log(aPath, "open-readonly", res, OpenReadOnlyRow, aPath);
    }
    else if (fi->flags & O_WRONLY)
    {
        loggedfs_log(aPath, "open-writeonly", res, OpenWriteOnlyRow, aPath);
    }
    else if (fi->flags & O_RDWR)
    {
        loggedfs_log(aPath, "open-readwrite", res, OpenReadWriteRow, aPath);
    }
    else
        loggedfs_log(aPath, "open", res, OpenRow, aPath);

    delete[] aPath;

    if (res == -1)
        return -errno;

    close(res);

    return 0;
}

/*******************************************************************************
 *
 */
static int loggedFS_read(const char* path, char* buf, size_t size, off_t offset,
                         struct fuse_file_info* fi)
{
    int fd;
    int res;

    const char* aPath = getAbsolutePath(path);
    path = getRelativePath(path);
    (void) fi;

    fd = open(path, O_RDONLY);

    delete[] path;

    if (fd == -1)
    {
        res = -errno;

        loggedfs_log(aPath, "read-request", -1, ReadRequestRow, aPath, size,
                     offset);

        delete[] aPath;

        return res;
    }
    else
    {
        loggedfs_log(aPath, "read-request", 0, ReadRequestRow, aPath, size,
                     offset);
    }

    res = pread(fd, buf, size, offset);

    if (res == -1)
        res = -errno;
    else
        loggedfs_log(aPath, "read", 0, ReadRow, aPath, res, offset);

    delete[] aPath;

    close(fd);

    return res;
}

/*******************************************************************************
 *
 */
static int loggedFS_write(const char* path, const char* buf, size_t size,
                          off_t offset, struct fuse_file_info* fi)
{
    int fd;
    int res;
    const char* aPath = getAbsolutePath(path);
    path = getRelativePath(path);
    (void) fi;

    fd = open(path, O_WRONLY);

    delete[] path;

    if (fd == -1)
    {
        res = -errno;

        loggedfs_log(aPath, "write-request", -1, WriteRequestRow, aPath, size,
                     offset);

        delete[] aPath;

        return res;
    }
    else
    {
        loggedfs_log(aPath, "write-request", 0, WriteRequestRow, aPath, size,
                     offset);
    }

    res = pwrite(fd, buf, size, offset);

    if (res == -1)
        res = -errno;
    else
        loggedfs_log(aPath, "write", 0, WriteRow, aPath, res, offset);

    delete[] aPath;

    close(fd);

    return res;
}

/*******************************************************************************
 *
 */
static int loggedFS_statfs(const char* path, struct statvfs* stbuf)
{
    int res;
    const char* aPath = getAbsolutePath(path);
    path = getRelativePath(path);

    res = statvfs(path, stbuf);

    delete[] path;

    loggedfs_log(aPath, "statfs", res, StatFsRow, aPath);

    delete[] aPath;

    if (res == -1)
        return -errno;

    return 0;
}

/*******************************************************************************
 * Just a stub. This method is optional and can safely be left unimplemented
 */
static int loggedFS_release(const char* path, struct fuse_file_info* fi)
{
    (void) path;
    (void) fi;

    return 0;
}

/*******************************************************************************
 * Just a stub. This method is optional and can safely be left unimplemented
 */
static int loggedFS_fsync(const char* path, int isdatasync,
                          struct fuse_file_info* fi)
{
    (void) path;
    (void) isdatasync;
    (void) fi;

    return 0;
}

#ifdef HAVE_SETXATTR

/* xattr operations are optional and can safely be left unimplemented */

/*******************************************************************************
 *
 */
static int loggedFS_setxattr(const char* path, const char* name, const
                             char* value, size_t size, int flags)
{
    int res = lsetxattr(path, name, value, size, flags);

    if (res == -1)
        return -errno;

    return 0;
}

/*******************************************************************************
 *
 */
static int loggedFS_getxattr(const char* path, const char* name, char* value,
                             size_t size)
{
    int res = lgetxattr(path, name, value, size);

    if (res == -1)
        return -errno;

    return res;
}

/*******************************************************************************
 *
 */
static int loggedFS_listxattr(const char* path, char* list, size_t size)
{
    int res = llistxattr(path, list, size);

    if (res == -1)
        return -errno;

    return res;
}

/*******************************************************************************
 *
 */
static int loggedFS_removexattr(const char* path, const char* name)
{
    int res = lremovexattr(path, name);

    if (res == -1)
        return -errno;

    return 0;
}
#endif /* HAVE_SETXATTR */

/*******************************************************************************
 *
 */
static void usage(char* name)
{
    fprintf(stderr, "Usage:\n");

    fprintf(stderr,
            "%s [-h] | [-l log-file] [-c config-file] [-f] [-p] [-e] "
            "/directory-mountpoint\n",
            name);

    fprintf(stderr, "Type 'man loggedfs' for more details\n");

    return;
}

/*******************************************************************************
 *
 */
static bool processArgs(int argc, char* argv[], LoggedFS_Args* out)
{
    // set defaults
    out->isDaemon = true;

    out->fuseArgc = 0;
    out->configFilename = NULL;

    // pass executable name through
    out->fuseArgv[0] = argv[0];
    ++out->fuseArgc;

    // leave a space for mount point, as FUSE expects the mount point before
    // any flags
    out->fuseArgv[1] = NULL;
    ++out->fuseArgc;
    opterr = 0;

    int res;

    bool got_p = false;

    // We need the "nonempty" option to mount the directory in recent FUSE's
    // because it's non empty and contains the files that will be logged.
    //
    // We need "use_ino" so the files will use their original inode numbers,
    // instead of all getting 0xFFFFFFFF . For example, this is required for
    // logging the ~/.kde/share/config directory, in which hard links for lock
    // files are verified by their inode equivalency.

#define COMMON_OPTS "nonempty,use_ino"

    string options = "";

    char* str1, * token, * option, * value;
    char* saveptr1, * saveptr2;
    unsigned char count = 0;

    FILE* pFileLog;

    while ((res = getopt(argc, argv, "hpfec:l:o:")) != -1)
    {
        switch (res)
        {
        case 'h':
            usage(argv[0]);

            return false;

        case 'f':
            out->isDaemon = false;
            // this option was added in fuse 2.x
            PUSHARG("-f");
            rLog(errChannel, "LoggedFS not running as a daemon");

            break;

        case 'p':
            PUSHARG("-o");
            PUSHARG("allow_other,default_permissions," COMMON_OPTS);
            got_p = true;
            rLog(errChannel, "LoggedFS running as a public filesystem");

            break;

        case 'e':
            PUSHARG("-o");
            PUSHARG("nonempty");
            rLog(errChannel, "Using existing directory");

            break;

        case 'c':
            out->configFilename = optarg;
            rLog(errChannel, "Configuration file: %s", optarg);

            break;

        case 'l':
            pFileLog = fopen(optarg, "a+");
            fprintf(pFileLog, "%s\n", Header);
            fclose(pFileLog);

            fileLog = open(optarg, O_WRONLY | O_CREAT | O_APPEND);
            fileLogNode = new StdioNode(fileLog, 16);
            fileLogNode->subscribeTo(RLOG_CHANNEL("out"));
            rLog(errChannel, "LoggedFS log file: %s", optarg);

            break;

        case 'o':
            // Option added for when used in "/etc/fstab".
            // In that case the default FUSE options COMMON_OPTS are not enabled
            // if they are not explicitly specified - i.e. up to the caller 
            // to add those options.
            // e.g.:
            // loggedfs some_absolute_path fuse 
            // c=xml_configuration_file[,l=csv_file],nonempty,use_ino,
            // allow_other,default_permissions 0 0

            rLog(errChannel, "Using mount options: %s", optarg);

            for (str1 = optarg;; str1 = NULL)
            {
                token = strtok_r(str1, ",", &saveptr1);

                if (token == NULL)
                    break;

                option = strtok_r(token, "=", &saveptr2);

                if (strcmp(option, "c") == 0)
                {
                    value = strtok_r(NULL, "=", &saveptr2);

                    if (value != NULL)
                    {
                        out->configFilename = value;
                        rLog(errChannel, "Configuration file: %s", value);
                    }
                }
                else if (strcmp(option, "l") == 0)
                {
                    value = strtok_r(NULL, "=", &saveptr2);

                    if (value != NULL)
                    {
                        pFileLog = fopen(value, "a+");
                        fprintf(pFileLog, "%s\n", Header);
                        fclose(pFileLog);

                        fileLog = open(value, O_WRONLY | O_CREAT | O_APPEND);
                        fileLogNode = new StdioNode(fileLog, 16);
                        fileLogNode->subscribeTo(RLOG_CHANNEL("out"));
                        rLog(errChannel, "LoggedFS log file: %s", value);
                    }
                }
                else
                {
                    if (count++ > 0)
                        options.append(",");
                    
                    options.append(token);
                }
            }

            PUSHARG("-o");
            PUSHARG(strdup(options.data()));
            got_p = true;

            rLog(errChannel, "Setting FUSE options: %s",
                 options.data());

            break;

        default:
            break;
        }
    }

    if (!got_p)
    {
        PUSHARG("-o");
        PUSHARG(COMMON_OPTS);
    }
#undef COMMON_OPTS

    if (optind + 1 <= argc)
    {
        out->mountPoint = argv[optind++];
        out->fuseArgv[1] = out->mountPoint;
    }
    else
    {
        fprintf(stderr, "Missing mountpoint\n");
        usage(argv[0]);

        return false;
    }

    // If there are still extra unparsed arguments, pass them onto FUSE..
    if (optind < argc)
    {
        rAssert(out->fuseArgc < MaxFuseArgs);

        while (optind < argc)
        {
            rAssert(out->fuseArgc < MaxFuseArgs);
            out->fuseArgv[out->fuseArgc++] = argv[optind];
            ++optind;
        }
    }

    if (!isAbsolutePath(out->mountPoint))
    {
        fprintf(stderr, "You must use absolute paths "
                "(beginning with '/') for %s\n", out->mountPoint);

        return false;
    }

    if (fileLog == 0)
    {
        printf("%s\n", Header);

        // Need to force the flush in case of redirection to 
        // force it to be the first line because of the RLog subscriber
        fflush(stdout);
    }

    return true;
}

/*******************************************************************************
 *
 */
int main(int argc, char* argv[])
{
    RLogInit(argc, argv);

    StdioNode* stdOut = new StdioNode(STDOUT_FILENO, 17);
    stdOut->subscribeTo(RLOG_CHANNEL("out"));

    StdioNode* stdErr = new StdioNode(STDERR_FILENO, 17);
    stdErr->subscribeTo(RLOG_CHANNEL("err"));

    // Display the args passed to LoggedFS
    string loggedfsArgsStr = "";

    for (int i = 0; i < argc; i++)
    {
        const char* arg = argv[i];

        loggedfsArgsStr.append(arg);
        loggedfsArgsStr.append(" ");
    }

    rLog(errChannel, "LoggedFS args (%d): %s", argc, loggedfsArgsStr.data());

    SyslogNode* logNode = NULL;

    umask(0);
    fuse_operations loggedFS_oper;
    
    // in case this code is compiled against a newer FUSE library and new
    // members have been added to fuse_operations, make sure they get set to
    // 0..
    memset(&loggedFS_oper, 0, sizeof (fuse_operations));
    
    loggedFS_oper.init = loggedFS_init;
    loggedFS_oper.getattr = loggedFS_getattr;
    loggedFS_oper.access = loggedFS_access;
    loggedFS_oper.readlink = loggedFS_readlink;
    loggedFS_oper.readdir = loggedFS_readdir;
    loggedFS_oper.mknod = loggedFS_mknod;
    loggedFS_oper.mkdir = loggedFS_mkdir;
    loggedFS_oper.symlink = loggedFS_symlink;
    loggedFS_oper.unlink = loggedFS_unlink;
    loggedFS_oper.rmdir = loggedFS_rmdir;
    loggedFS_oper.rename = loggedFS_rename;
    loggedFS_oper.link = loggedFS_link;
    loggedFS_oper.chmod = loggedFS_chmod;
    loggedFS_oper.chown = loggedFS_chown;
    loggedFS_oper.truncate = loggedFS_truncate;

#if (FUSE_USE_VERSION==25)
    loggedFS_oper.utime = loggedFS_utime;
#else
    loggedFS_oper.utimens = loggedFS_utimens;
#endif

    loggedFS_oper.open = loggedFS_open;
    loggedFS_oper.read = loggedFS_read;
    loggedFS_oper.write = loggedFS_write;
    loggedFS_oper.statfs = loggedFS_statfs;
    loggedFS_oper.release = loggedFS_release;
    loggedFS_oper.fsync = loggedFS_fsync;

#ifdef HAVE_SETXATTR
    loggedFS_oper.setxattr = loggedFS_setxattr;
    loggedFS_oper.getxattr = loggedFS_getxattr;
    loggedFS_oper.listxattr = loggedFS_listxattr;
    loggedFS_oper.removexattr = loggedFS_removexattr;
#endif

    for (int i = 0; i < MaxFuseArgs; ++i)
        loggedfsArgs->fuseArgv[i] = NULL; // libfuse expects null args..

    if (processArgs(argc, argv, loggedfsArgs))
    {
        if (fileLog != 0)
        {
            delete stdOut;
            stdOut = NULL;
        }

        if (loggedfsArgs->isDaemon)
        {
            logNode = new SyslogNode("loggedfs");

            if (fileLog != 0)
            {
                logNode->subscribeTo(RLOG_CHANNEL("err"));
            }
            else
            {
                logNode->subscribeTo(RLOG_CHANNEL(""));

                // disable stdout reporting...
                delete stdOut;
                stdOut = NULL;
            }

            // disable stderr reporting...
            delete stdErr;
            stdErr = NULL;
        }

        rLog(errChannel, "LoggedFS starting at %s.", loggedfsArgs->mountPoint);

        if (loggedfsArgs->configFilename != NULL)
        {
            if (strcmp(loggedfsArgs->configFilename, "-") == 0)
            {
                rLog(errChannel, "Using stdin configuration");
                char* input = new char[2048]; // 2kB MAX input for configuration
                memset(input, 0, 2048);
                char* ptr = input;

                int size = 0;

                do
                {
                    size = fread(ptr, 1, 1, stdin);
                    ptr++;
                }
                while (!feof(stdin) && size > 0);

                config.loadFromXmlBuffer(input);
                delete[] input;
            }
            else
            {
                rLog(errChannel, "Using configuration file %s.",
                     loggedfsArgs->configFilename);

                config.loadFromXmlFile(loggedfsArgs->configFilename);
            }
        }

        rLog(errChannel, "chdir to %s", loggedfsArgs->mountPoint);

        chdir(loggedfsArgs->mountPoint);
        savefd = open(".", 0);

        // Display the args passed to FUSE
        string fuseArgs = "";
        
        for (int i = 0; i < loggedfsArgs->fuseArgc; i++)
        {
            const char* arg = loggedfsArgs->fuseArgv[i];

            fuseArgs.append(arg);
            fuseArgs.append(" ");
        }

        rLog(errChannel, "FUSE args (%d): %s", loggedfsArgs->fuseArgc, fuseArgs.data());

#if (FUSE_USE_VERSION==25)
        fuse_main(loggedfsArgs->fuseArgc,
                  const_cast<char**> (loggedfsArgs->fuseArgv), &loggedFS_oper);
#else
        fuse_main(loggedfsArgs->fuseArgc,
                  const_cast<char**> (loggedfsArgs->fuseArgv), &loggedFS_oper,
                  NULL);
#endif

        delete stdErr;
        stdErr = NULL;

        if (fileLog != 0)
        {
            delete fileLogNode;
            fileLogNode = NULL;
            close(fileLog);
        }
        else
        {
            delete stdOut;
            stdOut = NULL;
        }

        if (loggedfsArgs->isDaemon)
        {
            delete logNode;
            logNode = NULL;
        }

        rLog(errChannel, "LoggedFS closing.");

        delete loggedfsArgs;
    }
}
