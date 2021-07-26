//
//  ichatReader.c
//  Convert ichat Files
//
//  Created on 12/8/18.
//  Copyright © 2018 Amethyst Software (contact@amethystsoftware.com). All rights reserved.
//

#include <locale.h>  // setlocale()
#include <stdbool.h> // bool
#include <stdio.h>   // fprintf()
#include <stdlib.h>  // malloc()
#include <string.h>  // strcpy()
#include "bplistReader.h"
#include "FileIO.h"
#include "ichatReader.h"

#pragma mark Globals
const int kVersion_ichat = 100000; // only known version of iChat log format

BPObject gObjectsArray;            // "$objects", the array object that points to all chat messages and metadata
BPObject gMessageListArray;        // the array object that points to all messages in the chat
uint64_t gNumParticipantNames = 0; // number of account IDs pointed to by gParticipantNames
char   **gParticipantNames = NULL; // pointer to array of pointers to "real" names of participants
uint64_t gNumParticipantIDs = 0;   // number of account IDs pointed to by gParticipantIDs
char   **gParticipantIDs = NULL;   // pointer to array of pointers to account IDs of participants
char    *gFirstMsgTime = NULL;     // long-format timestamp representing beginning of chat
char    *gClientName = "iChat";    // name to use when message sender is the chat client itself

extern uint64_t gRootObjID;
extern bool     gUseRealNames;
extern bool     gTrimEmailIDs;

#pragma mark Chat-level functions
// Determine if this binary plist is an iChat log
bool Validate_ichat(void)
{
    BPObject root, value;
    
    // Load root object, which should be a dictionary
    if (!LoadObject(gRootObjID, &root))
        return false;
    if (root.oType != kTypeDict)
    {
        //printf("Root object of this bplist is not a dictionary.\n");
        return false;
    }
    
    // Look for "$version" in root dict, which should be an 'int'
    uint64_t valueRef = ReturnValueRefForKeyName(&root, "$version");
    if (valueRef == (uint64_t)-1)
    {
        //printf("Could not find '$version' in root object, so this is probably not an iChat log.\n");
        return false;
    }
    if (!LoadObject(valueRef, &value))
        return false;
    if (value.oType != kTypeInt)
    {
        //printf("Value for key '$version' is not of type 'int'.\n");
        return false;
    }
    
    // Verify iChat format version
    if (value.oInt != kVersion_ichat)
    {
        printf("This is an unknown version of iChat log: %llu.\n", value.oInt);
        return false;
    }
    
    // Locate "$objects" array which contains the chat messages
    valueRef = ReturnValueRefForKeyName(&root, "$objects");
    if (valueRef == (uint64_t)-1)
    {
        //printf("Could not find '$objects' in file, so this is probably not an iChat log.\n");
        return false;
    }
    if (!LoadObject(valueRef, &gObjectsArray))
        return false;
    if (gObjectsArray.oType != kTypeArray)
    {
        //printf("Found '$objects' but it is not an array!\n");
        return false;
    }
    
    return true;
}

// Load any relevant metadata about chat
bool Load_ichat(void)
{
#define DieIf(boole) \
if (boole) \
{ \
printf("Failed test on line %d in %s.\n", __LINE__, __FILE__); \
return false; \
} \
do {} while (0)
    
    /* Load list of message IDs into memory */
    BPObject messageListDict;
    
    // Load dict with array of message IDs
    uint64_t messageListDictRef = ReturnElemRef(&gObjectsArray, 4);
    DieIf(messageListDictRef == (uint64_t)-1);
    DieIf(!LoadObject(messageListDictRef, &messageListDict));
    DieIf(messageListDict.oType != kTypeDict);
    
    // Load array of message IDs
    uint64_t messageListArrayRef = ReturnValueRefForKeyName(&messageListDict, "NS.objects");
    DieIf(messageListArrayRef == (uint64_t)-1);
    DieIf(!LoadObject(messageListArrayRef, &gMessageListArray));
    DieIf(gMessageListArray.oType != kTypeArray);
    
    /* Load "real names" and account IDs of participants into memory */
    BPObject root, top, metadataID, metadata, metadataKeys, metadataValues, participantsDictID, participantsDict, participantsArray, participantID, participant, participantName, presentityDictID, presentityDict, presentityArray, presentityID, presentity, presentityName;
    
    // Look for dict called "$top" in root object
    DieIf(!LoadObject(gRootObjID, &root));
    uint64_t topRef = ReturnValueRefForKeyName(&root, "$top");
    DieIf(topRef == (uint64_t)-1);
    DieIf(!LoadObject(topRef, &top));
    DieIf(top.oType != kTypeDict);
    
    // Look for "metadata" in dict, which is a UID leading to the metadata dict
    uint64_t metadataIDRef = ReturnValueRefForKeyName(&top, "metadata");
    DieIf(metadataIDRef == (uint64_t)-1);
    DieIf(!LoadObject(metadataIDRef, &metadataID));
    DieIf(metadataID.oType != kTypeUID);
    
    // Load metadata dict with that UID
    uint64_t metadataDictRef = ReturnElemRef(&gObjectsArray, metadataID.oInt);
    DieIf(metadataDictRef == (uint64_t)-1);
    DieIf(!LoadObject(metadataDictRef, &metadata));
    DieIf(metadata.oType != kTypeDict);
    
    // Load "NS.keys" in metadata
    uint64_t metadataKeysRef = ReturnValueRefForKeyName(&metadata, "NS.keys");
    DieIf(metadataKeysRef == (uint64_t)-1);
    DieIf(!LoadObject(metadataKeysRef, &metadataKeys));
    DieIf(metadataKeys.oType != kTypeArray);
    
    // Search "NS.keys" array for keys called "Participants" and "PresentityIDs"
    int partIndex = -1, presIndex = -1;
    for (int a = 0; a < metadataKeys.oSize; a++)
    {
        BPObject metadataKeyID, metadataKey;
        
        // Load UID that points to name of this key
        uint64_t metadataKeyIDref = ReturnElemRef(&metadataKeys, (uint64_t)a);
        DieIf(metadataKeyIDref == (uint64_t)-1);
        DieIf(!LoadObject(metadataKeyIDref, &metadataKeyID));
        DieIf(metadataKeyID.oType != kTypeUID);
        
        // Load name of this key
        uint64_t metadataKeyRef = ReturnElemRef(&gObjectsArray, metadataKeyID.oInt);
        DieIf(metadataKeyRef == (uint64_t)-1);
        DieIf(!LoadObject(metadataKeyRef, &metadataKey));
        DieIf(metadataKey.oType != kTypeStringASCII);
        
        if (!strcmp(metadataKey.oData, "Participants"))
            partIndex = a;
        else if (!strcmp(metadataKey.oData, "PresentityIDs"))
            presIndex = a;
    }
    DieIf(partIndex == -1);
    DieIf(presIndex == -1);
    
    // Load "NS.objects" array that corresponds to "NS.keys"
    uint64_t metadataValuesRef = ReturnValueRefForKeyName(&metadata, "NS.objects");
    DieIf(metadataValuesRef == (uint64_t)-1);
    DieIf(!LoadObject(metadataValuesRef, &metadataValues));
    DieIf(metadataValues.oType != kTypeArray);
    
    // Load UID in "NS.objects" that points to the dict that represents the value corresponding to the "Participants" key
    uint64_t participantsIDref = ReturnElemRef(&metadataValues, (uint64_t)partIndex);
    DieIf(participantsIDref == (uint64_t)-1);
    DieIf(!LoadObject(participantsIDref, &participantsDictID));
    DieIf(participantsDictID.oType != kTypeUID);
    
    // Load the dict that represents the value corresponding to the "Participants" key
    uint64_t participantsDictRef = ReturnElemRef(&gObjectsArray, participantsDictID.oInt);
    DieIf(participantsDictRef == (uint64_t)-1);
    DieIf(!LoadObject(participantsDictRef, &participantsDict));
    DieIf(participantsDict.oType != kTypeDict);
    
    // Load the array in the dict that references a dict/string for each participant
    uint64_t participantsListArrayRef = ReturnValueRefForKeyName(&participantsDict, "NS.objects");
    DieIf(participantsListArrayRef == (uint64_t)-1);
    DieIf(!LoadObject(participantsListArrayRef, &participantsArray));
    DieIf(participantsArray.oType != kTypeArray);
    
    // Record number of participants (might be a group chat) and allocate space for pointers to their names
    gNumParticipantNames = participantsArray.oSize;
    gParticipantNames = malloc(gNumParticipantNames * sizeof(char *)); // freed on program quit
    for (int a = 0; a < gNumParticipantNames; a++)
        gParticipantNames[a] = NULL;
    
    // Read names of participants into memory
    for (int a = 0; a < gNumParticipantNames; a++)
    {
        // Load UID pointing to name dict/string
        uint64_t nameID_IDref = ReturnElemRef(&participantsArray, (uint64_t)a);
        DieIf(nameID_IDref == (uint64_t)-1);
        DieIf(!LoadObject(nameID_IDref, &participantID));
        DieIf(participantID.oType != kTypeUID);
        
        // Load name dict/string
        uint64_t participantRef = ReturnElemRef(&gObjectsArray, participantID.oInt);
        DieIf(participantRef == (uint64_t)-1);
        DieIf(!LoadObject(participantRef, &participant));
        
        // The chat client user's name may be stored in a dict, whereas other participants are stored as straight ASCII strings,
        // so read name differently depending on what kind of object we just got
        if (participant.oType == kTypeDict)
        {
            // Look up value for key "NS.string"
            uint64_t participantNameRef = ReturnValueRefForKeyName(&participant, "NS.string");
            DieIf(participantNameRef == (uint64_t)-1);
            DieIf(!LoadObject(participantNameRef, &participantName));
            DieIf(participantName.oType != kTypeStringASCII);
            
            // Save participant's name
            if (participantName.oSize > 0)
            {
                gParticipantNames[a] = malloc(participantName.oSize + 1); // freed on program quit
                strncpy(gParticipantNames[a], participantName.oData, participantName.oSize);
                gParticipantNames[a][participantName.oSize] = '\0';
            }
            else
                asprintf(&gParticipantNames[a], "%s", "<empty>");
        }
        else if (participant.oType == kTypeStringASCII)
        {
            // Save participant's name
            if (participant.oSize > 0)
            {
                gParticipantNames[a] = malloc(participant.oSize + 1); // freed on program quit
                strncpy(gParticipantNames[a], participant.oData, participant.oSize);
                gParticipantNames[a][participant.oSize] = '\0';
            }
            else
                asprintf(&gParticipantNames[a], "%s", "<empty>");
        }
        else if (participant.oType == kTypeStringUnicode)
        {
            // This is probably because the participant name is embedded in "left-to-right" tags (0x202A/0x202C); we will strip all
            // Unicode characters as we translate to UTF-8 to produce a straight ASCII string, for simplicity's sake
            gParticipantNames[a] = calloc((participant.oSize * 2) + 1, 1); // freed on program quit
            for (int b = 0; b < participant.oSize * 2; b += 2)
            {
                char *bytes = NULL;
                ConvertUnicodeToUTF8((participant.oData + b), &bytes);
                if (strlen(bytes) == 1)
                    strcat(gParticipantNames[a], bytes);
            }
            // If all of the text was Unicode characters, we have an empty string on our hands, so put something in it
            if (strlen(gParticipantNames[a]) == 0)
            {
                free(gParticipantNames[a]);
                asprintf(&gParticipantNames[a], "%s", "<Unicode>");
            }
        }
        else DieIf(true);
    }
    
    // Load UID in "NS.objects" that points to the dict that represents the value corresponding to the "PresentityIDs" key
    uint64_t presentityIDref = ReturnElemRef(&metadataValues, (uint64_t)presIndex);
    DieIf(presentityIDref == (uint64_t)-1);
    DieIf(!LoadObject(presentityIDref, &presentityDictID));
    DieIf(presentityDictID.oType != kTypeUID);
    
    // Load the dict that represents the value corresponding to the "PresentityIDs" key
    uint64_t presentityDictRef = ReturnElemRef(&gObjectsArray, presentityDictID.oInt);
    DieIf(presentityDictRef == (uint64_t)-1);
    DieIf(!LoadObject(presentityDictRef, &presentityDict));
    DieIf(presentityDict.oType != kTypeDict);
    
    // Load the array in the dict that references a dict/string for each account ID
    uint64_t presentityListArrayRef = ReturnValueRefForKeyName(&presentityDict, "NS.objects");
    DieIf(presentityListArrayRef == (uint64_t)-1);
    DieIf(!LoadObject(presentityListArrayRef, &presentityArray));
    DieIf(presentityArray.oType != kTypeArray);
    
    // Record number of account IDs (might be a group chat) and allocate space for pointers to their IDs
    gNumParticipantIDs = presentityArray.oSize;
    gParticipantIDs = malloc(gNumParticipantIDs * sizeof(char *)); // freed on program quit
    for (int a = 0; a < gNumParticipantIDs; a++)
        gParticipantIDs[a] = NULL;
    
    // Read account IDs of participants into memory
    for (int a = 0; a < gNumParticipantIDs; a++)
    {
        // Load UID pointing to account ID dict/string
        uint64_t nameID_IDref = ReturnElemRef(&presentityArray, (uint64_t)a);
        DieIf(nameID_IDref == (uint64_t)-1);
        DieIf(!LoadObject(nameID_IDref, &presentityID));
        DieIf(presentityID.oType != kTypeUID);
        
        // Load account ID dict/string
        uint64_t presentityRef = ReturnElemRef(&gObjectsArray, presentityID.oInt);
        DieIf(presentityRef == (uint64_t)-1);
        DieIf(!LoadObject(presentityRef, &presentity));
        
        // The chat client user's account ID may be stored in a dict, whereas other participants are stored as straight ASCII strings,
        // so read ID differently depending on what kind of object we just got
        if (presentity.oType == kTypeDict)
        {
            // Look up value for key "NS.string"
            uint64_t presentityNameRef = ReturnValueRefForKeyName(&presentity, "NS.string");
            DieIf(presentityNameRef == (uint64_t)-1);
            DieIf(!LoadObject(presentityNameRef, &presentityName));
            DieIf(presentityName.oType != kTypeStringASCII);
            
            // Save participant's account ID
            if (presentityName.oSize > 0)
            {
                gParticipantIDs[a] = malloc(presentityName.oSize + 1); // freed on program quit
                strncpy(gParticipantIDs[a], presentityName.oData, presentityName.oSize);
                gParticipantIDs[a][presentityName.oSize] = '\0';
                
                if (gTrimEmailIDs)
                {
                    char *atPosition = strchr(gParticipantIDs[a], '@');
                    if (atPosition != NULL)
                        *atPosition = '\0'; // end string at '@'
                }
            }
            else
                asprintf(&gParticipantIDs[a], "%s", "<empty>");
        }
        else if (presentity.oType == kTypeStringASCII)
        {
            // Save participant's account ID
            if (presentity.oSize > 0)
            {
                gParticipantIDs[a] = malloc(presentity.oSize + 1); // freed on program quit
                strncpy(gParticipantIDs[a], presentity.oData, presentity.oSize);
                gParticipantIDs[a][presentity.oSize] = '\0';
                
                if (gTrimEmailIDs)
                {
                    char *atPosition = strchr(gParticipantIDs[a], '@');
                    if (atPosition != NULL)
                        *atPosition = '\0'; // end string at '@'
                }
            }
            else
                asprintf(&gParticipantIDs[a], "%s", "<empty>");
        }
        else if (presentity.oType == kTypeStringUnicode)
        {
            // I have not encountered this case in a chat log, so simply apply the approach used for Unicode participant names (see
            // comment under line "else if (participant.oType == kTypeStringUnicode)" above) and hope that it works
            gParticipantIDs[a] = calloc((presentity.oSize * 2) + 1, 1); // freed on program quit
            for (int b = 0; b < presentity.oSize * 2; b += 2)
            {
                char *bytes = NULL;
                ConvertUnicodeToUTF8((presentity.oData + b), &bytes);
                if (strlen(bytes) == 1)
                    strcat(gParticipantIDs[a], bytes);
            }
            // If all of the text was Unicode characters, we have an empty string on our hands, so put something in it
            if (strlen(gParticipantIDs[a]) == 0)
            {
                free(gParticipantIDs[a]);
                asprintf(&gParticipantIDs[a], "%s", "<Unicode>");
            }
        }
        else DieIf(true);
    }
    
    /*printf("Got the following IDs and \"real names\":\n");
    for (int a = 0; a < gNumParticipantIDs; a++)
        printf("ID %d: %s\n", a, gParticipantIDs[a]);
    for (int a = 0; a < gNumParticipantNames; a++)
        printf("Name %d: %s\n", a, gParticipantNames[a]);*/
    
    return true;
#undef DieIf
}

// Allow user to browse iChat log's "$objects" array interactively
void Browse_ichatObjects(void)
{
    BPObject o;
    char input[10];
    int inputted = 0;
    uint64_t inputNum = 0;
    do
    {
        printf("Type any letter to exit, or enter the UID [0-%llu] of the item in '$objects' to print:\n", gObjectsArray.oSize - 1);
        if (fgets(input, 10, stdin) != NULL)
            inputted = sscanf(input, "%llu", &inputNum);
        
        if (inputted == 0)
        {
            printf("Sayonara!\n");
            break;
        }
        
        if (inputNum >= gObjectsArray.oSize)
        {
            printf("Error: Input %lld out of range. Try again.\n", inputNum);
            continue;
        }
        
        uint64_t UID = ReturnElemRef(&gObjectsArray, inputNum);
        if (UID == (uint64_t)-1)
            return;
        if (!LoadObject(UID, &o))
            return;
        PrintObject(&o);
    }
    while (true);
}

// Allow user to browse message objects smartly
void Browse_ichatMessages(void)
{
    BPObject BPmsg;
    ICMessage ICmsg;
    char input[10];
    int inputted = 0;
    int64_t inputNum = 0;
    do
    {
        printf("Type any letter to exit, or enter the number [1-%llu] of the chat message to print, or enter 0 to print the whole chat:\n", gMessageListArray.oSize);
        if (fgets(input, 10, stdin) != NULL)
            inputted = sscanf(input, "%lld", &inputNum);
        
        if (inputted == 0)
        {
            printf("Adios!\n");
            break;
        }
        
        if (inputNum == 0)
        {
            for (int a = 0; a < gMessageListArray.oSize; a++)
            {
                uint64_t msgIDref = ReturnMessageRef((uint64_t)a);
                if (msgIDref == (uint64_t)-1) return;
                if (!LoadObject(msgIDref, &BPmsg)) return;
                InitMessage(&ICmsg);
                if (LoadMessage(&BPmsg, &ICmsg, (a == 0)))
                    PrintMessage(&ICmsg);
                DeleteMessage(&ICmsg);
            }
        }
        else if (inputNum >= 1 && inputNum <= gMessageListArray.oSize)
        {
            uint64_t msgIDref = ReturnMessageRef((uint64_t)inputNum - 1);
            if (msgIDref == (uint64_t)-1) return;
            if (!LoadObject(msgIDref, &BPmsg)) return;
            InitMessage(&ICmsg);
            if (LoadMessage(&BPmsg, &ICmsg, false))
                PrintMessage(&ICmsg);
            DeleteMessage(&ICmsg);
        }
        else
        {
            printf("Error: Input %lld out of range. Try again.\n", inputNum);
            continue;
        }
    }
    while (true);
}

// Convert iChat log to TXT or RTF based on "useRTF"
void Convert_ichat(bool useRTF)
{
    if (!CreateOutFile(useRTF))
        return;
    
    if (useRTF)
        WriteRTFHeader();
    
    BPObject BPmsg;
    ICMessage ICmsg;
    for (int a = 0; a < gMessageListArray.oSize; a++)
    {
        uint64_t msgIDref = ReturnMessageRef((uint64_t)a);
        if (msgIDref == (uint64_t)-1)
            return;
        if (!LoadObject(msgIDref, &BPmsg))
            return;
        InitMessage(&ICmsg);
        if (!LoadMessage(&BPmsg, &ICmsg, (a == 0)))
        {
            DeleteMessage(&ICmsg);
            return;
        }
        
        if (a == 0)
            WriteTimeHeader(useRTF); // has to take place after LoadMessage() is called on first message
        
        if (useRTF)
            ConvertMessageToRTF(&ICmsg);
        else
            ConvertMessageToTXT(&ICmsg);
        
        DeleteMessage(&ICmsg);
    }
    
    if (useRTF)
        WriteRTFFooter();
    
    CloseOutFile();
}
#pragma mark Message-level functions
// Initializes a message
void InitMessage(ICMessage *msg)
{
    msg->mHiccup = false;
    msg->mFromClient = false;
    msg->mFileTransfer = 0;
    msg->mSenderID = NULL;
    msg->mTime = NULL;
    msg->mText = NULL;
    msg->mWideStrSize = 0;
}

// Uses the BPObject dict passed in to look up the key data for a chat message and save it as an ICMessage. Warning: This function is
// absolutely *filled* with "return" statements, mostly in the form of DieIf() calls.
bool LoadMessage(BPObject *BPmsg, ICMessage *ICmsg, bool firstMsg)
{
#define DieIf(boole) \
if (boole) \
{ \
printf("Failed test on line %d in %s.\n", __LINE__, __FILE__); \
free(subject); \
return false; \
} \
do {} while (0)
    
    char *subject = NULL;
    
    // Determine if this is a message from the client or from a participant by looking for key "StatusChatItemStatusType" and seeing if
    // its value is "1" (participant has come online) or "2" (they have gone offline). The key can exist and have value "0", which seems
    // to have no meaning because the message will be an ordinary chat message. Usually the key does not exist at all in a message.
    bool isClient = false;
    BPObject statusType;
    uint64_t statusTypeRef = ReturnValueRefForKeyName(BPmsg, "StatusChatItemStatusType");
    if (statusTypeRef != (uint64_t)-1)
    {
        DieIf(!LoadObject(statusTypeRef, &statusType));
        DieIf(statusType.oType != kTypeInt);
        if (statusType.oInt == 1 || statusType.oInt == 2)
            isClient = true;
    }
    
    // If this is a client message, save the name of the subject of the message, if there is one
    if (isClient)
    {
        /* Save the subject's account ID */
        BPObject subjectDictID, subjectDict, subjectNameID, subjectName, subjectNameStr;
        ICmsg->mFromClient = true;
        
        // Look up value for key "Subject", which is a UID pointing to a dict with a UID pointing to a dict with the subject's ID
        uint64_t subjectDictID_IDref = ReturnValueRefForKeyName(BPmsg, "Subject");
        DieIf(subjectDictID_IDref == (uint64_t)-1);
        DieIf(!LoadObject(subjectDictID_IDref, &subjectDictID));
        DieIf(subjectDictID.oType != kTypeUID);
        
        // Load dict with UID pointing to subject dict
        uint64_t subjectDictIDref = ReturnElemRef(&gObjectsArray, subjectDictID.oInt);
        DieIf(subjectDictIDref == (uint64_t)-1);
        DieIf(!LoadObject(subjectDictIDref, &subjectDict));
        DieIf(subjectDict.oType != kTypeDict);
        
        // Look up value for key "ID", which is a UID pointing to the subject's account ID
        uint64_t subjectNameIDref = ReturnValueRefForKeyName(&subjectDict, "ID");
        DieIf(subjectNameIDref == (uint64_t)-1);
        DieIf(!LoadObject(subjectNameIDref, &subjectNameID));
        DieIf(subjectNameID.oType != kTypeUID);
        
        // Load dict with subject's account ID
        uint64_t subjectNameRef = ReturnElemRef(&gObjectsArray, subjectNameID.oInt);
        DieIf(subjectNameRef == (uint64_t)-1);
        DieIf(!LoadObject(subjectNameRef, &subjectName));
        if (subjectName.oType == kTypeDict)
        {
            // Look up value for key "NS.string"
            uint64_t subjectNameStrRef = ReturnValueRefForKeyName(&subjectName, "NS.string");
            DieIf(subjectNameStrRef == (uint64_t)-1);
            DieIf(!LoadObject(subjectNameStrRef, &subjectNameStr));
            DieIf(subjectNameStr.oType != kTypeStringASCII);
            
            // Save subject ID in ICMessage
            uint64_t nameLength = strlen(subjectNameStr.oData);
            subject = malloc(nameLength + 1); // freed with DieIf() or at end of function
            strncpy(subject, subjectNameStr.oData, nameLength);
            subject[nameLength] = '\0';
        }
        else if (subjectName.oType == kTypeStringASCII)
        {
            // Save subject ID in ICMessage
            uint64_t nameLength = strlen(subjectName.oData);
            subject = malloc(nameLength + 1); // freed with DieIf() or at end of function
            strncpy(subject, subjectName.oData, nameLength);
            subject[nameLength] = '\0';
        }
        else if (subjectName.oType == kTypeStringUnicode)
        {
            // I have not encountered this case in a chat log, so simply apply the approach used for Unicode participant names (see
            // comment under line "else if (participant.oType == kTypeStringUnicode)" above) and hope that it works
            subject = calloc((subjectName.oSize * 2) + 1, 1); // freed on program quit
            for (int b = 0; b < subjectName.oSize * 2; b += 2)
            {
                char *bytes = NULL;
                ConvertUnicodeToUTF8((subjectName.oData + b), &bytes);
                if (strlen(bytes) == 1)
                    strcat(subject, bytes);
            }
            // If all of the text was Unicode characters, we have an empty string on our hands, so put something in it
            if (strlen(subject) == 0)
            {
                free(subject);
                asprintf(&subject, "%s", "<Unicode>");
            }
        }
        else DieIf(true);
    }
    else
    {
        /* Save the sender's account ID (if the user asked for real names, the account will get replaced by their name when writing
         the message to disk) */
        BPObject senderDictID, senderDict, senderNameID, senderName, senderNameStr;
        
        // Look up value for key "Sender", which is a UID pointing to a dict with a UID pointing to a dict with the sender's ID
        uint64_t senderDictID_IDref = ReturnValueRefForKeyName(BPmsg, "Sender");
        DieIf(senderDictID_IDref == (uint64_t)-1);
        DieIf(!LoadObject(senderDictID_IDref, &senderDictID));
        DieIf(senderDictID.oType != kTypeUID);
        // UID 0 always seems to be "$null" and always seems to mean that the client is talking. We should have caught this above by
        // looking at "StatusChatItemStatusType", but if this somehow happens anyway, at least set this flag so we handle message
        // somewhat appropriately.
        if (senderDictID.oInt == 0)
            ICmsg->mFromClient = true;
        else
        {
            // Load dict with UID pointing to sender dict
            uint64_t senderDictIDref = ReturnElemRef(&gObjectsArray, senderDictID.oInt);
            DieIf(senderDictIDref == (uint64_t)-1);
            DieIf(!LoadObject(senderDictIDref, &senderDict));
            DieIf(senderDict.oType != kTypeDict);
            
            // Look up value for key "ID", which is a UID pointing to the sender's account ID
            uint64_t senderNameIDref = ReturnValueRefForKeyName(&senderDict, "ID");
            DieIf(senderNameIDref == (uint64_t)-1);
            DieIf(!LoadObject(senderNameIDref, &senderNameID));
            DieIf(senderNameID.oType != kTypeUID);
            
            // Load dict with sender's account ID
            uint64_t senderNameRef = ReturnElemRef(&gObjectsArray, senderNameID.oInt);
            DieIf(senderNameRef == (uint64_t)-1);
            DieIf(!LoadObject(senderNameRef, &senderName));
            if (senderName.oType == kTypeDict)
            {
                // Look up value for key "NS.string"
                uint64_t senderNameStrRef = ReturnValueRefForKeyName(&senderName, "NS.string");
                DieIf(senderNameStrRef == (uint64_t)-1);
                DieIf(!LoadObject(senderNameStrRef, &senderNameStr));
                DieIf(senderNameStr.oType != kTypeStringASCII);
                
                // Save sender ID in ICMessage
                uint64_t nameLength = strlen(senderNameStr.oData);
                ICmsg->mSenderID = malloc(nameLength + 1); // freed with DeleteMessage()
                strncpy(ICmsg->mSenderID, senderNameStr.oData, nameLength);
                ICmsg->mSenderID[nameLength] = '\0';
            }
            else if (senderName.oType == kTypeStringASCII)
            {
                // Save sender ID in ICMessage
                uint64_t nameLength = strlen(senderName.oData);
                ICmsg->mSenderID = malloc(nameLength + 1); // freed with DeleteMessage()
                strncpy(ICmsg->mSenderID, senderName.oData, nameLength);
                ICmsg->mSenderID[nameLength] = '\0';
            }
            else if (senderName.oType == kTypeStringUnicode)
            {
                // I have not encountered this case in a chat log, so simply apply the approach used for Unicode participant names (see
                // comment under line "else if (participant.oType == kTypeStringUnicode)" above) and hope that it works
                ICmsg->mSenderID = calloc((senderName.oSize * 2) + 1, 1); // freed on program quit
                for (int b = 0; b < senderName.oSize * 2; b += 2)
                {
                    char *bytes = NULL;
                    ConvertUnicodeToUTF8((ICmsg->mSenderID + b), &bytes);
                    if (strlen(bytes) == 1)
                        strcat(ICmsg->mSenderID, bytes);
                }
                // If all of the text was Unicode characters, we have an empty string on our hands, so put something in it
                if (strlen(ICmsg->mSenderID) == 0)
                {
                    free(ICmsg->mSenderID);
                    asprintf(&ICmsg->mSenderID, "%s", "<Unicode>");
                }
            }
            else DieIf(true);
        }
    }
    
    /* Retrieve and save message timestamp */
    BPObject timeDictID, timeDict, time;
    
    // Look up value for key "Time", which is a UID pointing to a dict with the timestamp
    uint64_t timeDictIDref = ReturnValueRefForKeyName(BPmsg, "Time");
    DieIf(timeDictIDref == (uint64_t)-1);
    DieIf(!LoadObject(timeDictIDref, &timeDictID));
    DieIf(timeDictID.oType != kTypeUID);
    
    // Look up dict containing the timestamp
    uint64_t timeDictRef = ReturnElemRef(&gObjectsArray, timeDictID.oInt);
    DieIf(timeDictRef == (uint64_t)-1);
    DieIf(!LoadObject(timeDictRef, &timeDict));
    DieIf(timeDict.oType != kTypeDict);
    
    // Convert NSTime to a string and save in ICMessage
    uint64_t timeRef = ReturnValueRefForKeyName(&timeDict, "NS.time");
    DieIf(timeRef == (uint64_t)-1);
    DieIf(!LoadObject(timeRef, &time));
    DieIf(time.oType != kTypeReal);
    if (firstMsg) // save timestamp in long format for header of converted chat log
        ConvertNSDate(time.oReal, &gFirstMsgTime, kDateSaveLong);
    ConvertNSDate(time.oReal, &(ICmsg->mTime), kDateSaveShort);
    
    /* Prepare to look up message text by loading "MessageText" dict */
    BPObject msgTextID, msgText;
    
    // Look up value for key "MessageText", which is a UID pointing to a dict containing a dict of message attributes
    uint64_t msgTextIDref = ReturnValueRefForKeyName(BPmsg, "MessageText");
    DieIf(msgTextIDref == (uint64_t)-1);
    DieIf(!LoadObject(msgTextIDref, &msgTextID));
    DieIf(msgTextID.oType != kTypeUID);
    
    // Follow UID to dict containing the dict containing the message attributes
    uint64_t msgTextRef = ReturnElemRef(&gObjectsArray, msgTextID.oInt);
    DieIf(msgTextRef == (uint64_t)-1);
    DieIf(!LoadObject(msgTextRef, &msgText));
    DieIf(msgText.oType != kTypeDict);
    
    // Determine if this is a chat message or file transfer message by looking for key "OriginalMessage". If we find it, this is a
    // regular text message.
    bool isText = (ReturnValueRefForKeyName(BPmsg, "OriginalMessage") != -1);
    if (isText)
    {
        /* Get text of message */
        BPObject stringDictID, stringDict, string;
        
        // Look up value for key "NSString", which is a UID pointing to a dict that contains the actual string
        uint64_t stringDictIDref = ReturnValueRefForKeyName(&msgText, "NSString");
        DieIf(stringDictIDref == (uint64_t)-1);
        DieIf(!LoadObject(stringDictIDref, &stringDictID));
        DieIf(stringDictID.oType != kTypeUID);
        
        // Follow UID to dict containing the string
        uint64_t stringDictRef = ReturnElemRef(&gObjectsArray, stringDictID.oInt);
        DieIf(stringDictRef == (uint64_t)-1);
        DieIf(!LoadObject(stringDictRef, &stringDict));
        
        // Look up value for key "NS.string", which is a UID pointing to the message text
        uint64_t stringRef = ReturnValueRefForKeyName(&stringDict, "NS.string");
        DieIf(stringRef == (uint64_t)-1);
        DieIf(!LoadObject(stringRef, &string));
        
        // If message was stored in plain ASCII, simply copy the string into ICMessage
        if (string.oType == kTypeStringASCII)
        {
            // If this is a client message that says that "%@" is now on/offline, replace "%@" with subject name gotten earlier
            if (isClient && !strcmp(string.oData, "%@ is now online."))
                asprintf(&ICmsg->mText, "%s is now online.", subject); // freed in either DeleteMessage() or ConvertMessageToRTF()
            else if (isClient && !strcmp(string.oData, "%@ is now offline."))
                asprintf(&ICmsg->mText, "%s is now offline.", subject); // freed in either DeleteMessage() or ConvertMessageToRTF()
            else
            {
                uint64_t msgLength = strlen(string.oData);
                ICmsg->mText = malloc(msgLength + 1); // freed in either DeleteMessage() or ConvertMessageToRTF()
                strncpy(ICmsg->mText, string.oData, msgLength);
                ICmsg->mText[msgLength] = '\0';
            }
        }
        // Otherwise, copy Unicode as memory block rather than normal string since it can have null bytes
        else if (string.oType == kTypeStringUnicode)
        {
            ICmsg->mWideStrSize = string.oSize;
            uint64_t msgLength = string.oSize * 2; // "oSize" is the number of wide chars
            ICmsg->mText = malloc(msgLength + 1); // freed in either DeleteMessage() or ConvertMessageToRTF()
            memcpy(ICmsg->mText, string.oData, msgLength);
            ICmsg->mText[msgLength] = '\0';
        }
        else DieIf(true);
    }
    else
    {
        /* Load name of file being transferred */
        BPObject attribID, attrib, msgKeys, msgValues, attribObjects, attribObjID, attribObj, fileNameID, fileName;
        
        // Look up value for key "MessageText", which is a UID pointing to a dict containing a dict of message attributes
        uint64_t textIDref = ReturnValueRefForKeyName(BPmsg, "MessageText");
        DieIf(textIDref == (uint64_t)-1);
        DieIf(!LoadObject(textIDref, &msgTextID));
        DieIf(msgTextID.oType != kTypeUID);
        
        // Follow UID to dict containing the dict containing the message attributes
        uint64_t textRef = ReturnElemRef(&gObjectsArray, msgTextID.oInt);
        DieIf(textRef == (uint64_t)-1);
        DieIf(!LoadObject(textRef, &msgText));
        DieIf(msgText.oType != kTypeDict);
        
        // Look for NSAttributeInfo. If present, multiple files are being sent with this one message.
        bool isMultipleFiles = (ReturnValueRefForKeyName(&msgText, "NSAttributeInfo") != -1);
        
        // Look up value for key "NSAttributes", which is a UID pointing to a dict containing message attributes
        uint64_t attribIDref = ReturnValueRefForKeyName(&msgText, "NSAttributes");
        if (attribIDref == (uint64_t)-1) // this means there will be no message text, so there's no harm in skipping it
        {
            printf("Warning: SMS hiccup detected; message skipped.\n");
            ICmsg->mHiccup = true;
            return true;
        }
        DieIf(!LoadObject(attribIDref, &attribID));
        DieIf(attribID.oType != kTypeUID);
        
        // Follow UID to dict containing the message attributes
        uint64_t attribRef = ReturnElemRef(&gObjectsArray, attribID.oInt);
        DieIf(attribRef == (uint64_t)-1);
        DieIf(!LoadObject(attribRef, &attrib));
        DieIf(attrib.oType != kTypeDict);
        
        // Set number of files that are being transferred in this message and look up relevant element in NSAttributes
        if (isMultipleFiles)
        {
            // Look up "NS.objects" in the "NSAttributes" dict; this is an array of dicts with the properties of each file
            uint64_t attribObjectsRef = ReturnValueRefForKeyName(&attrib, "NS.objects");
            DieIf(attribObjectsRef == (uint64_t)-1);
            DieIf(!LoadObject(attribObjectsRef, &attribObjects));
            DieIf(attribObjects.oType != kTypeArray);
            
            ICmsg->mFileTransfer = attribObjects.oSize; // number of items in array is number of files
        }
        else
        {
            // Look up "NS.keys" in the "NSAttributes" dict; these are the properties of the single file being transferred
            uint64_t attribKeysRef = ReturnValueRefForKeyName(&attrib, "NS.keys");
            DieIf(attribKeysRef == (uint64_t)-1);
            DieIf(!LoadObject(attribKeysRef, &msgKeys));
            DieIf(msgKeys.oType != kTypeArray);
            
            // Load "NS.objects" array that corresponds to "NS.keys"
            uint64_t attribValuesRef = ReturnValueRefForKeyName(&attrib, "NS.objects");
            DieIf(attribValuesRef == (uint64_t)-1);
            DieIf(!LoadObject(attribValuesRef, &msgValues));
            DieIf(msgValues.oType != kTypeArray);
            
            ICmsg->mFileTransfer = 1;
        }
        
        // Load all file names into "mText"
        for (uint64_t a = 0; a < ICmsg->mFileTransfer; a++)
        {
            // Look up next file's properties if there is more than one file
            if (isMultipleFiles)
            {
                // Load UID that points to a file element in array
                uint64_t attribObjIDref = ReturnElemRef(&attribObjects, a);
                DieIf(attribObjIDref == (uint64_t)-1);
                DieIf(!LoadObject(attribObjIDref, &attribObjID));
                DieIf(attribObjID.oType != kTypeUID);
                
                // Follow UID to file element
                uint64_t attribObjRef = ReturnElemRef(&gObjectsArray, attribObjID.oInt);
                DieIf(attribObjRef == (uint64_t)-1);
                DieIf(!LoadObject(attribObjRef, &attribObj));
                DieIf(attribObj.oType != kTypeDict);
                
                // Look up "NS.keys" in element's dict; these are the properties of the file
                uint64_t attribObjKeysRef = ReturnValueRefForKeyName(&attribObj, "NS.keys");
                DieIf(attribObjKeysRef == (uint64_t)-1);
                DieIf(!LoadObject(attribObjKeysRef, &msgKeys));
                DieIf(msgKeys.oType != kTypeArray);
                
                // Load "NS.objects" array that corresponds to "NS.keys"
                uint64_t attribObjValuesRef = ReturnValueRefForKeyName(&attribObj, "NS.objects");
                DieIf(attribObjValuesRef == (uint64_t)-1);
                DieIf(!LoadObject(attribObjValuesRef, &msgValues));
                DieIf(msgValues.oType != kTypeArray);
            }
            
            // Search "NS.keys" array for key called "__kIMFilenameAttributeName"
            int nameIndex = -1;
            for (int b = 0; b < msgKeys.oSize; b++)
            {
                BPObject msgKeyID, msgKey;
                
                // Load UID that points to name of this key
                uint64_t msgKeyIDref = ReturnElemRef(&msgKeys, (uint64_t)b);
                DieIf(msgKeyIDref == (uint64_t)-1);
                DieIf(!LoadObject(msgKeyIDref, &msgKeyID));
                DieIf(msgKeyID.oType != kTypeUID);
                
                // Load name of this key
                uint64_t msgKeyRef = ReturnElemRef(&gObjectsArray, msgKeyID.oInt);
                DieIf(msgKeyRef == (uint64_t)-1);
                DieIf(!LoadObject(msgKeyRef, &msgKey));
                DieIf(msgKey.oType != kTypeStringASCII);
                
                if (!strcmp(msgKey.oData, "__kIMFilenameAttributeName"))
                    nameIndex = b;
            }
            DieIf(nameIndex == -1);
            
            // Load UID in "NS.objects" that points to the object that represents the value corresponding to the
            // "__kIMFilenameAttributeName" key
            uint64_t fileNameIDref = ReturnElemRef(&msgValues, (uint64_t)nameIndex);
            DieIf(fileNameIDref == (uint64_t)-1);
            DieIf(!LoadObject(fileNameIDref, &fileNameID));
            DieIf(fileNameID.oType != kTypeUID);
            
            // Follow this UID to the actual file name
            uint64_t fileNameRef = ReturnElemRef(&gObjectsArray, fileNameID.oInt);
            DieIf(fileNameRef == (uint64_t)-1);
            DieIf(!LoadObject(fileNameRef, &fileName));
            DieIf(fileName.oType != kTypeStringASCII);
            
            // Copy the file name into ICMessage
            if (isMultipleFiles)
            {
                // If there is already at least one file name in "mText", add ", " onto end of that string and append this file name
                if (ICmsg->mText != NULL)
                {
                    char *prevStr = NULL;
                    asprintf(&prevStr, "%s", ICmsg->mText); // freed below
                    free(ICmsg->mText);
                    asprintf(&ICmsg->mText, "%s, %s", prevStr, fileName.oData); // freed in either DeleteMessage() or ConvertMessageToRTF()
                    free(prevStr);
                }
                else
                    asprintf(&ICmsg->mText, "%s", fileName.oData);
            }
            else
            {
                uint64_t fileStrLength = strlen(fileName.oData);
                ICmsg->mText = malloc(fileStrLength + 1); // freed in either DeleteMessage() or ConvertMessageToRTF()
                strncpy(ICmsg->mText, fileName.oData, fileStrLength);
                ICmsg->mText[fileStrLength] = '\0';
            }
        }
    }
    free(subject);
    return true;
#undef DieIf
}

// Print the contents of "msg"
void PrintMessage(ICMessage *msg)
{
    if (msg->mHiccup)
    {
        printf("Message was deemed to be SMS hiccup and was skipped.");
        return;
    }
    
    if (msg->mFileTransfer > 0)
    {
        if (msg->mFileTransfer == 1)
            printf("%s %s sent file %s.\n", msg->mTime, msg->mSenderID, msg->mText);
        else
            printf("%s %s sent %llu files: %s.\n", msg->mTime, msg->mSenderID, msg->mFileTransfer, msg->mText);
        return;
    }
    
    if (msg->mFromClient)
    {
        printf("%s %s:\n   %s\n", msg->mTime, gClientName, msg->mText);
        return;
    }
    
    printf("%s %s said:\n   ", msg->mTime, msg->mSenderID);
    if (msg->mWideStrSize == 0)
        printf("%s\n", msg->mText);
    else
        PrintWideString(msg->mText, msg->mWideStrSize);
}

// Write message to disk in RTF
void ConvertMessageToRTF(ICMessage *msg)
{
    // Do nothing for an SMS hiccup
    if (msg->mHiccup)
        return;
    
    // If this message is coming from the chat client, print timestamp and then name of client in bold
    if (msg->mFromClient)
    {
        char *clientMsg = NULL;
        asprintf(&clientMsg, "\\cf1 %s \\cf0 \\b1 %s\\b0 ", msg->mTime, gClientName); // freed below
        WriteToOutFile(clientMsg);
        free(clientMsg);
    }
    else
    {
        // Write timestamp of message in gray
        char *time = NULL;
        asprintf(&time, "\\cf1 %s ", msg->mTime); // freed below
        WriteToOutFile(time);
        free(time);
        
        // Print out sender name
        WriteSenderName(msg, true);
    }
    
    if (msg->mFileTransfer > 0)
    {
        // Simply write name of file transferred. End the italics tag started in WriteSenderName().
        char *fileMsg = NULL;
        if (msg->mFileTransfer == 1)
            asprintf(&fileMsg, "\\cf0  sent file %s.\\i0 \n", msg->mText); // freed below
        else
            asprintf(&fileMsg, "\\cf0  sent %llu files: %s.\\i0 \n", msg->mFileTransfer, msg->mText); // freed below
        WriteToOutFile(fileMsg);
        free(fileMsg);
    }
    else
    {
        // Prepare to write message in black
        WriteToOutFile("\\cf0 : ");
        
        // Since RTF uses curly braces and backslashes as part of its markup, we need to escape any that are part of the message.
        // Newlines in the message also need to be escaped to display as such in RTF. This is the code for ASCII strings; Unicode
        // strings are escaped during file-write below.
        if (msg->mWideStrSize == 0)
        {
            // If we find something that needs escaping, allocate the biggest string we could need (2x current string size) and then
            // scan through message, escaping all applicable characters
            if (strchr(msg->mText, '{') || strchr(msg->mText, '}') || strchr(msg->mText, '\\') || strchr(msg->mText, 0x0A))
            {
                uint64_t strLen = strlen(msg->mText);
                char *newStr = calloc((strLen * 2) + 1, 1); // freed with DeleteMessage()
                strncpy(newStr, msg->mText, strLen);
                char *reader = newStr;
                do
                {
                    // Move string right to open up a place for the backslash, insert it, then skip past escaped character
                    if (*reader == '{' || *reader == '}' || *reader == '\\' || *reader == 0x0A)
                    {
                        memmove(reader + 1, reader, strlen(reader));
                        *reader = '\\';
                        reader++;
                    }
                    reader++;
                }
                while (*reader != '\0');
                free(msg->mText);
                msg->mText = newStr;
            }
        }
        
        // Write message as plain-text if it's regular ASCII, otherwise convert Unicode hex value to RTF Unicode markup
        if (msg->mWideStrSize == 0)
            WriteToOutFile(msg->mText);
        else
        {
            for (int byte = 0; byte < msg->mWideStrSize * 2; byte += 2)
            {
                int wc = (*(char *)(msg->mText + byte) << 8) + *(char *)(msg->mText + byte + 1);
                
                // If this 2-byte character falls within the standard ASCII range, e.g. 0x0061, write it as just that ASCII byte
                if (wc <= 127)
                {
                    // Escape curly braces and backslashes to avoid breaking RTF markup. Escape newlines to make them display as such.
                    if (wc == '{' || wc == '}' || wc == '\\' || wc == 0x0A)
                    {
                        char escStr[2] = {'\\', 0};
                        WriteToOutFile(escStr);
                    }
                    char byteStr[2] = {(char)wc, 0};
                    WriteToOutFile(byteStr);
                }
                // Otherwise convert the hex value to RTF's decimal Unicode markup, e.g. 0x2019 => "\uc0\u8217 "
                else
                {
                    char *bytes = NULL;
                    asprintf(&bytes, "\\uc0\\u%d ", wc); // freed below
                    WriteToOutFile(bytes);
                    free(bytes);
                }
            }
            WriteToOutFile("\n");
        }
    }
    WriteToOutFile("\\\n");
}

// Write message to disk in plain-text format
void ConvertMessageToTXT(ICMessage *msg)
{
    // Do nothing for an SMS hiccup
    if (msg->mHiccup)
        return;
    
    // If this message is coming from the chat client, print timestamp and then name of client in bold
    if (msg->mFromClient)
    {
        char *clientMsg = NULL;
        asprintf(&clientMsg, "%s %s ", msg->mTime, gClientName); // freed below
        WriteToOutFile(clientMsg);
        free(clientMsg);
    }
    else
    {
        // Write timestamp of message in gray
        char *time = NULL;
        asprintf(&time, "%s ", msg->mTime); // freed below
        WriteToOutFile(time);
        free(time);
        
        // Print out sender name
        WriteSenderName(msg, false);
    }
    
    if (msg->mFileTransfer > 0)
    {
        // Simply write name of file transferred
        char *fileMsg = NULL;
        if (msg->mFileTransfer == 1)
            asprintf(&fileMsg, " sent file %s.\n", msg->mText); // freed below
        else
            asprintf(&fileMsg, " sent %llu files: %s.\n", msg->mFileTransfer, msg->mText); // freed below
        WriteToOutFile(fileMsg);
        free(fileMsg);
    }
    else
    {
        // Prepare to write message
        WriteToOutFile(": ");
        
        // Write message as plain-text if it's regular ASCII, otherwise convert each 16-bit character to UTF-8
        if (msg->mWideStrSize == 0)
        {
            WriteToOutFile(msg->mText);
            WriteToOutFile("\n");
        }
        else
        {
            for (int a = 0; a < msg->mWideStrSize * 2; a += 2)
            {
                char *bytes = NULL;
                ConvertUnicodeToUTF8((msg->mText + a), &bytes);
                WriteToOutFile(bytes);
            }
            WriteToOutFile("\n");
        }
    }
}

// Release memory allocated for message
void DeleteMessage(ICMessage *msg)
{
    free(msg->mSenderID);
    msg->mSenderID = NULL;
    free(msg->mTime);
    msg->mTime = NULL;
    free(msg->mText);
    msg->mText = NULL;
}

// Return the ID (offset table index) for the message in gMessageListArray at position "msgNum"
uint64_t ReturnMessageRef(uint64_t msgNum)
{
    BPObject msgID_ID;
    uint64_t msgID_IDref = ReturnElemRef(&gMessageListArray, (uint64_t)msgNum);
    if (msgID_IDref == (uint64_t)-1)
        return (uint64_t)-1;
    if (!LoadObject(msgID_IDref, &msgID_ID))
        return (uint64_t)-1;
    if (msgID_ID.oType != kTypeUID)
        return false;
    uint64_t msgIDref = ReturnElemRef(&gObjectsArray, msgID_ID.oInt);
    if (msgIDref == (uint64_t)-1)
        return (uint64_t)-1;
    
    return msgIDref;
}
#pragma mark Utility functions
// Takes the 16-bit Unicode character passed in and returns a UTF-8 string of up to 4 characters
void ConvertUnicodeToUTF8(char *unicodeStr, char **utf8Str)
{
    int wc = (*(char *)unicodeStr << 8) + *(char *)(unicodeStr + 1);
    *utf8Str = calloc(5, 1); // freed with DeleteMessage()
    char *byte = *utf8Str;
    if (wc < 0x80) // 7 bits or less, so we have a standard ASCII byte; just save it
        *byte = (char)wc;
    else if (wc < 0x800) // no more than 11 bits, so we can fit the Unicode into two bytes of 5 + 6 bits
    {
        *byte++ = (char)(0xC0 + (wc >> 6)); // add b110xxxxx to upper five bits
        *byte = (char)(0x80 + (wc & 0x3F)); // add b10xxxxxx to lower six bits
    }
    else if ((unsigned)wc - 0xd800u < 0x800) // falls in forbidden range
        printf("Error: Failed to convert a Unicode character: forbidden range.\n");
    else if (wc < 0x10000) // no more than 16 bits, so we can fit it in 4 + 6 + 6 bits
    {
        *byte++ = (char)(0xE0 + (wc >> 12));       // add b1110xxxx to upper four bits
        *byte++ = (char)0x80 + ((wc >> 6) & 0x3F); // add b10xxxxxx to next six bits
        *byte = (char)(0x80 + (wc & 0x3F));        // add b10xxxxxx to final six bits
    }
    else if (wc < 0x110000) // no more than 20 bits, so it is not too large (Unicode stops at 21 bits, 3 + 6 + 6 + 6)
    {
        *byte++ = (char)(0xF0 + (wc >> 18));          // add b11110xxx to upper three bits
        *byte++ = (char)(0x80 + ((wc >> 12) & 0x3F)); // add b10xxxxxx to next six bits
        *byte++ = (char)(0x80 + ((wc >> 6) & 0x3F));  // add b10xxxxxx to next six bits
        *byte = (char)(0x80 + (wc >> 6));             // add b10xxxxxx to final six bits
    }
    else
        printf("Error: Failed to convert a Unicode character: out of range.\n");
}

// Write sender account ID or real name to disk, and trim ID if requested by user
void WriteSenderName(ICMessage *msg, bool useRTF)
{
    char *nameToUse = NULL;
    char *senderCompareCopy = NULL;
    bool lookupSuccess = false;
    
    // Make copy of sender name for this comparison, then adjust it for known differences in how sender name can be stored in the
    // "Participants" array versus the message metadata
    asprintf(&senderCompareCopy, "%s", msg->mSenderID); // freed at end of function
    
    // "e:user@domain.com" in a message might be stored as "e:user" in "Participants"
    char *atMarkPosition = strchr(senderCompareCopy, '@');
    if (atMarkPosition != NULL)
        *atMarkPosition = '\0';
    
    // "+15551235555" in a message might be stored as "15551235555" in "Participants"
    if (*senderCompareCopy == '+')
    {
        uint64_t copySize = strlen(senderCompareCopy);
        memmove(senderCompareCopy, senderCompareCopy + 1, copySize + 1);
    }
    
    // Look for this account ID in our preloaded array of IDs. If we are using "real names", this will give us the location of said
    // real name. If we are simply using account ID, then we still need to know the location of this ID in our master array so we can
    // pick the appropriate color below.
    int nameIndex = -1;
    for (int a = 0; a < gNumParticipantIDs; a++)
    {
        // Try message's sender ID against a raw participant ID and also our massaged version of it
        if (!strcmp(gParticipantIDs[a], msg->mSenderID) || !strcmp(gParticipantIDs[a], senderCompareCopy))
        {
            nameIndex = a;
            break;
        }
    }
    if (nameIndex == -1)
        printf("Warning: The sender ID on this message, %s, did not match a known participant ID.\n", msg->mSenderID);
    
    // If "real names" were requested, see if we have one for this sender ID
    if (gUseRealNames)
    {
        if (nameIndex > gNumParticipantNames)
            printf("Error: There is no corresponding real name for sender with ID '%s' at index %d. Falling back to account ID.\n", msg->mSenderID, nameIndex);
        else if (gParticipantNames[nameIndex] == NULL)
            printf("Error: Attempted to look up real name of sender '%s' at index %d, but it was missing. Falling back to account ID.\n", msg->mSenderID, nameIndex);
        else
            lookupSuccess = true;
    }
    
    // Retrieve "real name" if it exists
    if (lookupSuccess) // automatically "false" if gUseRealNames is "false"
    {
        uint64_t nameLength = strlen(gParticipantNames[nameIndex]);
        nameToUse = malloc(nameLength + 1); // freed at end of function
        strncpy(nameToUse, gParticipantNames[nameIndex], nameLength);
        nameToUse[nameLength] = '\0';
    }
    
    // If "real name" doesn't exist or we are using account ID, prepare account ID for writing to disk
    if (!gUseRealNames || !lookupSuccess)
    {
        uint64_t IDlength = strlen(msg->mSenderID);
        char *IDstart = msg->mSenderID;
        
        // Adjust string start/end if trimming was requested
        if (gTrimEmailIDs)
        {
            // Start string after 'e:'
            char *colonPosition = strchr(msg->mSenderID, ':');
            if (colonPosition != NULL)
                IDstart = colonPosition + 1;
            
            // End string at '@'
            char *atMarkPosition2 = strchr(msg->mSenderID, '@');
            if (atMarkPosition2 != NULL)
                IDlength -= (uint32_t)((IDstart + IDlength) - atMarkPosition2);
        }
        
        // Retrieve trimmed account ID
        nameToUse = malloc(IDlength + 1); // freed at end of function
        strncpy(nameToUse, IDstart, IDlength);
        nameToUse[IDlength] = '\0';
    }
    
    if (useRTF)
    {
        // For sender name, use colors 2 through 6 in our table depending on position in gParticipantIDs. Use black if we couldn't
        // find this participant in our list of known IDs for some reason. Use italics if this is a file transfer (ending tag is in
        // ConvertMessageToRTF()).
        char *nameColor = NULL;
        if (nameIndex == -1)
            nameIndex = 0;
        else
            nameIndex = (nameIndex % 5) + 2;
        asprintf(&nameColor, "%s\\cf%d ", msg->mFileTransfer ? "\\i1 " : "", nameIndex); // freed below
        WriteToOutFile(nameColor);
        free(nameColor);
    }
    
    // Actually write sender name
    WriteToOutFile(nameToUse);
    
    free(nameToUse);
    free(senderCompareCopy);
}

// Starts RTF file with necessary header markup
void WriteRTFHeader(void)
{
    // Standard Mac text and font settings
    WriteToOutFile("{\\rtf1\\ansi\\ansicpg1252\\cocoartf1038\\cocoasubrtf360\n");
    WriteToOutFile("{\\fonttbl\\f0\\fswiss\\fcharset0 Helvetica;}\n");
    
    // Set up color table with black for message, gray for timestamp, then blue, green, orange, cyan and red for participant names;
    // these five colors are cycled through in the case of more than five participants
    WriteToOutFile("{\\colortbl\\red0\\green0\\blue0;\\red128\\green128\\blue128;\\red0\\green0\\blue128;\\red0\\green128\\blue0;");
    WriteToOutFile("\\red255\\green128\\blue0;\\red0\\green128\\blue128;\\red128\\green0\\blue0;}\n");
    
    // Typical margin and view settings (vieww/h is Mac-only)
    WriteToOutFile("\\margl1440\\margr1440\\vieww9000\\viewh8400\\viewkind0\n\n");
}

// Closes RTF markup at end of file
void WriteRTFFooter(void)
{
    WriteToOutFile("}");
}

// Write long-format timestamp at top of converted log
void WriteTimeHeader(bool useRTF)
{
    if (useRTF)
        WriteToOutFile("\\cf1 "); // gray
    WriteToOutFile("Chat window opened on ");
    WriteToOutFile(gFirstMsgTime);
    if (useRTF)
        WriteToOutFile(":\\\n");
    else
        WriteToOutFile(":\n");
}
