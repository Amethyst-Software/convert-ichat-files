//
//  main.c
//  Convert ichat Files
//
//  Created on 12/4/16.
//  Copyright © 2016 Amethyst Software (contact@amethystsoftware.com). All rights reserved.
//

#include <stdbool.h> // bool
#include <stdio.h>   // fprintf()
#include <stdlib.h>  // malloc()
#include <string.h>  // strcpy()
#include "FileIO.h"
#include "bplistReader.h"
#include "ichatReader.h"

#pragma mark Enums
enum ProgramModes
{
    kModeNone,
    kModeConvert,
    kModeBrowse
};

enum OutputFormats
{
    kFormatNone,
    kFormatTXT,
    kFormatRTF
};

#pragma mark Function prototypes
bool ProcessArguments(int argc, const char *argv[]);
void BrowseMenu_bplist(void);
void BrowseMenu_ichat(void);

#pragma mark Globals
bool  gIs_ichat = false;      // whether the file is an iChat log
bool  gTreatAs_ichat = true;  // if false, browse the file as a bplist instead of an iChat log
int   gMode = kModeNone;      // whether to browse or convert file
char *gInFilePath = NULL;     // full path to file to process
char *gInFileName = NULL;     // name of file to process
int   gFormat = kFormatNone;  // whether to convert into TXT or RTF
bool  gFollowRefs = false;    // whether to follow UIDs to the source or just print the UID #s when printing arrays and dicts
bool  gUseRealNames = false;  // whether to look up names given to chat accounts in iChat or use account IDs
bool  gOverwriteFile = false; // whether to overwrite a file by the same name when converting a log
bool  gTrimEmailIDs = false;  // whether to remove '@domain.com' from end of account ID names when converting a log

#pragma mark Functions
int main(int argc, const char *argv[])
{
    if (!ProcessArguments(argc, argv))
        return 1;
    
    if (!LoadInFile(gInFilePath))
        return 1;
    
    if (!Validate_bplist())
        return 1;
    
    if (!Load_bplist())
        return 1;
    
    gIs_ichat = Validate_ichat();
    
    if (gMode == kModeConvert)
        printf("Converting \"%s\"...\n", gInFileName);
    else // kModeBrowse
    {
        printf("Browsing \"%s\"...\n", gInFileName);
        if (gIs_ichat)
            BrowseMenu_bplist(); // set gTreatAs_ichat
    }
    
    if (gIs_ichat && gTreatAs_ichat)
    {
        if (!Load_ichat())
            return 1;
        
        if (gMode == kModeConvert)
            Convert_ichat((gFormat == kFormatRTF));
        else // kModeBrowse
            BrowseMenu_ichat();
    }
    else // handle as generic non-iChat bplist
    {
        if (gMode == kModeConvert)
        {
            printf("Conversion of non-iChat binary plists is not supported.\n");
            return 1;
        }
        else // kModeBrowse
            Browse_bplistElements();
    }

    return 0;
}

// Interpret arguments passed to program
bool ProcessArguments(int argc, const char *argv[])
{
    bool error = false;
    char *mode = NULL, *format = NULL;
    
    // Print usage if the user doesn't seem to know what they're doing
    if (argc < 4)
    {
        printf("Thanks for your interest in \"Convert ichat Files\". Syntax:\n");
        printf(" Arguments:\n");
        printf("   -mode [convert | browse]: Required. Supply \"browse\" as the parameter in order to interactively browse a .ichat file or any other bplist. Supply \"convert\" to convert a .ichat file to a specified output format (specified by \"-format\" argument).\n");
        printf("   -input \"<full path to file>\": Required.\n");
        printf("   -format [TXT | RTF]: Required when using \"convert\" mode. Used to specify which format a .ichat file should be outputted in.\n");
        printf(" Options:\n");
        printf("   --follow-links: When browsing, follow UID links to the objects they reference.\n");
        printf("   --overwrite: When converting, overwrite any existing file with the same name.\n");
        printf("   --real-names: When converting, use the \"real\" names that were attached to participants' accounts in iChat instead of the chat service account IDs.\n");
        printf("   --trim-email-ids: When converting, an account ID such as 'john@doe.com' is written as 'john'.\n");
        return false;
    }
    
    // Look at arguments after our own binary path
    for (int a = 1; a < argc; a++)
    {
        if (!strcmp(argv[a], "-mode"))
        {
            if (a + 1 < argc)
                asprintf(&mode, "%s", argv[++a]); // freed on program quit
            else
                break;
        }
        else if (!strcmp(argv[a], "-input"))
        {
            if (a + 1 < argc)
            {
                asprintf(&gInFilePath, "%s", argv[++a]); // freed on program quit
                
                // Extract file name from full path
                char *lastSlash = strrchr(gInFilePath, '/');
                asprintf(&gInFileName, "%s", lastSlash + 1); // freed on program quit
            }
            else
                break;
        }
        else if (!strcmp(argv[a], "-format"))
        {
            if (a + 1 < argc)
                asprintf(&format, "%s", argv[++a]); // freed on program quit
            else
                break;
        }
        else if (!strcmp(argv[a], "--follow-links"))
            gFollowRefs = true;
        else if (!strcmp(argv[a], "--overwrite"))
            gOverwriteFile = true;
        else if (!strcmp(argv[a], "--real-names"))
            gUseRealNames = true;
        else if (!strcmp(argv[a], "--trim-email-ids"))
            gTrimEmailIDs = true;
    }
    
    // Review arguments received, save parameters, and look for problems
    if (mode != NULL)
    {
        if (!strcmp(mode, "browse"))
            gMode = kModeBrowse;
        else if (!strcmp(mode, "convert"))
            gMode = kModeConvert;
        else
        {
            printf("Fatal error: You need to supply 'browse' or 'convert' as a parameter for the -mode argument.\n");
            error = true;
        }
    }
    if (!error && gInFilePath == NULL)
    {
        printf("Fatal error: You need to supply the full path to the .ichat file or other bplist after the -input argument.\n");
        error = true;
    }
    if (!error && gMode == kModeBrowse && format != NULL)
    {
        printf("Fatal error: You supplied the -format argument which is meant for conversion mode, but you asked for \"browse\" mode instead of \"convert\" mode.\n");
        error = true;
    }
    if (!error && gMode == kModeConvert && format == NULL)
    {
        printf("Fatal error: You need to supply the -format argument followed by 'TXT' or 'RTF' as the format for the converted log.\n");
        error = true;
    }
    if (!error && gMode == kModeConvert && format != NULL)
    {
        if (!strcmp(format, "TXT"))
            gFormat = kFormatTXT;
        else if (!strcmp(format, "RTF"))
            gFormat = kFormatRTF;
        else
        {
            printf("Fatal error: You need to supply 'TXT' or 'RTF' as a parameter for the -format argument.\n");
            error = true;
        }
    }
    
    return !error;
}

// Even though this is an iChat log, for troubleshooting purposes allow the user to browse the file as raw objects decoded by
// bplistReader
void BrowseMenu_bplist(void)
{
    int inputted = 0;
    fflush(stdin);
    do
    {
        char input[3];
        inputted = 0;
        uint64_t inputNum = 0;
        
        printf("The bplist file has been identified as an iChat log. Do you wish to (1) browse it as an iChat log or (2) browse it as a raw plist? Type something other than 1 or 2 to quit.\n");
        if (fgets(input, 3, stdin) != NULL)
            inputted = sscanf(input, "%llu", &inputNum);
        
        if (inputNum == 1)
            gTreatAs_ichat = true;
        else if (inputNum == 2)
            gTreatAs_ichat = false;
        else
        {
            printf("All right, see you later!\n");
            break;
        }
    }
    while (!inputted);
}

// Allow "smart" browsing where messages are printed intelligently or troubleshooting mode where objects are printed through
// bplistReader
void BrowseMenu_ichat(void)
{
    int inputted = 0;
    fflush(stdin);
    do
    {
        char input[3];
        inputted = 0;
        uint64_t inputNum = 0;
        
        printf("Do you want to (1) browse the chat messages smartly or (2) browse the items in '$objects' as raw plist data? Type something other than 1 or 2 to quit.\n");
        if (fgets(input, 3, stdin) != NULL)
            inputted = sscanf(input, "%llu", &inputNum);
        
        if (inputNum == 1)
            Browse_ichatMessages();
        else if (inputNum == 2)
            Browse_ichatObjects();
        else
        {
            printf("All right, maybe next time!\n");
            break;
        }
    }
    while (!inputted);
}
