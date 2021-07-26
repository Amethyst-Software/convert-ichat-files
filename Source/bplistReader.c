//
//  bplistReader.c
//  Convert ichat Files
//
//  Created on 3/27/17.
//  Copyright © 2017 Amethyst Software (contact@amethystsoftware.com). All rights reserved.
//

#include <locale.h>  // setlocale()
#include <math.h>    // pow()
#include <stdbool.h> // bool
#include <stdio.h>   // fprintf()
#include <stdlib.h>  // malloc()
#include <string.h>  // strcpy()
#include "bplistReader.h"

#pragma mark Globals
// For reading binary plist header and trailer
const char *kMagicWord = "bplist";
const int   kMagicWordLength = 6;
const char *kVersion_bplist = "00";
const int   kVerLength = 2;
const int   kTrailerOffset = 26;
const int   kOffsetSizeOffset = 0;
const int   kParamSizeOffset = 1;
const int   kNumObjOffset = 2;
const int   kRootObjOffset = 10;
const int   kOffsetTableOffsetOffset = 18;

// For storing basic bplist information
uint64_t  gRefSize = 0;
uint64_t  gNumObj = 0;
uint64_t  gRootObjID = 0;
uint64_t *gOffsets = NULL;

// For formatting output
char *gUIDpad = NULL;         // formatting string for PrintObject() that will pad to the width of the largest UID
int   gIndent = 0;            // how far to indent objects in browsing mode based on file's hierarchy
bool  gPrintedSpaces = false; // used to prevent multiplied indentation when printing arrays and dicts

extern bool  gFollowRefs;
extern char *gInFileContents;
extern long  gInFileLength;

// Types of data that can be found in a bplist
BPObjectType gTypeTable[] =
{
    //otEnum     otHighQuad otLowQuad otSizeType      otReadFunc              otPrintFunc              otName
    {kTypeNone,           0,  0, kSizeNone,           NULL,                   NULL,                    ""},
    {kTypeNull,           0,  0, kSizeNone,           ReadData_Null,          PrintData_Null,          "null"},
    {kTypeBoolFalse,      0,  8, kSizeNone,           ReadData_BoolFalse,     PrintData_BoolFalse,     "boolean"},
    {kTypeBoolTrue,       0,  9, kSizeNone,           ReadData_BoolTrue,      PrintData_BoolTrue,      "boolean"},
    {kTypeFill,           0, 15, kSizeNone,           ReadData_Fill,          PrintData_Fill,          "fill"},
    {kTypeInt,            1, -1, kSizePowerOfTwo,     ReadData_Int,           PrintData_Int,           "int"},
    {kTypeReal,           2, -1, kSizePowerOfTwo,     ReadData_Real,          PrintData_Real,          "real"},
    {kTypeDate,           3,  3, kSize8ByteFloat,     ReadData_Date,          PrintData_Date,          "date"},
    {kTypeData,           4, -1, kSizeScalarOverflow, ReadData_Data,          PrintData_Data,          "data"},
    {kTypeStringASCII,    5, -1, kSizeScalarOverflow, ReadData_StringASCII,   PrintData_StringASCII,   "string (ASCII)"},
    {kTypeStringUnicode,  6, -1, kSizeScalarOverflow, ReadData_StringUnicode, PrintData_StringUnicode, "string (Unicode)"},
    {kTypeUID,            8, -1, kSizeAddOne,         ReadData_UID,           PrintData_UID,           "UID"},
    {kTypeArray,         10, -1, kSizeScalarOverflow, ReadData_Array,         PrintData_Array,         "array"},
    {kTypeSet,           12, -1, kSizeScalarOverflow, ReadData_Set,           PrintData_Set,           "set"},
    {kTypeDict,          13, -1, kSizeScalarOverflow, ReadData_Dict,          PrintData_Dict,          "dict"}
};
#pragma mark File-level functions
// Validate that this is a binary plist
bool Validate_bplist(void)
{
    // Sanity checks
    if (gInFileContents == NULL)
    {
        printf("Fatal error: File was not loaded.\n");
        return false;
    }
    if (gInFileLength < kMagicWordLength + kVerLength)
    {
        printf("Fatal error: File is not long enough to be a bplist.\n");
        return false;
    }
    
    // Look for magic word indicating bplist
    if (strncmp(gInFileContents, kMagicWord, kMagicWordLength))
    {
        printf("Fatal error: This is not a bplist file.\n");
        return false;
    }
    
    // Look for version number (only known version is "00")
    if (strncmp(gInFileContents + kMagicWordLength, kVersion_bplist, kVerLength))
    {
        printf("Fatal error: This is not a version %s bplist file, so I cannot read it.\n", kVersion_bplist);
        return false;
    }
    
    return true;
}

// Read a binary plist's trailer and offset table into memory
bool Load_bplist(void)
{
    uint64_t offsetTableOffset = 0, offsetSize = 0;
    
    // Read trailer data
    char *trailer = gInFileContents + gInFileLength - kTrailerOffset;
    offsetSize = (uint64_t)*(trailer + kOffsetSizeOffset);
    gRefSize = (uint64_t)*(trailer + kParamSizeOffset);
    gNumObj = ReadUInt_8Byte(trailer + kNumObjOffset);
    gRootObjID = ReadUInt_8Byte(trailer + kRootObjOffset);
    offsetTableOffset = ReadUInt_8Byte(trailer + kOffsetTableOffsetOffset);
    
    // Sanity checks
    if (!gNumObj)
    {
        printf("Fatal error: Found no objects in bplist!\n");
        return false;
    }
    if (gRootObjID > gNumObj)
    {
        printf("Fatal error: Root object ID is higher than number of objects in bplist!\n");
        return false;
    }
    
    // Read all offsets into memory for future reference
    gOffsets = malloc(gNumObj * sizeof(uint64_t)); // freed on program quit
    char *offsetReader = gInFileContents + offsetTableOffset;
    for (int a = 0; a < gNumObj; a++)
    {
        gOffsets[a] = ReadUInt_XByte(offsetReader, offsetSize);
        offsetReader += offsetSize;
    }
    
    // Find how many digits the largest UID is and use this to set up our padding string for PrintObject()
    uint64_t highestUID = gNumObj;
    int UIDmag = 1;
    do
    {
        highestUID /= 10;
        UIDmag++;
    }
    while (highestUID >= 10);
    asprintf(&gUIDpad, "%%0%dllu:", UIDmag); // freed on program quit
    
    return true;
}

// Allow user to browse bplist interactively
void Browse_bplistElements(void)
{
    // Start off by printing the root object
    printf("Printing root object:\n");
    BPObject o;
    if (!LoadObject(gRootObjID, &o))
        return;
    PrintObject(&o);
    
    // Enter interactive browsing mode
    char input[10];
    uint64_t inputNum = 0;
    do
    {
        int inputted = 0;
        printf("Type any letter to exit, or enter the number [0-%llu] of the element in the offset table to print:\n", gNumObj - 1);
        if (fgets(input, 10, stdin) != NULL)
            inputted = sscanf(input, "%llu", &inputNum);
        
        if (inputted == 0)
        {
            printf("Goodbye!\n");
            break;
        }
        
        if (inputNum >= gNumObj)
        {
            printf("Error: Input %lld out of range. Try again.\n", inputNum);
            continue;
        }
        
        if (!LoadObject(inputNum, &o))
            return;
        PrintObject(&o);
    }
    while (true);
}
#pragma mark Object management
// Proceed through the five stages of loading an object's data from the bplist into memory
bool LoadObject(uint64_t objNum, BPObject *obj)
{
    if (!LoadObject_S1_Init(objNum, obj))
        return false;
    
    if (!LoadObject_S2_Locate(obj))
        return false;
    
    if (!LoadObject_S3_GetType(obj))
        return false;
        
    if (!LoadObject_S4_ReadSize(obj))
        return false;
    
    if (!LoadObject_S5_ReadData(obj))
        return false;
    
    return true;
}

// Initialize the elements of a bplist object, setting its UID to "objNum"
bool LoadObject_S1_Init(uint64_t objNum, BPObject *obj)
{
    if (obj == NULL)
    {
        printf("Error: LoadObject_S1_Init() was passed a NULL object!\n");
        return false;
    }
    
    obj->oUID = objNum;
    obj->oObjAddress = NULL;
    obj->oDataAddress = NULL;
    obj->oType = kTypeNone;
    obj->oSize = (uint64_t)-1;
    obj->oBool = false;
    obj->oInt = 0;
    obj->oReal = 0;
    obj->oData = NULL;
    obj->oIsBaseWritingDirection = false;
    obj->oIsNSTime = false;
    
    return true;
}

// Get location in file of object with UID "oUID" and save it in "oObjAddress"
bool LoadObject_S2_Locate(BPObject *obj)
{
    if (gInFileContents == NULL)
    {
        printf("Error: Asked to get pointer to object before file was loaded!\n");
        return false;
    }
    
    if (obj->oUID < 0)
    {
        printf("Error: LoadObject_S2_Locate() was passed an object with a negative UID!\n");
        return false;
    }
    
    if (obj->oUID > gNumObj - 1)
    {
        printf("Error: Asked to get pointer to object %llu, which does not exist!\n", obj->oUID);
        return false;
    }
    
    uint64_t offset = gOffsets[obj->oUID];
    char *ptr = (gInFileContents + offset);
    obj->oObjAddress = ptr;
    
    return true;
}

// Sets type of "obj" by reading data at "oObjAddress", using -1 if the object cannot be identified
bool LoadObject_S3_GetType(BPObject *obj)
{
    if (obj->oObjAddress == NULL)
    {
        printf("Error: LoadObject_S3_GetType() was given an object without its address set.\n");
        return false;
    }
    
    int oType = -1;
    uint16_t hiQuad = (*obj->oObjAddress & 0xF0) >> 4;
    uint16_t loQuad = *obj->oObjAddress & 0x0F;
    
    // Get type of object by looking for our known quadbit codes
    for (int a = 1; a < kTypeCount; a++) // skip element 0, the "none" type
    {
        if (hiQuad == gTypeTable[a].otHighQuad)
        {
            if (gTypeTable[a].otLowQuad != -1) // if we are supposed to match the low quad too, make sure that matches
            {
                if (loQuad == gTypeTable[a].otLowQuad)
                {
                    oType = gTypeTable[a].otEnum;
                    break;
                }
                else
                    continue;
            }
            // If we're still here, then that means we didn't need to match the low quad, so we're done
            oType = gTypeTable[a].otEnum;
            break;
        }
    }
    
    if (oType == -1)
        printf("LoadObject_S3_GetType() was unable to identify the object with type code byte %02x.\n", *(obj->oObjAddress));
    
    obj->oType = oType;
    return true;
}

// Sets the size of "obj" (measured in whatever units this type uses — number of bytes, objects, etc.)
bool LoadObject_S4_ReadSize(BPObject *obj)
{
    if (obj->oType <= kTypeNone)
    {
        printf("Error: LoadObject_S4_ReadSize() was given an object without its type set.\n");
        return false;
    }
    
    uint64_t payloadSize = 0, overflow = 0;
    uint16_t loQuad = (*obj->oObjAddress) & 0x0F;
    
    if (gTypeTable[obj->oType].otSizeType == kSizePowerOfTwo)
        payloadSize = (uint64_t)pow(2, loQuad);
    else if (gTypeTable[obj->oType].otSizeType == kSize8ByteFloat)
        payloadSize = 8;
    else if (gTypeTable[obj->oType].otSizeType == kSizeScalarOverflow)
    {
        payloadSize = loQuad;
        if (payloadSize == 0xF) // then length is in subsequent scalar int
        {
            // The low quad of the next byte is the size of the data as a power of 2
            uint16_t loScalarQuad = (*(obj->oObjAddress + 1)) & 0x0F;
            uint64_t payloadSizeScalar = (uint64_t)pow(2, loScalarQuad);
            payloadSize = ReadUInt_XByte(obj->oObjAddress + 2, payloadSizeScalar);
            overflow = payloadSizeScalar + 1;
        }
    }
    else if (gTypeTable[obj->oType].otSizeType == kSizeAddOne)
        payloadSize = loQuad + 1;
    else if (gTypeTable[obj->oType].otSizeType != kSizeNone)
    {
        printf("Error: LoadObject_S4_ReadSize() encountered an unknown size code.\n");
        return false;
    }
    
    obj->oSize = payloadSize;
    obj->oDataAddress = obj->oObjAddress + 1 + overflow;
    return true;
}

// Calls the data type's designated read function to load its data into memory
bool LoadObject_S5_ReadData(BPObject *obj)
{
    if (obj->oSize == (uint64_t)-1)
    {
        printf("Error: LoadObject_S5_ReadData() was passed an object without its size set.\n");
        return false;
    }
    
    int oType = obj->oType;
    if (oType >= kTypeCount)
    {
        printf("Error: LoadObject_S5_ReadData() was passed an object with an unknown type.\n");
        return false;
    }
    
    if (gTypeTable[oType].otReadFunc != NULL)
        gTypeTable[oType].otReadFunc(obj);
    else
    {
        printf("Error: LoadObject_S5_ReadData() could not find the read function for this object's data type.\n");
        return false;
    }
    
    return true;
}

// Copies special iChat-related formatting cues from key object to value object
void CopyObjectMetadata(BPObject *objSrc, BPObject *objDest)
{
    objDest->oIsBaseWritingDirection = objSrc->oIsBaseWritingDirection;
    objDest->oIsNSTime = objSrc->oIsNSTime;
}

// Calls the data type's designated print function
void PrintObject(BPObject *obj)
{
    if (obj->oSize == (uint64_t)-1)
    {
        printf("Error: PrintObject() was passed an object that was not finished loading.\n");
        return;
    }
    
    int oType = obj->oType;
    if (oType >= kTypeCount)
    {
        printf("Error: PrintObject() was passed an object with an unknown type.\n");
        return;
    }
    
    if (gTypeTable[oType].otPrintFunc != NULL)
    {
        gPrintedSpaces = false;
        printf(gUIDpad, obj->oUID);
        gTypeTable[oType].otPrintFunc(obj);
    }
    else
        printf("Error: PrintObject() could not find the print function for this object's data type.\n");
}
#pragma mark Data-reading functions
// Simply identifies the current byte as a null value
void ReadData_Null(BPObject *obj)
{
    obj->oBool = false;
}

// Simply identifies the current byte as a boolean that is false
void ReadData_BoolFalse(BPObject *obj)
{
    obj->oBool = false;
}

// Simply identifies the current byte as a boolean that is true
void ReadData_BoolTrue(BPObject *obj)
{
    obj->oBool = true;
}

// Simply identifies these bytes as filler
void ReadData_Fill(BPObject *obj)
{
    obj->oBool = false;
}

// Saves 'int' data into "obj"
void ReadData_Int(BPObject *obj)
{
    obj->oInt = ReadUInt_XByte(obj->oDataAddress, obj->oSize);
}

// Saves 'double' data in "obj", assuming it is four or eight bytes
void ReadData_Real(BPObject *obj)
{
    uint64_t size = obj->oSize;
    
    if (size != 4 && size != 8)
    {
        printf("Error: %llu-byte 'real's cannot be read.\n", size);
        return;
    }
    
    uint8_t *floatData = malloc(size); // freed below
    char *reader;
    for (int a = 0; a < size; a++)
    {
        reader = obj->oDataAddress + obj->oSize - 1 - a;
        floatData[a] = (uint8_t)*reader;
    }
    double real = *(double *)floatData;
    obj->oReal = real;
    free(floatData);
}

// Saves an NSDate into "obj". This function merely reads the underlying 'float' into memory, to be passed later to ConvertNSDate().
void ReadData_Date(BPObject *obj)
{
    uint64_t size = obj->oSize;
    
    if (size != 4 && size != 8)
    {
        printf("Error: %llu-byte 'date's cannot be read.\n", size);
        return;
    }
    
    uint8_t *floatData = malloc(size); // freed below
    char *reader;
    for (int a = 0; a < size; a++)
    {
        reader = obj->oDataAddress + obj->oSize - 1 - a;
        floatData[a] = (uint8_t)*reader;
    }
    double real = *(double *)floatData;
    obj->oReal = real;
    free(floatData);
}

// Copies blob of raw data into "obj"
void ReadData_Data(BPObject *obj)
{
    uint64_t size = obj->oSize;
    
    if (size > 0)
    {
        obj->oData = malloc(size); // freed with DeleteMessage()
        memcpy(obj->oData, obj->oDataAddress, size);
    }
    else
    {
        //printf("Warning: Could not read data blob, as it was zero bytes.\n");
        return;
    }
}

// Saves a pointer to an ASCII string into "obj"
void ReadData_StringASCII(BPObject *obj)
{
    uint64_t size = obj->oSize;
    
    if (size > 0)
    {
        obj->oData = malloc(size + 1); // freed with DeleteMessage()
        strncpy(obj->oData, obj->oDataAddress, size);
        obj->oData[size] = '\0';
        
        if (!strcmp(obj->oData, "BaseWritingDirection"))
            obj->oIsBaseWritingDirection = true;
        else if (!strcmp(obj->oData, "NS.time"))
            obj->oIsNSTime = true;
    }
    else
    {
        //printf("Warning: Did not attempt to get ASCII string, as it was empty.\n");
        return;
    }
}

// Saves a pointer to a Unicode (16-bit) string into "obj"
void ReadData_StringUnicode(BPObject *obj)
{
    if (obj->oSize > 0)
    {
        obj->oData = malloc((obj->oSize * 2) + 1); // "oSize" is the number of wide chars; freed with DeleteMessage()
        memcpy(obj->oData, obj->oDataAddress, obj->oSize * 2);
        obj->oData[obj->oSize * 2] = '\0';
    }
    else
    {
        //printf("Warning: Did not attempt to get Unicode string, as it was empty.\n");
        return;
    }
}

// Saves a scope-dependent XML node ID composed of "oSize" bytes into "obj"
void ReadData_UID(BPObject *obj)
{
    obj->oInt = ReadUInt_XByte(obj->oDataAddress, obj->oSize);
}

// There is nothing to save in "obj", as this is just an array of further objects
void ReadData_Array(BPObject *obj)
{
    obj->oBool = false;
}

// Sets are not implemented because I have not encountered any in the wild
void ReadData_Set(BPObject *obj)
{
    obj->oBool = false;
}

// There is nothing to save in "obj", but scan dictionary for special iChat-related flags
void ReadData_Dict(BPObject *obj)
{
    uint64_t size = obj->oSize;
    char *pairReader = obj->oDataAddress;
    
    for (int a = 0; a < size; a++)
    {
        // The values are listed after all the keys, so the value is at the location of the key plus the number of k/v pairs
        uint64_t keyRef = ReadUInt_XByte(pairReader, gRefSize);
        uint64_t valueRef = ReadUInt_XByte(pairReader + (gRefSize * size), gRefSize);
        
        BPObject key, value;
        if (!LoadObject(keyRef, &key))
            return;
        if (!LoadObject(valueRef, &value))
            return;
        CopyObjectMetadata(&key, &value);
        
        pairReader += gRefSize;
    }
}
#pragma mark Data-printing functions
void PrintData_Null(BPObject *obj)
{
    obj=obj; // avoid "unused parameter" warning
    PrintSpaces(gIndent);
    printf("(null)\n");
}

void PrintData_BoolFalse(BPObject *obj)
{
    obj=obj; // avoid "unused parameter" warning
    PrintSpaces(gIndent);
    printf("false\n");
}

void PrintData_BoolTrue(BPObject *obj)
{
    obj=obj; // avoid "unused parameter" warning
    PrintSpaces(gIndent);
    printf("true\n");
}

void PrintData_Fill(BPObject *obj)
{
    PrintSpaces(gIndent);
    printf("(%llu bytes of filler)\n", obj->oSize);
}

void PrintData_Int(BPObject *obj)
{
    PrintSpaces(gIndent);
    
    if (obj->oIsBaseWritingDirection)
        printf("%lli\n", (int64_t)obj->oInt);
    else
        printf("%llu\n", obj->oInt);
}

void PrintData_Real(BPObject *obj)
{
    PrintSpaces(gIndent);
    
    if (obj->oIsNSTime)
        ConvertNSDate(obj->oReal, NULL, kDatePrint);
    else
        printf("%ff\n", obj->oReal);
}

void PrintData_Date(BPObject *obj)
{
    PrintSpaces(gIndent);
    ConvertNSDate(obj->oReal, NULL, kDatePrint);
}

void PrintData_Data(BPObject *obj)
{
    PrintSpaces(gIndent);
    printf("Printing %llu byte(s) of raw data:\n", obj->oSize);
    printf("hex  dec  char\n");
    for (int a = 0; a < obj->oSize; a++)
        printf("0x%02x %03d  '%c'\n", obj->oData[a], obj->oData[a], obj->oData[a]);
}

void PrintData_StringASCII(BPObject *obj)
{
    PrintSpaces(gIndent);
    printf("'%s'\n", obj->oData);
}

void PrintData_StringUnicode(BPObject *obj)
{
    PrintSpaces(gIndent);
    PrintWideString(obj->oData, obj->oSize);
}

void PrintData_UID(BPObject *obj)
{
    PrintSpaces(gIndent);
    printf("UID %llu\n", obj->oInt);
}

void PrintData_Array(BPObject *obj)
{
    uint64_t size = obj->oSize;
    PrintSpaces(gIndent);
    printf("The array has %llu element%s:\n", size, size == 1 ? "" : "s");
    gIndent++;
    char *elemReader = obj->oDataAddress;
    for (int a = 0; a < size; a++)
    {
        uint64_t elemRef = ReadUInt_XByte(elemReader, gRefSize);
        if (gFollowRefs)
        {
            BPObject elem;
            if (!LoadObject(elemRef, &elem))
                return;
            PrintObject(&elem);
        }
        else
        {
            PrintSpaces(gIndent); gPrintedSpaces = false; // take the place of calling PrintObject()
            printf("(UID %llu)\n", elemRef);
        }
        
        elemReader += gRefSize;
    }
    gIndent--;
}

void PrintData_Set(BPObject *obj)
{
    PrintSpaces(gIndent);
    printf("Warning: The 'set' type is not supported yet, but this is a %llu-element set.\n", obj->oSize);
}

void PrintData_Dict(BPObject *obj)
{
    uint64_t size = obj->oSize;
    PrintSpaces(gIndent);
    printf("The dict has %llu key/value pair%s.\n", size, size == 1 ? "" : "s");
    gIndent++;
    char *pairReader = obj->oDataAddress;
    for (int a = 0; a < size; a++)
    {
        // The values are listed after all the keys, so the value is at the location of the key plus half the dict size. The
        // dict size is 2 * "size" because "size" is the number of k/v pairs.
        uint64_t keyRef = ReadUInt_XByte(pairReader, gRefSize);
        uint64_t valueRef = ReadUInt_XByte(pairReader + (gRefSize * size), gRefSize);
        
        if (gFollowRefs)
        {
            BPObject key, value;
            if (!LoadObject(keyRef, &key))
                return;
            if (!LoadObject(valueRef, &value))
                return;
            CopyObjectMetadata(&key, &value);
            PrintObject(&key);
            PrintObject(&value);
        }
        else
        {
            PrintSpaces(gIndent); gPrintedSpaces = false; // take the place of calling PrintObject()
            printf("(UID %llu, %llu)\n", keyRef, valueRef);
        }
        
        pairReader += gRefSize;
    }
    gIndent--;
}
#pragma mark Utility functions
// Search given dictionary for given key name and return the value as a reference (offset table index)
uint64_t ReturnValueRefForKeyName(BPObject *dict, char *name)
{
    if (dict->oSize == (uint64_t)-1)
    {
        printf("Error: ReturnValueRefForKeyName() was passed an object that was not finished loading.\n");
        return (uint64_t)-1;
    }
    
    if (dict->oType != kTypeDict)
    {
        printf("Error: ReturnValueRefForKeyName() was passed an object that is not a dictionary.\n");
        return (uint64_t)-1;
    }
    
    // Search dictionary's key names
    char *reader = dict->oDataAddress;
    for (int a = 0; a < dict->oSize; a++)
    {
        uint64_t keyRef = ReadUInt_XByte(reader, gRefSize);
        BPObject key;
        if (!LoadObject(keyRef, &key))
            return (uint64_t)-1;
        if (key.oType == kTypeStringASCII)
        {
            if (!strcmp(key.oData, name))
            {
                // Corresponding value in this pair is in second half of dict, so add number of k/v pairs to jump to it
                uint64_t valueRef = ReadUInt_XByte(reader + (gRefSize * dict->oSize), gRefSize);
                return valueRef;
            }
        }
        
        reader += gRefSize;
    }
    
    //printf("Warning: Failed to find key \"%s\" in dictionary with UID %llu.\n", name, dict->oUID);
    return (uint64_t)-1;
}

// Search given array for given element number and return the element as a reference (offset table index)
uint64_t ReturnElemRef(BPObject *array, uint64_t elem)
{
    if (array->oSize == (uint64_t)-1)
    {
        printf("Error: ReturnElemRef() was passed an object that was not finished loading.\n");
        return (uint64_t)-1;
    }
    
    if (array->oType != kTypeArray)
    {
        printf("Error: ReturnElemRef() was passed an object that is not an array.\n");
        return (uint64_t)-1;
    }
    
    if (elem >= array->oSize)
    {
        printf("Error: ReturnElemRef() was asked for element %llu in a %llu-element array.\n", elem, array->oSize);
        return (uint64_t)-1;
    }
    
    char *reader = array->oDataAddress;
    reader += (elem * gRefSize);
    uint64_t objNum = ReadUInt_XByte(reader, gRefSize);
    return objNum;
}

// Converts "nsDate" into a string using a rough implementation of Apple's NSDate format, and if "mode" is 0 prints it to screen,
// else it copies the string into "strDate"
void ConvertNSDate(double nsDate, char **strDate, int mode)
{
    // Start at beginning of Apple's NSDate epoch
    int theYear = 2001;
    int theMonth = 1;
    int theDay = 1;
    int theHour = 0, theMinute = 0, theSecond = 0;
    
    // Info for calculating year-month-day
#define DaysInYear(x) (x % 4 == 0 ? (x % 100 == 0 ? (x % 400 == 0 ? 366 : 365) : 366) : 365)
#define DaysInFeb(x) (x % 4 == 0 ? (x % 100 == 0 ? (x % 400 == 0 ? 29 : 28) : 29) : 28)
#define DaysInMonth(x, y) (x == 2 ? DaysInFeb(y) : daysInMonths[theMonth - 1])
    int daysInMonths[12] = {31, -1, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    
    // Number of days that we have to count from the epoch
    int dayBank = (int)(nsDate / 60.f / 60.f / 24.f);
    
    // Number of hours, minutes and seconds that we have to count into the day
    double dayFraction = nsDate - (double)(dayBank * 60 * 60 * 24);
    
    // Increment the year until we run out of a year's worth of days
    for (; dayBank > DaysInYear(theYear); theYear++)
        dayBank -= DaysInYear(theYear);
    
    // Increment the month until we run out of the month's worth of days
    do
    {
        if (dayBank > DaysInMonth(theMonth, theYear))
        {
            dayBank -= DaysInMonth(theMonth, theYear);
            theMonth++;
        }
        else
            break;
        if (theMonth > 12)
        {
            theMonth = 1;
            theYear++;
        }
    }
    while (true);
    
    // The day of the month is however many days we have left to spend in the day bank
    theDay += dayBank;
    
    // Set hour, minute and second of day
    theHour = (int)(dayFraction / 60.f / 60.f);
    dayFraction -= (double)(theHour * 60 * 60);
    theMinute = (int)(dayFraction / 60.f);
    dayFraction -= (double)(theMinute * 60);
    theSecond = (int)(dayFraction);
    
    // Adjust for time zone; if this takes us into the previous day, deduct from theDay, theMonth, and theYear as needed
    theHour += LOCAL_TIME_ZONE;
    if (theHour < 0)
    {
        theDay--;
        theHour += 24;
        if (theDay < 1)
        {
            theMonth--;
            if (theMonth < 1)
            {
                theYear--;
                theMonth += 12;
            }
            theDay += DaysInMonth(theMonth, theYear);
        }
    }
    
    // Prepare string in desired format
    char *output = NULL;
    if (mode == kDatePrint || mode == kDateSaveLong)
        asprintf(&output, "%d-%02d-%02d %02d:%02d:%02d", theYear, theMonth, theDay, theHour, theMinute, theSecond); // freed below
    else // kDateSaveShort
        asprintf(&output, "%02d:%02d:%02d", theHour, theMinute, theSecond); // freed below
    
    // Print or save string
    if (mode == kDatePrint)
        printf("%s\n", output);
    else
    {
        uint64_t outputLength = strlen(output);
        *strDate = malloc(outputLength + 1); // freed with DeleteMessage()
        strncpy(*strDate, output, outputLength);
        char *nullTerm = *strDate + outputLength;
        *nullTerm = '\0';
    }
    
    free(output);
#undef DaysInMonth
#undef DaysInFeb
#undef DaysInYear
}

// Prints contents of a 16-bit big-endian Unicode string. "strSize" should be the number of two-byte characters in the string.
void PrintWideString(char *strPtr, uint64_t strSize)
{
    if (!getenv("TERM"))
    {
        printf("<cannot print Unicode text to Xcode console>\n");
        return;
    }
    for (int a = 0; a < strSize * 2; a += 2)
    {
        int widechar = (strPtr[a] << 8) + strPtr[a + 1];
        setlocale(LC_ALL, "");
        printf("%lc", (wint_t)widechar); // does not produce proper output in Xcode's console, but does in macOS' Terminal
    }
    printf("\n");
}

// Prints name of object type "oType"
void PrintTypeName(int oType)
{
    if (oType < kTypeNone || oType > kTypeCount)
    {
        printf("Error: PrintObjectType() was given an invalid type code!\n");
        return;
    }
    
    char *kVowels = "aeiou";
    bool useAn = false;
    for (int a = 0; a < strlen(kVowels); a++)
    {
        if (gTypeTable[oType].otName[0] == kVowels[a])
        {
            useAn = true;
            break;
        }
    }
    printf("Object is %s %s with value:", useAn ? "an" : "a", gTypeTable[oType].otName);
}

// Prints a sort of notched ruler in the space before an indented object is printed. The purpose is to help the reader of a long
// printout of nested objects understand which level of the hierarchy each object is at.
void PrintSpaces(int spaceNum)
{
    if (spaceNum <= 0 || gPrintedSpaces)
        return;
    
    char *gIndentRuler = "  |  |  |  |  |  |  |  |  |  |";
    
    if (spaceNum > strlen(gIndentRuler))
        spaceNum = (int)strlen(gIndentRuler);
    
    char *spaceOutput = malloc((unsigned long)(spaceNum + 1)); // freed below
    strncpy(spaceOutput, gIndentRuler, spaceNum);
    spaceOutput[spaceNum] = '\0';
    printf("%s", spaceOutput);
    free(spaceOutput);
    gPrintedSpaces = true;
}

// Print a string of 1s and 0s based on the "inNumber" that is "inBytes" bytes long
void PrintBinary(uint64_t inNumber, int inBytes)
{
    for (int a = inBytes * 8 - 1; a >= 0; a--)
    {
        if (inNumber & (1ull << a))
            printf("1");
        else
            printf("0");
    }
    printf("\n");
}
#pragma mark Byte-reading functions
// Receiving an unsigned big-endian integer of "size" bytes starting at "start", calls the appropriate reader for that size of
// integer
uint64_t ReadUInt_XByte(char *start, uint64_t size)
{
    if (size == 1)
        return (uint8_t)(*start);
    else if (size == 2)
        return ReadUInt_2Byte(start);
    else if (size == 4)
        return ReadUInt_4Byte(start);
    else if (size == 8)
        return ReadUInt_8Byte(start);
    
    printf("Error: Not able to read a %llu byte int.\n", size);
    return (uint64_t)-1;
}

// Returns the value of an eight-byte unsigned big-endian integer that starts at "start"
uint64_t ReadUInt_8Byte(char *start)
{
    uint64_t result = 0;
    for (int a = 0; a < 8; a++)
        result |= (uint64_t)(*((uint8_t *)(start + a)) << ((7 - a) * 8));
    
    return result;
}

// Returns the value of a four-byte unsigned big-endian integer that starts at "start"
uint32_t ReadUInt_4Byte(char *start)
{
    uint32_t result = 0;
    for (int a = 0; a < 4; a++)
        result |= (uint32_t)(*((uint8_t *)(start + a)) << ((3 - a) * 8));
    
    return result;
}

// Returns the value of a two-byte unsigned big-endian integer that starts at "start"
uint16_t ReadUInt_2Byte(char *start)
{
    uint16_t result = (uint16_t)((*(uint8_t *)(start + 1)) + (*((uint8_t *)start) << 8));
    
    return result;
}

// Receiving a big-endian integer of "size" bytes starting at "start", calls the appropriate reader for that size of integer
int64_t ReadInt_XByte(char *start, uint64_t size)
{
    if (size == 1)
        return (int8_t)(*start);
    else if (size == 2)
        return ReadInt_2Byte(start);
    else if (size == 4)
        return ReadInt_4Byte(start);
    else if (size == 8)
        return ReadInt_8Byte(start);
    
    printf("Error: Not able to read a %llu byte int.\n", size);
    return 0;
}

// Returns the value of an eight-byte big-endian integer that starts at "start"
int64_t ReadInt_8Byte(char *start)
{
    uint64_t result = 0;
    for (int a = 0; a < 8; a++)
        result |= (uint64_t)(*((uint8_t *)(start + a)) << ((7 - a) * 8));
    
    return (int16_t)result;
}

// Returns the value of a four-byte big-endian integer that starts at "start"
int32_t ReadInt_4Byte(char *start)
{
    uint32_t result = 0;
    for (int a = 0; a < 4; a++)
        result |= (uint32_t)(*((uint8_t *)(start + a)) << ((3 - a) * 8));
    
    return (int16_t)result;
}

// Returns the value of a two-byte big-endian integer that starts at "start"
int16_t ReadInt_2Byte(char *start)
{
    uint16_t result = (uint16_t)((*(uint8_t *)(start + 1)) + (*((uint8_t *)start) << 8));
    
    return (int16_t)result;
}
