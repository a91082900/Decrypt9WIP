#include "fs.h"
#include "draw.h"

#include "fatfs/ff.h"

static FATFS fs;
static FIL file;
static DIR dir;

bool InitFS()
{
#ifndef EXEC_GATEWAY
    // TODO: Magic?
    *(u32*)0x10000020 = 0;
    *(u32*)0x10000020 = 0x340;
#endif
    return (f_mount(&fs, "0:", 1) == FR_OK);
}

void DeinitFS()
{
    LogWrite(NULL);
    f_mount(NULL, "0:", 1);
}

bool FileOpen(const char* path)
{
    unsigned flags = FA_READ | FA_WRITE | FA_OPEN_EXISTING;
    #ifdef WORK_DIR
    if (*path == '/' || *path == '\\') path++;
    f_chdir(WORK_DIR);
    bool ret = (f_open(&file, path, flags) == FR_OK);
    f_chdir("/");
    if (!ret) ret = (f_open(&file, path, flags) == FR_OK);
    #else
    bool ret = (f_open(&file, path, flags) == FR_OK);
    #endif
    f_lseek(&file, 0);
    f_sync(&file);
    return ret;
}

bool DebugFileOpen(const char* path)
{
    Debug("Opening %s ...", path);
    if (!FileOpen(path)) {
        Debug("Could not open %s!", path);
        return false;
    }
    
    return true;
}

bool FileCreate(const char* path, bool truncate)
{
    unsigned flags = FA_READ | FA_WRITE;
    flags |= truncate ? FA_CREATE_ALWAYS : FA_OPEN_ALWAYS;
    #ifdef WORK_DIR
    if (*path == '/' || *path == '\\') path++;
    f_chdir(WORK_DIR);
    bool ret = (f_open(&file, path, flags) == FR_OK);
    f_chdir("/");
    #else
    bool ret = (f_open(&file, path, flags) == FR_OK);
    #endif
    f_lseek(&file, 0);
    f_sync(&file);
    return ret;
}

bool DebugFileCreate(const char* path, bool truncate) {
    Debug("Creating %s ...", path);
    if (!FileCreate(path, truncate)) {
        Debug("Could not create %s!", path);
        return false;
    }

    return true;
}

size_t FileCopyTo(const char* dest, void* buf, size_t bufsize)
{
    unsigned flags = FA_READ | FA_WRITE | FA_CREATE_ALWAYS;
    size_t fsize = f_size(&file);
    FIL dfile;
    #ifdef WORK_DIR
    if (*dest == '/' || *dest == '\\') dest++;
    f_chdir(WORK_DIR);
    bool ret = (f_open(&dfile, dest, flags) == FR_OK);
    f_chdir("/");
    #else
    bool ret = (f_open(&dfile, dest, flags) == FR_OK);
    #endif
    if (!ret) return 0;
    f_lseek(&dfile, 0);
    f_sync(&dfile);
    f_lseek(&file, 0);
    f_sync(&file);
    for (size_t pos = 0; pos < fsize; pos += bufsize) {
        UINT bytes_read = 0;
        UINT bytes_written = 0;
        ShowProgress(pos, fsize);
        f_read(&file, buf, bufsize, &bytes_read);
        f_write(&file, buf, bytes_read, &bytes_written);
        if (bytes_read != bytes_written) ret = false;
    }
    ShowProgress(0, 0); 
    return (ret) ? fsize : 0;
}

size_t FileRead(void* buf, size_t size, size_t foffset)
{
    UINT bytes_read = 0;
    f_lseek(&file, foffset);
    f_read(&file, buf, size, &bytes_read);
    return bytes_read;
}

bool DebugFileRead(void* buf, size_t size, size_t foffset) {
    size_t bytesRead = FileRead(buf, size, foffset);
    if(bytesRead != size) {
        Debug("ERROR, file is too small!");
        return false;
    }
    
    return true;
}

size_t FileWrite(void* buf, size_t size, size_t foffset)
{
    UINT bytes_written = 0;
    f_lseek(&file, foffset);
    f_write(&file, buf, size, &bytes_written);
    f_sync(&file);
    return bytes_written;
}

bool DebugFileWrite(void* buf, size_t size, size_t foffset)
{
    size_t bytesWritten = FileWrite(buf, size, foffset);
    if(bytesWritten != size) {
        Debug("ERROR, SD card may be full!");
        return false;
    }
    
    return true;
}

size_t FileGetSize()
{
    return f_size(&file);
}

void FileClose()
{
    f_close(&file);
}

bool DirMake(const char* path)
{
    FRESULT res = f_mkdir(path);
    bool ret = (res == FR_OK) || (res == FR_EXIST);
    return ret;
}

bool DebugDirMake(const char* path)
{
    Debug("Creating dir %s ...", path);
    if (!DirMake(path)) {
        Debug("Could not create %s!", path);
        return false;
    }
    
    return true;
}

bool DirOpen(const char* path)
{
    return (f_opendir(&dir, path) == FR_OK);
}

bool DebugDirOpen(const char* path)
{
    Debug("Opening %s ...", path);
    if (!DirOpen(path)) {
        Debug("Could not open %s!", path);
        return false;
    }
    
    return true;
}

bool DirRead(char* fname, int fsize)
{
    FILINFO fno;
    fno.lfname = fname;
    fno.lfsize = fsize;
    bool ret = false;
    while (f_readdir(&dir, &fno) == FR_OK) {
        if (fno.fname[0] == 0) break;
        if ((fno.fname[0] != '.') && !(fno.fattrib & AM_DIR)) {
            if (fname[0] == 0)
                strcpy(fname, fno.fname);
            ret = true;
            break;
        }
    }
    return ret;
}

void DirClose()
{
    f_closedir(&dir);
}

bool GetFileListWorker(char** list, int* lsize, char* fpath, int fsize, bool recursive)
{
    DIR pdir;
    FILINFO fno;
    char* fname = fpath + strnlen(fpath, fsize - 1);
    bool ret = false;
    
    if (f_opendir(&pdir, fpath) != FR_OK) return false;
    (fname++)[0] = '/';
    fno.lfname = fname;
    fno.lfsize = fsize - (fname - fpath);
    
    while (f_readdir(&pdir, &fno) == FR_OK) {
        if (fno.fname[0] == '.') continue;
        if (fname[0] == 0)
            strcpy(fname, fno.fname);
        if (fno.fname[0] == 0) {
            ret = true;
            break;
        } else if (fno.fattrib & AM_DIR) {
            if (recursive && !GetFileListWorker(list, lsize, fpath, fsize, recursive))
                break;
        } else {
            snprintf(*list, *lsize, "%s\n", fpath);
            for(;(*list)[0] != '\0' && (*lsize) > 1; (*list)++, (*lsize)--); 
            if ((*lsize) <= 1) break;
        }
    }
    f_closedir(&pdir);
    
    return ret;
}

bool GetFileList(const char* path, char* list, int lsize, bool recursive)
{
    char fpath[256];
    strncpy(fpath, path, 256);
    return GetFileListWorker(&list, &lsize, fpath, 256, recursive);
}

size_t LogWrite(const char* text)
{
    #ifdef LOG_FILE
    static FIL lfile;
    static bool lready = false;
    static size_t lstart = 0;
    
    if ((text == NULL) && lready) {
        f_sync(&lfile);
        f_close(&lfile);
        lready = false;
        return lstart; // return the log start
    } else if (text == NULL) {
        return 0;
    }
    
    if (!lready) {
        unsigned flags = FA_READ | FA_WRITE | FA_OPEN_ALWAYS;
        #ifdef WORK_DIR
        f_chdir(WORK_DIR);
        lready = (f_open(&lfile, LOG_FILE, flags) == FR_OK);
        f_chdir("/");
        if (!lready) lready = (f_open(&lfile, LOG_FILE, flags) == FR_OK);
        #else
        lready = (f_open(&lfile, LOG_FILE, flags) == FR_OK);
        #endif
        if (!lready) return 0;
        lstart = f_size(&lfile);
        f_lseek(&lfile, lstart);
        f_sync(&lfile);
    }
    
    const char newline = '\n';
    UINT bytes_written;
    UINT tlen = strnlen(text, 128); 
    f_write(&lfile, text, tlen, &bytes_written);
    if (bytes_written != tlen) return 0;
    f_write(&lfile, &newline, 1, &bytes_written);
    if (bytes_written != 1) return 0;
    #endif
    
    return f_size(&lfile); // return the current position
}

static uint64_t ClustersToBytes(FATFS* fs, DWORD clusters)
{
    uint64_t sectors = clusters * fs->csize;
#if _MAX_SS != _MIN_SS
    return sectors * fs->ssize;
#else
    return sectors * _MAX_SS;
#endif
}

uint64_t RemainingStorageSpace()
{
    DWORD free_clusters;
    FATFS *fs2;
    FRESULT res = f_getfree("0:", &free_clusters, &fs2);
    if (res)
        return -1;

    return ClustersToBytes(&fs, free_clusters);
}

uint64_t TotalStorageSpace()
{
    return ClustersToBytes(&fs, fs.n_fatent - 2);
}
