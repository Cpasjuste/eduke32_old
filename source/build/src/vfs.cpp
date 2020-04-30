
#ifdef _WIN32
// for FILENAME_CASE_CHECK
# define NEED_SHELLAPI_H
# include "windows_inc.h"
#endif

#include "baselayer.h"
#include "cache1d.h"
#include "compat.h"
#include "klzw.h"
#include "lz4.h"
#include "osd.h"
#include "pragmas.h"
#include "vfs.h"
#include "cache1d.h"

#ifdef __cplusplus
extern "C" {
#endif
char toupperlookup[256] =
{
    0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
    0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,
    0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,0x29,0x2a,0x2b,0x2c,0x2d,0x2e,0x2f,
    0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x3b,0x3c,0x3d,0x3e,0x3f,
    0x40,0x41,0x42,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4a,0x4b,0x4c,0x4d,0x4e,0x4f,
    0x50,0x51,0x52,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x5b,0x5c,0x5d,0x5e,0x5f,
    0x60,0x41,0x42,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4a,0x4b,0x4c,0x4d,0x4e,0x4f,
    0x50,0x51,0x52,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x7b,0x7c,0x7d,0x7e,0x7f,
    0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8a,0x8b,0x8c,0x8d,0x8e,0x8f,
    0x90,0x91,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0x9b,0x9c,0x9d,0x9e,0x9f,
    0xa0,0xa1,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xab,0xac,0xad,0xae,0xaf,
    0xb0,0xb1,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xbb,0xbc,0xbd,0xbe,0xbf,
    0xc0,0xc1,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xcb,0xcc,0xcd,0xce,0xcf,
    0xd0,0xd1,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xdb,0xdc,0xdd,0xde,0xdf,
    0xe0,0xe1,0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xeb,0xec,0xed,0xee,0xef,
    0xf0,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa,0xfb,0xfc,0xfd,0xfe,0xff
};
#ifdef __cplusplus
}
#endif
#ifdef WITHKPLIB
#include "kplib.h"

//Insert '|' in front of filename
//Doing this tells kzopen to load the file only if inside a .ZIP file
static intptr_t kzipopen(const char *filnam)
{
    uint32_t i;
    char newst[BMAX_PATH+8];

    newst[0] = '|';
    for (i=0; i < BMAX_PATH+4 && filnam[i]; i++) newst[i+1] = filnam[i];
    newst[i+1] = 0;
    return kzopen(newst);
}

#endif

char *kpzbuf = NULL;
int32_t kpzbufsiz;

int32_t kpzbufloadfil(buildvfs_kfd const handle)
{
    int32_t const leng = kfilelength(handle);
    if (leng > kpzbufsiz)
    {
        kpzbuf = (char *) Xrealloc(kpzbuf, leng+1);
        kpzbufsiz = leng;
        if (!kpzbuf)
            return 0;
    }

    kpzbuf[leng] = 0;  // FIXME: buf[leng] read in kpegrend(), see BUF_LENG_READ
    kread(handle, kpzbuf, leng);

    return leng;
}

int32_t kpzbufload(char const * const filnam)
{
    buildvfs_kfd const handle = kopen4load(filnam, 0);
    if (handle == buildvfs_kfd_invalid)
        return 0;

    int32_t const leng = kpzbufloadfil(handle);

    kclose(handle);

    return leng;
}

#ifdef USE_PHYSFS

int32_t numgroupfiles;

void uninitgroupfile(void)
{
    PHYSFS_deinit();
}

#include <sys/stat.h>

int32_t klseek(buildvfs_kfd handle, int32_t offset, int32_t whence)
{
    // TODO: replace klseek calls with _{abs,cur,end} versions

    if (whence == SEEK_CUR)
        offset += PHYSFS_tell(handle);
    else if (whence == SEEK_END)
        offset += PHYSFS_fileLength(handle);

    PHYSFS_seek(handle, offset);
    return PHYSFS_tell(handle);
}

#endif

#include <errno.h>

typedef struct _searchpath
{
    struct _searchpath *next;
    char *path;
    size_t pathlen;		// to save repeated calls to strlen()
    int32_t user;
} searchpath_t;
static searchpath_t *searchpathhead = NULL;
static size_t maxsearchpathlen = 0;
int32_t pathsearchmode = 0;

#ifndef USE_PHYSFS

char *listsearchpath(int32_t initp)
{
    static searchpath_t *sp;

    if (initp)
        sp = searchpathhead;
    else if (sp != NULL)
        sp = sp->next;

    return sp ? sp->path : NULL;
}

int32_t addsearchpath_user(const char *p, int32_t user)
{
    struct Bstat st;
    char *s;
    searchpath_t *srch;
    char *path = Xstrdup(p);

    if (path[Bstrlen(path)-1] == '\\')
        path[Bstrlen(path)-1] = 0; // hack for stat() returning ENOENT on paths ending in a backslash

    if (Bstat(path, &st) < 0)
    {
        Xfree(path);
        if (errno == ENOENT) return -2;
        return -1;
    }
    if (!(st.st_mode & BS_IFDIR))
    {
        Xfree(path);
        return -1;
    }

    srch = (searchpath_t *)Xmalloc(sizeof(searchpath_t));

    srch->next    = searchpathhead;
    srch->pathlen = Bstrlen(path)+1;
    srch->path    = (char *)Xmalloc(srch->pathlen + 1);

    Bstrcpy(srch->path, path);
    for (s=srch->path; *s; s++) { }
    s--;

    if (s<srch->path || toupperlookup[*s] != '/')
        Bstrcat(srch->path, "/");

    searchpathhead = srch;
    if (srch->pathlen > maxsearchpathlen)
        maxsearchpathlen = srch->pathlen;

    Bcorrectfilename(srch->path,0);

    srch->user = user;

    initprintf("Using %s for game data\n", srch->path);

    Xfree(path);
    return 0;
}

int32_t removesearchpath(const char *p)
{
    searchpath_t *srch;
    char *s;
    char *path = (char *)Xmalloc(Bstrlen(p) + 2);

    Bstrcpy(path, p);

    if (path[Bstrlen(path)-1] == '\\')
        path[Bstrlen(path)-1] = 0;

    for (s=path; *s; s++) { }
    s--;

    if (s<path || toupperlookup[*s] != '/')
        Bstrcat(path, "/");

    Bcorrectfilename(path,0);

    for (srch = searchpathhead; srch; srch = srch->next)
    {
        if (!Bstrncmp(path, srch->path, srch->pathlen))
        {
//            initprintf("Removing %s from path stack\n", path);

            if (srch == searchpathhead)
                searchpathhead = srch->next;
            else
            {
                searchpath_t *sp;

                for (sp = searchpathhead; sp; sp = sp->next)
                {
                    if (sp->next == srch)
                    {
//                        initprintf("matched %s\n", srch->path);
                        sp->next = srch->next;
                        break;
                    }
                }
            }

            Xfree(srch->path);
            Xfree(srch);
            break;
        }
    }

    Xfree(path);
    return 0;
}

void removesearchpaths_withuser(int32_t usermask)
{
    searchpath_t *next;

    for (searchpath_t *srch = searchpathhead; srch; srch = next)
    {
        next = srch->next;

        if (srch->user & usermask)
        {

            if (srch == searchpathhead)
                searchpathhead = srch->next;
            else
            {
                searchpath_t *sp;

                for (sp = searchpathhead; sp; sp = sp->next)
                {
                    if (sp->next == srch)
                    {
                        sp->next = srch->next;
                        break;
                    }
                }
            }

            Xfree(srch->path);
            Xfree(srch);
        }
    }
}

int32_t findfrompath(const char *fn, char **where)
{
    // pathsearchmode == 0: tests current dir and then the dirs of the path stack
    // pathsearchmode == 1: tests fn without modification, then like for pathsearchmode == 0

    if (pathsearchmode)
    {
        // test unmolested filename first
        if (buildvfs_exists(fn))
        {
            *where = Xstrdup(fn);
            return 0;
        }
#ifndef _WIN32
        else
        {
            char *tfn = Bstrtolower(Xstrdup(fn));

            if (buildvfs_exists(tfn))
            {
                *where = tfn;
                return 0;
            }

            Bstrupr(tfn);

            if (buildvfs_exists(tfn))
            {
                *where = tfn;
                return 0;
            }

            Xfree(tfn);
        }
#endif
    }

    char const *cpfn;

    for (cpfn = fn; toupperlookup[*cpfn] == '/'; cpfn++) { }
    char *ffn = Xstrdup(cpfn);

    Bcorrectfilename(ffn,0);	// compress relative paths

    int32_t allocsiz = max<int>(maxsearchpathlen, 2);	// "./" (aka. curdir)
    allocsiz += strlen(ffn);
    allocsiz += 1;	// a nul

    char *pfn = (char *)Xmalloc(allocsiz);

    strcpy(pfn, "./");
    strcat(pfn, ffn);
    if (buildvfs_exists(pfn))
    {
        *where = pfn;
        Xfree(ffn);
        return 0;
    }

    for (searchpath_t *sp = searchpathhead; sp; sp = sp->next)
    {
        char *tfn = Xstrdup(ffn);

        strcpy(pfn, sp->path);
        strcat(pfn, ffn);
        //initprintf("Trying %s\n", pfn);
        if (buildvfs_exists(pfn))
        {
            *where = pfn;
            Xfree(ffn);
            Xfree(tfn);
            return 0;
        }

#ifndef _WIN32
        //Check with all lowercase
        strcpy(pfn, sp->path);
        Bstrtolower(tfn);
        strcat(pfn, tfn);
        if (buildvfs_exists(pfn))
        {
            *where = pfn;
            Xfree(ffn);
            Xfree(tfn);
            return 0;
        }

        //Check again with uppercase
        strcpy(pfn, sp->path);
        Bstrupr(tfn);
        strcat(pfn, tfn);
        if (buildvfs_exists(pfn))
        {
            *where = pfn;
            Xfree(ffn);
            Xfree(tfn);
            return 0;
        }
#endif
        Xfree(tfn);
    }

    Xfree(pfn); Xfree(ffn);
    return -1;
}

#if defined(_WIN32) && defined(DEBUGGINGAIDS)
# define FILENAME_CASE_CHECK
#endif

static buildvfs_kfd openfrompath_internal(const char *fn, char **where, int32_t flags, int32_t mode)
{
    if (findfrompath(fn, where) < 0)
        return -1;

    return Bopen(*where, flags, mode);
}

buildvfs_kfd openfrompath(const char *fn, int32_t flags, int32_t mode)
{
    char *pfn = NULL;

    buildvfs_kfd h = openfrompath_internal(fn, &pfn, flags, mode);

    Xfree(pfn);

    return h;
}

buildvfs_FILE fopenfrompath(const char *fn, const char *mode)
{
    int32_t fh;
    buildvfs_FILE h;
    int32_t bmode = 0, smode = 0;
    const char *c;

    for (c=mode; c[0];)
    {
        if (c[0] == 'r' && c[1] == '+') { bmode = BO_RDWR; smode = BS_IREAD|BS_IWRITE; c+=2; }
        else if (c[0] == 'r')                { bmode = BO_RDONLY; smode = BS_IREAD; c+=1; }
        else if (c[0] == 'w' && c[1] == '+') { bmode = BO_RDWR|BO_CREAT|BO_TRUNC; smode = BS_IREAD|BS_IWRITE; c+=2; }
        else if (c[0] == 'w')                { bmode = BO_WRONLY|BO_CREAT|BO_TRUNC; smode = BS_IREAD|BS_IWRITE; c+=2; }
        else if (c[0] == 'a' && c[1] == '+') { bmode = BO_RDWR|BO_CREAT; smode=BS_IREAD|BS_IWRITE; c+=2; }
        else if (c[0] == 'a')                { bmode = BO_WRONLY|BO_CREAT; smode=BS_IREAD|BS_IWRITE; c+=1; }
        else if (c[0] == 'b')                { bmode |= BO_BINARY; c+=1; }
        else if (c[1] == 't')                { bmode |= BO_TEXT; c+=1; }
        else c++;
    }
    fh = openfrompath(fn,bmode,smode);
    if (fh < 0) return NULL;

    h = fdopen(fh,mode);
    if (!h) close(fh);

    return h;
}

#define MAXGROUPFILES 8     // Warning: Fix groupfil if this is changed
#define MAXOPENFILES 64     // Warning: Fix filehan if this is changed

enum {
    GRP_RESERVED_ID_START = 254,

    GRP_ZIP = GRP_RESERVED_ID_START,
    GRP_FILESYSTEM = GRP_RESERVED_ID_START + 1,
};

EDUKE32_STATIC_ASSERT(MAXGROUPFILES <= GRP_RESERVED_ID_START);

int32_t numgroupfiles = 0;
static int32_t gnumfiles[MAXGROUPFILES];
static intptr_t groupfil[MAXGROUPFILES] = {-1,-1,-1,-1,-1,-1,-1,-1};
static int32_t groupfilpos[MAXGROUPFILES];
static uint8_t groupfilgrp[MAXGROUPFILES];
static char *gfilelist[MAXGROUPFILES];
static char *groupname[MAXGROUPFILES];
static int32_t *gfileoffs[MAXGROUPFILES];

static uint8_t filegrp[MAXOPENFILES];
static int32_t filepos[MAXOPENFILES];
static intptr_t filehan[MAXOPENFILES] =
{
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
};

#ifdef WITHKPLIB
static char filenamsav[MAXOPENFILES][260];
static int32_t kzcurhand = -1;

int32_t cache1d_file_fromzip(buildvfs_kfd fil)
{
    return (filegrp[fil] == GRP_ZIP);
}
#endif

static int32_t kopen_internal(const char *filename, char **lastpfn, char searchfirst, char checkcase, char tryzip, int32_t newhandle, uint8_t *arraygrp, intptr_t *arrayhan, int32_t *arraypos);
static int32_t kread_grp(int32_t handle, void *buffer, int32_t leng);
static int32_t klseek_grp(int32_t handle, int32_t offset, int32_t whence);
static void kclose_grp(int32_t handle);

int initgroupfile(const char *filename)
{
    char buf[70];

    // translate all backslashes (0x5c) to forward slashes (0x2f)
    toupperlookup[0x5c] = 0x2f;

    if (filename == NULL)
        return -1;

    // Technically you should be able to load more zips even if your GRPs are maxed out,
    // but this system is already enough of a disaster.
    if (numgroupfiles >= MAXGROUPFILES)
        return -1;

    char *zfn = NULL;

    if (kopen_internal(filename, &zfn, 0, 0, 0, numgroupfiles, groupfilgrp, groupfil, groupfilpos) < 0)
        return -1;

#ifdef WITHKPLIB
    // check if ZIP
    if (zfn)
    {
        kread_grp(numgroupfiles, buf, 4);
        if (buf[0] == 0x50 && buf[1] == 0x4B && buf[2] == 0x03 && buf[3] == 0x04)
        {
            kclose_grp(numgroupfiles);

            kzaddstack(zfn);
            Xfree(zfn);
            return MAXGROUPFILES;
        }
        klseek_grp(numgroupfiles,0,BSEEK_SET);

        Xfree(zfn);
    }
#else
    Xfree(zfn);
#endif

    // check if GRP
    kread_grp(numgroupfiles,buf,16);
    if (!Bmemcmp(buf, "KenSilverman", 12))
    {
        gnumfiles[numgroupfiles] = B_LITTLE32(*((int32_t *)&buf[12]));

        gfilelist[numgroupfiles] = (char *)Xmalloc(gnumfiles[numgroupfiles]<<4);
        gfileoffs[numgroupfiles] = (int32_t *)Xmalloc((gnumfiles[numgroupfiles]+1)<<2);

        kread_grp(numgroupfiles,gfilelist[numgroupfiles],gnumfiles[numgroupfiles]<<4);

        int32_t j = (gnumfiles[numgroupfiles]+1)<<4;
        for (bssize_t i=0; i<gnumfiles[numgroupfiles]; i++)
        {
            int32_t const k = B_LITTLE32(*((int32_t *)&gfilelist[numgroupfiles][(i<<4)+12]));
            gfilelist[numgroupfiles][(i<<4)+12] = 0;
            gfileoffs[numgroupfiles][i] = j;
            j += k;
        }
        gfileoffs[numgroupfiles][gnumfiles[numgroupfiles]] = j;
        groupname[numgroupfiles] = Xstrdup(filename);
        return numgroupfiles++;
    }
    klseek_grp(numgroupfiles, 0, BSEEK_SET);

    // check if SSI
    // this performs several checks because there is no "SSI" magic
    int32_t version;
    kread_grp(numgroupfiles, &version, 4);
    version = B_LITTLE32(version);
    while (version == 1 || version == 2) // if
    {
        char zerobuf[70];
        Bmemset(zerobuf, 0, 70);

        int32_t numfiles;
        kread_grp(numgroupfiles, &numfiles, 4);
        numfiles = B_LITTLE32(numfiles);

        uint8_t temp, temp2;

        // get the string length
        kread_grp(numgroupfiles, &temp, 1);
        if (temp > 31) // 32 bytes allocated for the string
            break;
        // seek to the end of the string
        klseek_grp(numgroupfiles, temp, BSEEK_CUR);
        // verify everything remaining is a null terminator
        temp = 32 - temp;
        kread_grp(numgroupfiles, buf, temp);
        if (Bmemcmp(buf, zerobuf, temp))
            break;

        if (version == 2)
        {
            // get the string length
            kread_grp(numgroupfiles, &temp, 1);
            if (temp > 11) // 12 bytes allocated for the string
                break;
            // seek to the end of the string
            klseek_grp(numgroupfiles, temp, BSEEK_CUR);
            // verify everything remaining is a null terminator
            temp = 12 - temp;
            kread_grp(numgroupfiles, buf, temp);
            if (Bmemcmp(buf, zerobuf, temp))
                break;
        }

        temp2 = 0;
        for (int i=0;i<3;i++)
        {
            // get the string length
            kread_grp(numgroupfiles, &temp, 1);
            if (temp > 70) // 70 bytes allocated for the string
            {
                temp2 = 1;
                break;
            }
            // seek to the end of the string
            klseek_grp(numgroupfiles, temp, BSEEK_CUR);
            // verify everything remaining is a null terminator
            temp = 70 - temp;
            if (temp == 0)
                continue;
            kread_grp(numgroupfiles, buf, temp);
            temp2 |= !!Bmemcmp(buf, zerobuf, temp);
        }
        if (temp2)
            break;

        // Passed all the tests: read data.

        gnumfiles[numgroupfiles] = numfiles;

        gfilelist[numgroupfiles] = (char *)Xmalloc(gnumfiles[numgroupfiles]<<4);
        gfileoffs[numgroupfiles] = (int32_t *)Xmalloc((gnumfiles[numgroupfiles]+1)<<2);

        int32_t j = (version == 2 ? 267 : 254) + (numfiles * 121), k;
        for (bssize_t i = 0; i < numfiles; i++)
        {
            // get the string length
            kread_grp(numgroupfiles, &temp, 1);
            if (temp > 12)
                temp = 12;
            // read the file name
            kread_grp(numgroupfiles, &gfilelist[numgroupfiles][i<<4], temp);
            gfilelist[numgroupfiles][(i<<4)+temp] = 0;

            // skip to the end of the 12 bytes
            klseek_grp(numgroupfiles, 12-temp, BSEEK_CUR);

            // get the file size
            kread_grp(numgroupfiles, &k, 4);
            k = B_LITTLE32(k);

            // record the offset of the file in the SSI
            gfileoffs[numgroupfiles][i] = j;
            j += k;

            // skip unknown data
            klseek_grp(numgroupfiles, 104, BSEEK_CUR);
        }
        gfileoffs[numgroupfiles][gnumfiles[numgroupfiles]] = j;
        groupname[numgroupfiles] = Xstrdup(filename);
        return numgroupfiles++;
    }

    kclose_grp(numgroupfiles);
    return -1;
}

void uninitgroupfile(void)
{
    int32_t i;

    for (i=numgroupfiles-1; i>=0; i--)
        if (groupfil[i] != -1)
        {
            DO_FREE_AND_NULL(gfilelist[i]);
            DO_FREE_AND_NULL(gfileoffs[i]);
            DO_FREE_AND_NULL(groupname[i]);

            Bclose(groupfil[i]);
            groupfil[i] = -1;
        }
    numgroupfiles = 0;

    // JBF 20040111: "close" any files open in groups
    for (i=0; i<MAXOPENFILES; i++)
    {
        if (filegrp[i] < GRP_RESERVED_ID_START)   // JBF 20040130: not external or ZIPped
            filehan[i] = -1;
    }
}

#ifdef FILENAME_CASE_CHECK
// See
// http://stackoverflow.com/questions/74451/getting-actual-file-name-with-proper-casing-on-windows
// for relevant discussion.

static char fnbuf[BMAX_PATH];
int fnofs;

int32_t (*check_filename_casing_fn)(void) = NULL;

// -1: failure, 0: match, 1: mismatch
static int32_t check_filename_mismatch(const char * const filename, int ofs)
{
    if (!GetShortPathNameA(filename, fnbuf, BMAX_PATH)) return -1;
    if (!GetLongPathNameA(fnbuf, fnbuf, BMAX_PATH)) return -1;

    fnofs = ofs;

    int len = Bstrlen(fnbuf+ofs);

    char const * const fn = filename+ofs;

    if (!Bstrncmp(fnbuf+ofs, fn, len))
        return 0;

    char * const tfn = Bstrtolower(Xstrdup(fn));

    if (!Bstrncmp(fnbuf+ofs, tfn, len))
    {
        Xfree(tfn);
        return 0;
    }

    Bstrupr(tfn);

    if (!Bstrncmp(fnbuf+ofs, tfn, len))
    {
        Xfree(tfn);
        return 0;
    }

    Xfree(tfn);

    return 1;
}
#endif

static int32_t kopen_internal(const char *filename, char **lastpfn, char searchfirst, char checkcase, char tryzip, int32_t newhandle, uint8_t *arraygrp, intptr_t *arrayhan, int32_t *arraypos)
{
    buildvfs_kfd fil;
    if (searchfirst == 0 && (fil = openfrompath_internal(filename, lastpfn, BO_BINARY|BO_RDONLY, BS_IREAD)) >= 0)
    {
#ifdef FILENAME_CASE_CHECK
        if (checkcase && check_filename_casing_fn && check_filename_casing_fn())
        {
            int32_t status;
            char *cp, *lastslash;

            // convert all slashes to backslashes because SHGetFileInfo()
            // complains else!
            lastslash = *lastpfn;
            for (cp=*lastpfn; *cp; cp++)
                if (*cp=='/')
                {
                    *cp = '\\';
                    lastslash = cp;
                }
            if (lastslash != *lastpfn)
                lastslash++;

            status = check_filename_mismatch(*lastpfn, lastslash-*lastpfn);

            if (status == -1)
            {
//                initprintf("SHGetFileInfo failed with error code %lu\n", GetLastError());
            }
            else if (status == 1)
            {
                initprintf("warning: case mismatch: passed \"%s\", real \"%s\"\n",
                           lastslash, fnbuf+fnofs);
            }
        }
#else
        UNREFERENCED_PARAMETER(checkcase);
#endif
        arraygrp[newhandle] = GRP_FILESYSTEM;
        arrayhan[newhandle] = fil;
        arraypos[newhandle] = 0;
        return newhandle;
    }

    for (; toupperlookup[*filename] == '/'; filename++) { }

#ifdef WITHKPLIB
    if (tryzip)
    {
        intptr_t i;
        if ((kzcurhand != newhandle) && (kztell() >= 0))
        {
            if (kzcurhand >= 0) arraypos[kzcurhand] = kztell();
            kzclose();
            kzcurhand = -1;
        }
        if (searchfirst != 1 && (i = kzipopen(filename)) != 0)
        {
            kzcurhand = newhandle;
            arraygrp[newhandle] = GRP_ZIP;
            arrayhan[newhandle] = i;
            arraypos[newhandle] = 0;
            strcpy(filenamsav[newhandle],filename);
            return newhandle;
        }
    }
#else
    UNREFERENCED_PARAMETER(tryzip);
#endif

    for (bssize_t k = searchfirst != 1 ? numgroupfiles-1 : 0; k >= 0; --k)
    {
        if (groupfil[k] < 0)
            continue;

        for (bssize_t i = gnumfiles[k]-1; i >= 0; --i)
        {
            char const * const gfileptr = (char *)&gfilelist[k][i<<4];

            unsigned int j;
            for (j = 0; j < 13; ++j)
            {
                if (!filename[j]) break;
                if (toupperlookup[filename[j]] != toupperlookup[gfileptr[j]])
                    goto gnumfiles_continue;
            }
            if (j<13 && gfileptr[j]) continue;   // JBF: because e1l1.map might exist before e1l1
            if (j==13 && filename[j]) continue;   // JBF: long file name

            arraygrp[newhandle] = k;
            arrayhan[newhandle] = i;
            arraypos[newhandle] = 0;
            return newhandle;

gnumfiles_continue: ;
        }
    }

    return -1;
}

void krename(int32_t crcval, int32_t filenum, const char *newname)
{
    Bstrncpy((char *)&gfilelist[crcval][filenum<<4], newname, 12);
}

char const * kfileparent(int32_t const handle)
{
    int32_t const groupnum = filegrp[handle];

    if ((unsigned)groupnum >= MAXGROUPFILES || groupfil[groupnum] == -1)
        return NULL;

    return groupname[groupnum];
}

int32_t kopen4load(const char *filename, char searchfirst)
{
    int32_t newhandle = MAXOPENFILES-1;

    if (filename==NULL)
        return -1;

    while (filehan[newhandle] != -1)
    {
        newhandle--;
        if (newhandle < 0)
        {
            initprintf("TOO MANY FILES OPEN IN FILE GROUPING SYSTEM!");
            Bexit(EXIT_SUCCESS);
        }
    }

    char *lastpfn = NULL;

    int32_t h = kopen_internal(filename, &lastpfn, searchfirst, 1, 1, newhandle, filegrp, filehan, filepos);

    Xfree(lastpfn);

    return h;
}

char g_modDir[BMAX_PATH] = "/";

buildvfs_kfd kopen4loadfrommod(const char *fileName, char searchfirst)
{
    buildvfs_kfd kFile = buildvfs_kfd_invalid;

    if (g_modDir[0] != '/' || g_modDir[1] != 0)
    {
        static char staticFileName[BMAX_PATH];
        Bsnprintf(staticFileName, sizeof(staticFileName), "%s/%s", g_modDir, fileName);
        kFile = kopen4load(staticFileName, searchfirst);
    }

    return (kFile == buildvfs_kfd_invalid) ? kopen4load(fileName, searchfirst) : kFile;
}

int32_t kread_internal(int32_t handle, void *buffer, int32_t leng, const uint8_t *arraygrp, const intptr_t *arrayhan, int32_t *arraypos)
{
    int32_t filenum = arrayhan[handle];
    int32_t groupnum = arraygrp[handle];

    if (groupnum == GRP_FILESYSTEM) return Bread(filenum,buffer,leng);
#ifdef WITHKPLIB
    else if (groupnum == GRP_ZIP)
    {
        if (kzcurhand != handle)
        {
            if (kztell() >= 0) { arraypos[kzcurhand] = kztell(); kzclose(); }
            kzcurhand = handle;
            kzipopen(filenamsav[handle]);
            kzseek(arraypos[handle],SEEK_SET);
        }
        return kzread(buffer,leng);
    }
#endif

    if (EDUKE32_PREDICT_FALSE(groupfil[groupnum] == -1))
        return 0;

    int32_t rootgroupnum = groupnum;
    int32_t i = 0;
    while (groupfilgrp[rootgroupnum] != GRP_FILESYSTEM)
    {
        i += gfileoffs[groupfilgrp[rootgroupnum]][groupfil[rootgroupnum]];
        rootgroupnum = groupfilgrp[rootgroupnum];
    }
    if (EDUKE32_PREDICT_TRUE(groupfil[rootgroupnum] != -1))
    {
        i += gfileoffs[groupnum][filenum]+arraypos[handle];
        if (i != groupfilpos[rootgroupnum])
        {
            Blseek(groupfil[rootgroupnum],i,BSEEK_SET);
            groupfilpos[rootgroupnum] = i;
        }
        leng = min(leng,(gfileoffs[groupnum][filenum+1]-gfileoffs[groupnum][filenum])-arraypos[handle]);
        leng = Bread(groupfil[rootgroupnum],buffer,leng);
        arraypos[handle] += leng;
        groupfilpos[rootgroupnum] += leng;
        return leng;
    }

    return 0;
}

int32_t klseek_internal(int32_t handle, int32_t offset, int32_t whence, const uint8_t *arraygrp, intptr_t *arrayhan, int32_t *arraypos)
{
    int32_t const groupnum = arraygrp[handle];

    if (groupnum == GRP_FILESYSTEM) return Blseek(arrayhan[handle],offset,whence);
#ifdef WITHKPLIB
    else if (groupnum == GRP_ZIP)
    {
        if (kzcurhand != handle)
        {
            if (kztell() >= 0) { arraypos[kzcurhand] = kztell(); kzclose(); }
            kzcurhand = handle;
            kzipopen(filenamsav[handle]);
            kzseek(arraypos[handle],SEEK_SET);
        }
        return kzseek(offset,whence);
    }
#endif

    if (groupfil[groupnum] != -1)
    {
        switch (whence)
        {
        case BSEEK_SET:
            arraypos[handle] = offset; break;
        case BSEEK_END:
        {
            int32_t const i = arrayhan[handle];
            arraypos[handle] = (gfileoffs[groupnum][i+1]-gfileoffs[groupnum][i])+offset;
            break;
        }
        case BSEEK_CUR:
            arraypos[handle] += offset; break;
        }
        return arraypos[handle];
    }
    return -1;
}

int32_t kfilelength_internal(int32_t handle, const uint8_t *arraygrp, intptr_t *arrayhan, int32_t *arraypos)
{
    int32_t const groupnum = arraygrp[handle];
    if (groupnum == GRP_FILESYSTEM)
    {
        return buildvfs_length(arrayhan[handle]);
    }
#ifdef WITHKPLIB
    else if (groupnum == GRP_ZIP)
    {
        if (kzcurhand != handle)
        {
            if (kztell() >= 0) { arraypos[kzcurhand] = kztell(); kzclose(); }
            kzcurhand = handle;
            kzipopen(filenamsav[handle]);
            kzseek(arraypos[handle],SEEK_SET);
        }
        return kzfilelength();
    }
#endif
    int32_t const i = arrayhan[handle];
    return gfileoffs[groupnum][i+1]-gfileoffs[groupnum][i];
}

int32_t ktell_internal(int32_t handle, const uint8_t *arraygrp, intptr_t *arrayhan, int32_t *arraypos)
{
    int32_t groupnum = arraygrp[handle];

    if (groupnum == GRP_FILESYSTEM) return Blseek(arrayhan[handle],0,BSEEK_CUR);
#ifdef WITHKPLIB
    else if (groupnum == GRP_ZIP)
    {
        if (kzcurhand != handle)
        {
            if (kztell() >= 0) { arraypos[kzcurhand] = kztell(); kzclose(); }
            kzcurhand = handle;
            kzipopen(filenamsav[handle]);
            kzseek(arraypos[handle],SEEK_SET);
        }
        return kztell();
    }
#endif
    if (groupfil[groupnum] != -1)
        return arraypos[handle];
    return -1;
}

void kclose_internal(int32_t handle, const uint8_t *arraygrp, intptr_t *arrayhan)
{
    if (handle < 0) return;
    if (arraygrp[handle] == GRP_FILESYSTEM) Bclose(arrayhan[handle]);
#ifdef WITHKPLIB
    else if (arraygrp[handle] == GRP_ZIP)
    {
        kzclose();
        kzcurhand = -1;
    }
#endif
    arrayhan[handle] = -1;
}

int32_t kread(int32_t handle, void *buffer, int32_t leng)
{
    return kread_internal(handle, buffer, leng, filegrp, filehan, filepos);
}
int32_t klseek(int32_t handle, int32_t offset, int32_t whence)
{
    return klseek_internal(handle, offset, whence, filegrp, filehan, filepos);
}
int32_t kfilelength(int32_t handle)
{
    return kfilelength_internal(handle, filegrp, filehan, filepos);
}
int32_t ktell(int32_t handle)
{
    return ktell_internal(handle, filegrp, filehan, filepos);
}
void kclose(int32_t handle)
{
    return kclose_internal(handle, filegrp, filehan);
}

static int32_t kread_grp(int32_t handle, void *buffer, int32_t leng)
{
    return kread_internal(handle, buffer, leng, groupfilgrp, groupfil, groupfilpos);
}
static int32_t klseek_grp(int32_t handle, int32_t offset, int32_t whence)
{
    return klseek_internal(handle, offset, whence, groupfilgrp, groupfil, groupfilpos);
}
static void kclose_grp(int32_t handle)
{
    return kclose_internal(handle, groupfilgrp, groupfil);
}
#endif

int32_t klistaddentry(BUILDVFS_FIND_REC **rec, const char *name, int32_t type, int32_t source)
{
    BUILDVFS_FIND_REC *r = NULL, *attach = NULL;

    if (*rec)
    {
        int32_t insensitive, v;
        BUILDVFS_FIND_REC *last = NULL;

        for (attach = *rec; attach; last = attach, attach = attach->next)
        {
            if (type == BUILDVFS_FIND_DRIVE) continue;	// we just want to get to the end for drives
#ifdef _WIN32
            insensitive = 1;
#else
            if (source == BUILDVFS_SOURCE_GRP || attach->source == BUILDVFS_SOURCE_GRP)
                insensitive = 1;
            else if (source == BUILDVFS_SOURCE_ZIP || attach->source == BUILDVFS_SOURCE_ZIP)
                insensitive = 1;
            else
            {
                extern int16_t editstatus;  // XXX
                insensitive = !editstatus;
            }
            // ^ in the game, don't show file list case-sensitive
#endif
            if (insensitive) v = Bstrcasecmp(name, attach->name);
            else v = Bstrcmp(name, attach->name);

            // sorted list
            if (v > 0) continue;	// item to add is bigger than the current one
            // so look for something bigger than us
            if (v < 0)  			// item to add is smaller than the current one
            {
                attach = NULL;		// so wedge it between the current item and the one before
                break;
            }

            // matched
            if (source >= attach->source) return 1;	// item to add is of lower priority
            r = attach;
            break;
        }

        // wasn't found in the list, so attach to the end
        if (!attach) attach = last;
    }

    if (r)
    {
        r->type = type;
        r->source = source;
        return 0;
    }

    r = (BUILDVFS_FIND_REC *)Xmalloc(sizeof(BUILDVFS_FIND_REC)+strlen(name)+1);

    r->name = (char *)r + sizeof(BUILDVFS_FIND_REC); strcpy(r->name, name);
    r->type = type;
    r->source = source;
    r->usera = r->userb = NULL;

    if (!attach)  	// we are the first item
    {
        r->prev = NULL;
        r->next = *rec;
        if (*rec)(*rec)->prev = r;
        *rec = r;
    }
    else
    {
        r->prev = attach;
        r->next = attach->next;
        if (attach->next) attach->next->prev = r;
        attach->next = r;
    }

    return 0;
}

void klistfree(BUILDVFS_FIND_REC *rec)
{
    BUILDVFS_FIND_REC *n;

    while (rec)
    {
        n = rec->next;
        Xfree(rec);
        rec = n;
    }
}

BUILDVFS_FIND_REC *klistpath(const char *_path, const char *mask, int32_t type)
{
    BUILDVFS_FIND_REC *rec = NULL;
    char *path;

    // pathsearchmode == 0: enumerates a path in the virtual filesystem
    // pathsearchmode == 1: enumerates the system filesystem path passed in

    path = Xstrdup(_path);

    // we don't need any leading dots and slashes or trailing slashes either
    {
        int32_t i,j;
        for (i=0; path[i] == '.' || toupperlookup[path[i]] == '/';) i++;
        for (j=0; (path[j] = path[i]); j++,i++) ;
        while (j>0 && toupperlookup[path[j-1]] == '/') j--;
        path[j] = 0;
        //initprintf("Cleaned up path = \"%s\"\n",path);
    }

    if (*path && (type & BUILDVFS_FIND_DIR))
    {
        if (klistaddentry(&rec, "..", BUILDVFS_FIND_DIR, BUILDVFS_SOURCE_CURDIR) < 0)
        {
            Xfree(path);
            klistfree(rec);
            return NULL;
        }
    }

    if (!(type & BUILDVFS_OPT_NOSTACK))  	// current directory and paths in the search stack
    {

        int32_t stackdepth = BUILDVFS_SOURCE_CURDIR;


#ifdef USE_PHYSFS
        char **rc = PHYSFS_enumerateFiles("");
        char **i;

        for (i = rc; *i != NULL; i++)
        {
            char * name = *i;

            if ((name[0] == '.' && name[1] == 0) ||
                    (name[0] == '.' && name[1] == '.' && name[2] == 0))
                continue;

            bool const isdir = buildvfs_isdir(name);
            if ((type & BUILDVFS_FIND_DIR) && !isdir) continue;
            if ((type & BUILDVFS_FIND_FILE) && isdir) continue;
            if (!Bwildmatch(name, mask)) continue;
            switch (klistaddentry(&rec, name,
                                  isdir ? BUILDVFS_FIND_DIR : BUILDVFS_FIND_FILE,
                                  stackdepth))
            {
            case -1: goto failure;
                //case 1: initprintf("%s:%s dropped for lower priority\n", d,dirent->name); break;
                //case 0: initprintf("%s:%s accepted\n", d,dirent->name); break;
            default:
                break;
            }
        }

        PHYSFS_freeList(rc);
#else
        static const char *const CUR_DIR = "./";
        // Adjusted for the following "autoload" dir fix - NY00123
        searchpath_t *search = NULL;
        const char *d = pathsearchmode ? _path : CUR_DIR;
        char buf[BMAX_PATH];
        BDIR *dir;
        struct Bdirent *dirent;
        do
        {
            if (d==CUR_DIR && (type & BUILDVFS_FIND_NOCURDIR))
                goto next;

            strcpy(buf, d);
            if (!pathsearchmode)
            {
                // Fix for "autoload" dir in multi-user environments - NY00123
                strcat(buf, path);
                if (*path) strcat(buf, "/");
            }
            dir = Bopendir(buf);
            if (dir)
            {
                while ((dirent = Breaddir(dir)))
                {
                    if ((dirent->name[0] == '.' && dirent->name[1] == 0) ||
                            (dirent->name[0] == '.' && dirent->name[1] == '.' && dirent->name[2] == 0))
                        continue;
                    if ((type & BUILDVFS_FIND_DIR) && !(dirent->mode & BS_IFDIR)) continue;
                    if ((type & BUILDVFS_FIND_FILE) && (dirent->mode & BS_IFDIR)) continue;
                    if (!Bwildmatch(dirent->name, mask)) continue;
                    switch (klistaddentry(&rec, dirent->name,
                                          (dirent->mode & BS_IFDIR) ? BUILDVFS_FIND_DIR : BUILDVFS_FIND_FILE,
                                          stackdepth))
                    {
                    case -1: goto failure;
                        //case 1: initprintf("%s:%s dropped for lower priority\n", d,dirent->name); break;
                        //case 0: initprintf("%s:%s accepted\n", d,dirent->name); break;
                    default:
                        break;
                    }
                }
                Bclosedir(dir);
            }
next:
            if (pathsearchmode)
                break;

            if (!search)
            {
                search = searchpathhead;
                stackdepth = BUILDVFS_SOURCE_PATH;
            }
            else
            {
                search = search->next;
                stackdepth++;
            }

            if (search)
                d = search->path;
        }
        while (search);
#endif
    }

#ifndef USE_PHYSFS
#ifdef WITHKPLIB
    if (!(type & BUILDVFS_FIND_NOCURDIR))  // TEMP, until we have sorted out fs.listpath() API
    if (!pathsearchmode)  	// next, zip files
    {
        char buf[BMAX_PATH+4];
        int32_t i, j, ftype;
        strcpy(buf,path);
        if (*path) strcat(buf,"/");
        strcat(buf,mask);
        for (kzfindfilestart(buf); kzfindfile(buf);)
        {
            if (buf[0] != '|') continue;	// local files we don't need

            // scan for the end of the string and shift
            // everything left a char in the process
            for (i=1; (buf[i-1]=buf[i]); i++)
            {
                /* do nothing */
            }
            i-=2;
            if (i < 0)
                i = 0;

            // if there's a slash at the end, this is a directory entry
            if (toupperlookup[buf[i]] == '/') { ftype = BUILDVFS_FIND_DIR; buf[i] = 0; }
            else ftype = BUILDVFS_FIND_FILE;

            // skip over the common characters at the beginning of the base path and the zip entry
            for (j=0; buf[j] && path[j]; j++)
            {
                if (toupperlookup[ path[j] ] == toupperlookup[ buf[j] ]) continue;
                break;
            }
            // we've now hopefully skipped the common path component at the beginning.
            // if that's true, we should be staring at a null byte in path and either any character in buf
            // if j==0, or a slash if j>0
            if ((!path[0] && buf[j]) || (!path[j] && toupperlookup[ buf[j] ] == '/'))
            {
                if (j>0) j++;

                // yep, so now we shift what follows back to the start of buf and while we do that,
                // keep an eye out for any more slashes which would mean this entry has sub-entities
                // and is useless to us.
                for (i = 0; (buf[i] = buf[j]) && toupperlookup[buf[j]] != '/'; i++,j++) ;
                if (toupperlookup[buf[j]] == '/') continue;	// damn, try next entry
            }
            else
            {
                // if we're here it means we have a situation where:
                //   path = foo
                //   buf = foobar...
                // or
                //   path = foobar
                //   buf = foo...
                // which would mean the entry is higher up in the directory tree and is also useless
                continue;
            }

            if ((type & BUILDVFS_FIND_DIR) && ftype != BUILDVFS_FIND_DIR) continue;
            if ((type & BUILDVFS_FIND_FILE) && ftype != BUILDVFS_FIND_FILE) continue;

            // the entry is in the clear
            switch (klistaddentry(&rec, buf, ftype, BUILDVFS_SOURCE_ZIP))
            {
            case -1:
                goto failure;
                //case 1: initprintf("<ZIP>:%s dropped for lower priority\n", buf); break;
                //case 0: initprintf("<ZIP>:%s accepted\n", buf); break;
            default:
                break;
            }
        }
    }
#endif
    // then, grp files
    if (!(type & BUILDVFS_FIND_NOCURDIR))  // TEMP, until we have sorted out fs.listpath() API
    if (!pathsearchmode && !*path && (type & BUILDVFS_FIND_FILE))
    {
        char buf[13];
        int32_t i,j;
        buf[12] = 0;
        for (i=0; i<MAXGROUPFILES; i++)
        {
            if (groupfil[i] == -1) continue;
            for (j=gnumfiles[i]-1; j>=0; j--)
            {
                Bmemcpy(buf,&gfilelist[i][j<<4],12);
                if (!Bwildmatch(buf,mask)) continue;
                switch (klistaddentry(&rec, buf, BUILDVFS_FIND_FILE, BUILDVFS_SOURCE_GRP))
                {
                case -1:
                    goto failure;
                    //case 1: initprintf("<GRP>:%s dropped for lower priority\n", workspace); break;
                    //case 0: initprintf("<GRP>:%s accepted\n", workspace); break;
                default:
                    break;
                }
            }
        }
    }
#endif

    if (pathsearchmode && (type & BUILDVFS_FIND_DRIVE))
    {
        char *drives, *drp;
        drives = Bgetsystemdrives();
        if (drives)
        {
            for (drp=drives; *drp; drp+=strlen(drp)+1)
            {
                if (klistaddentry(&rec, drp, BUILDVFS_FIND_DRIVE, BUILDVFS_SOURCE_DRIVE) < 0)
                {
                    Xfree(drives);
                    goto failure;
                }
            }
            Xfree(drives);
        }
    }

    Xfree(path);
    // XXX: may be NULL if no file was listed, and thus indistinguishable from
    // an error condition.
    return rec;
failure:
    Xfree(path);
    klistfree(rec);
    return NULL;
}


static int32_t kdfread_func(intptr_t fil, void *outbuf, int32_t length)
{
    return kread((buildvfs_kfd)fil, outbuf, length);
}

static void dfwrite_func(intptr_t fp, const void *inbuf, int32_t length)
{
    buildvfs_fwrite(inbuf, length, 1, (buildvfs_FILE)fp);
}


int32_t kdfread(void *buffer, int dasizeof, int count, buildvfs_kfd fil)
{
    return klzw_read_compressed(buffer, dasizeof, count, (intptr_t)fil, kdfread_func);
}

// LZ4_COMPRESSION_ACCELERATION_VALUE can be tuned for performance/space trade-off
// (lower number = higher compression ratio, higher number = faster compression speed)
#define LZ4_COMPRESSION_ACCELERATION_VALUE 5

static char compressedDataStackBuf[131072];
int32_t lz4CompressionLevel = LZ4_COMPRESSION_ACCELERATION_VALUE;

int32_t kdfread_LZ4(void *buffer, int dasizeof, int count, buildvfs_kfd fil)
{
    int32_t leng;

    // read compressed data length
    if (kread(fil, &leng, sizeof(leng)) != sizeof(leng))
        return -1;

    leng = B_LITTLE32(leng);

    char *pCompressedData = compressedDataStackBuf;

    if (leng > ARRAY_SSIZE(compressedDataStackBuf))
        pCompressedData = (char *)Xaligned_alloc(16, leng);

    if (kread(fil, pCompressedData, leng) != leng)
        return -1;

    int32_t decompressedLength = LZ4_decompress_safe(pCompressedData, (char*) buffer, leng, dasizeof*count);

    if (pCompressedData != compressedDataStackBuf)
        Xaligned_free(pCompressedData);

    return decompressedLength/dasizeof;
}


void dfwrite(const void *buffer, int dasizeof, int count, buildvfs_FILE fil)
{
    klzw_write_compressed(buffer, dasizeof, count, (intptr_t)fil, dfwrite_func);
}

void dfwrite_LZ4(const void *buffer, int dasizeof, int count, buildvfs_FILE fil)
{
    char *    pCompressedData   = compressedDataStackBuf;
    int const maxCompressedSize = LZ4_compressBound(dasizeof * count);

    if (maxCompressedSize > ARRAY_SSIZE(compressedDataStackBuf))
        pCompressedData = (char *)Xaligned_alloc(16, maxCompressedSize);

    int const leng = LZ4_compress_fast((const char*) buffer, pCompressedData, dasizeof*count, maxCompressedSize, lz4CompressionLevel);
    int const swleng = B_LITTLE32(leng);

    buildvfs_fwrite(&swleng, sizeof(swleng), 1, fil);
    buildvfs_fwrite(pCompressedData, leng, 1, fil);

    if (pCompressedData != compressedDataStackBuf)
        Xaligned_free(pCompressedData);
}
