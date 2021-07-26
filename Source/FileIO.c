//
//  FileIO.c
//  Convert ichat Files
//
//  Created on 3/26/17.
//  Copyright © 2017 Amethyst Software (contact@amethystsoftware.com). All rights reserved.
//

#include <errno.h>   // errno
#include <stdbool.h> // bool
#include <stdio.h>   // fprintf()
#include <stdlib.h>  // malloc()
#include <string.h>  // strerror()
#include "FileIO.h"

#define FILE_SIZE_MAX_MB 5
#define FILE_SIZE_MAX    (FILE_SIZE_MAX_MB * 1024 * 1024)

char  *gInFileContents = NULL;
size_t gInFileLength = 0;
char  *gOutFilePath = NULL;
FILE  *gOutFileHandle = NULL;

extern char *gInFilePath;
extern bool  gOverwriteFile;

// Compiled from various file-related functions' man pages
FileError gErrorTable[] =
{
    {EACCES,       "search permission denied"},
    {EBADF,        "stream not seekable"},
    {EFAULT,       "invalid address"},
    {EINVAL,       "seek location negative or argument has improper value"},
    {EIO,          "I/O error"},
    {ELOOP,        "possible symlink loop"},
    {ENAMETOOLONG, "name too long"},
    {ENOENT,       "does not exist"},
    {ENOMEM,       "malloc failure"},
    {ENOTDIR,      "a component of the file path is not a directory"},
    {EOVERFLOW,    "seek location too large to be stored in off_t/long"},
    {ESPIPE,       "stream's file desc. associated with pipe, socket or FIFO; or file-position indicator is unspecified"},
    {0,            ""}
};

#pragma mark Input file
// Load file from disk which is going to be examined and browsed/converted
bool LoadInFile(char *srcPath)
{
#define DieIf(boole) \
if (boole) \
{ \
   ReportInFileError(stream); \
   return false; \
} \
do {} while (0)
    
    long result = 0;
    
    FILE *stream = fopen(srcPath, "r");
    DieIf(stream == NULL);
    
    result = fseek(stream, 0, SEEK_END);
    DieIf(result == -1);
    
    result = ftell(stream);
    DieIf(result == -1);
    
    gInFileLength = (size_t)result;
    if (gInFileLength > FILE_SIZE_MAX)
    {
        printf("Fatal error: File is over the limit of %d megabytes.\n", FILE_SIZE_MAX_MB);
        return false;
    }
    
    rewind(stream);
    DieIf(ferror(stream));
    
    gInFileContents = calloc((unsigned long)(gInFileLength + 1), 1); // freed on program quit
    if (gInFileContents == NULL)
    {
        printf("Fatal error: Memory allocation failed.\n");
        return false;
    }
    
    result = (long)fread(gInFileContents, 1, gInFileLength, stream);
    DieIf(result != gInFileLength);
    
    fclose(stream);
    
    return true;
    
#undef DieIf
}

// Report on whatever error occurred when working with the in file
void ReportInFileError(FILE *stream)
{
    int error = 0;
    
    if (stream == NULL)
        error = errno;
    else
        error = ferror(stream);
    
    FileError *e;
    for (e = gErrorTable; e->feCode != 0; e++)
    {
        if (e->feCode == error)
        {
            printf("Fatal file error %d occurred: %s.\n", e->feCode, e->feDesc);
            break;
        }
    }
    if (e->feCode == 0)
        printf("Fatal file error occurred. Could not obtain details.\n");
}
#pragma mark Output file
// Create RTF or TXT file for converted chat log
bool CreateOutFile(bool useRTF)
{
    char *suffix = (useRTF ? "rtf" : "txt");
    
    // Change suffix of gInFileName to .rtf or .txt and save in gOutFilePath
    asprintf(&gOutFilePath, "%s", gInFilePath); // freed on program quit
    char *dotPosition = strrchr(gOutFilePath, '.');
    if (dotPosition == NULL)
    {
        printf("Fatal error: Could not create output file name!\n");
        return false;
    }
    strncpy(dotPosition + 1, suffix, 4);
    
    gOutFileHandle = fopen(gOutFilePath, (gOverwriteFile ? "w" : "wx"));
    
    // Check for pre-existing file with this name
    if (!gOutFileHandle)
    {
        if (errno == 17) // "File exists"
        {
            char *fileName = NULL;
            char *lastSlash = strrchr(gOutFilePath, '/');
            asprintf(&fileName, "%s", lastSlash + 1); // freed below
            printf("Skipping conversion; \"%s\" already exists.\n", fileName);
            free(fileName);
        }
        else
            printf("Fatal error %d: \"%s\". Could not create output file.\n", errno, strerror(errno));
        return false;
    }
    
    return true;
}

// Write provided text to out file
void WriteToOutFile(char *output)
{
    fprintf(gOutFileHandle, "%s", output);
}

// Close file now that we are done with it
void CloseOutFile(void)
{
    fflush(gOutFileHandle);
    fclose(gOutFileHandle);
}
