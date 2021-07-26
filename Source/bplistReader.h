//
//  bplistReader.h
//  Convert ichat Files
//
//  Created on 3/27/17.
//  Copyright © 2017 Amethyst Software (contact@amethystsoftware.com). All rights reserved.
//

#ifndef bplistReader_h
#define bplistReader_h

#define LOCAL_TIME_ZONE  -5

// Possible types of data, as specified by the object's code byte; see Apple's CFBinaryPList.c for original bplist format breakdown
enum BPObjectTypeCode
{
    kTypeNone = 0,
    kTypeNull,
    kTypeBoolFalse,
    kTypeBoolTrue,
    kTypeFill,
    kTypeInt, // 5
    kTypeReal,
    kTypeDate,
    kTypeData,
    kTypeStringASCII,
    kTypeStringUnicode, // 10
    kTypeUID,
    kTypeArray,
    kTypeSet,
    kTypeDict,
    kTypeCount // 15
};

// Modes for ConvertNSDate()
enum BPTimeConvertMode
{
    kDatePrint,
    kDateSaveLong,
    kDateSaveShort
};

// Ways in which an object indicates the size of its data payload
enum BPObjectSizeType
{
    kSizeNone,           // no data payload follows
    kSizePowerOfTwo,     // payload is 2^x bytes, where 'x' is lower quadbit of object code byte
    kSize8ByteFloat,     // payload is always an 8-byte float
    kSizeScalarOverflow, // number of bytes/chars/objects is in lower quadbit, unless it's 0xF, in which case it's stored in a
                         // subsequent scalar int
    kSizeAddOne          // payload is x+1 bytes, where 'x' is lower quadbit
};

// For storing the information about a given object in the plist, plus its data in whichever type of variable is applicable
typedef struct BPObject
{
    uint64_t oUID;         // the bplist's UID for the object, set in stage 1
    char    *oObjAddress;  // where this object starts in memory, set in stage 2
    int      oType;        // a value from enum BPObjectTypeCode, set in stage 3
    uint64_t oSize;        // number of units of data (bytes, objects, etc.), set in stage 4
    char    *oDataAddress; // where payload starts in memory, set in stage 4
    bool     oBool;        // the data payload will be in one of these members, set in stage 5
    uint64_t oInt;         // used to store Int and UID data types
    double   oReal;        // used to store Real and Date data types
    char    *oData;        // used to store ASCII and Unicode data types
    bool     oIsBaseWritingDirection;
    bool     oIsNSTime;
} BPObject;

// Allows us to build a table of object type info
typedef struct BPObjectType
{
    int    otEnum;     // a value from enum BPObjectTypeCode
    int    otHighQuad; // upper half of byte identifies the type, sometimes in league with lower half
    int    otLowQuad;  // if -1, then it is not part of the type code and precedes the count of following bytes/chars/objects
    int    otSizeType; // a value from enum BPObjectSizeType
    void (*otReadFunc)(BPObject *); // designated function for reading this type of data
    void (*otPrintFunc)(BPObject *); // designated function for printing this type of data
    char  *otName;     // human-readable name of type
} BPObjectType;

bool     Validate_bplist(void);
bool     Load_bplist(void);
void     Browse_bplistElements(void);
bool     LoadObject(uint64_t objNum, BPObject *obj);
bool     LoadObject_S1_Init(uint64_t objNum, BPObject *obj);
bool     LoadObject_S2_Locate(BPObject *obj);
bool     LoadObject_S3_GetType(BPObject *obj);
bool     LoadObject_S4_ReadSize(BPObject *obj);
bool     LoadObject_S5_ReadData(BPObject *obj);
void     CopyObjectMetadata(BPObject *objSrc, BPObject *objDest);
void     PrintObject(BPObject *obj);
void     ReadData_Null(BPObject *obj);
void     ReadData_BoolFalse(BPObject *obj);
void     ReadData_BoolTrue(BPObject *obj);
void     ReadData_Fill(BPObject *obj);
void     ReadData_Int(BPObject *obj);
void     ReadData_Real(BPObject *obj);
void     ReadData_Date(BPObject *obj);
void     ReadData_Data(BPObject *obj);
void     ReadData_StringASCII(BPObject *obj);
void     ReadData_StringUnicode(BPObject *obj);
void     ReadData_UID(BPObject *obj);
void     ReadData_Array(BPObject *obj);
void     ReadData_Set(BPObject *obj);
void     ReadData_Dict(BPObject *obj);
void     PrintData_Null(BPObject *obj);
void     PrintData_BoolFalse(BPObject *obj);
void     PrintData_BoolTrue(BPObject *obj);
void     PrintData_Fill(BPObject *obj);
void     PrintData_Int(BPObject *obj);
void     PrintData_Real(BPObject *obj);
void     PrintData_Date(BPObject *obj);
void     PrintData_Data(BPObject *obj);
void     PrintData_StringASCII(BPObject *obj);
void     PrintData_StringUnicode(BPObject *obj);
void     PrintData_UID(BPObject *obj);
void     PrintData_Array(BPObject *obj);
void     PrintData_Set(BPObject *obj);
void     PrintData_Dict(BPObject *obj);
uint64_t ReturnValueRefForKeyName(BPObject *dict, char *name);
uint64_t ReturnElemRef(BPObject *array, uint64_t elem);
void     ConvertNSDate(double nsDate, char **strDate, int mode);
void     PrintWideString(char *strPtr, uint64_t strSize);
void     PrintTypeName(int oType);
void     PrintSpaces(int spaceNum);
void     PrintBinary(uint64_t inNumber, int inBytes);
uint64_t ReadUInt_XByte(char *start, uint64_t size);
uint64_t ReadUInt_8Byte(char *start);
uint32_t ReadUInt_4Byte(char *start);
uint16_t ReadUInt_2Byte(char *start);
int64_t  ReadInt_XByte(char *start, uint64_t size);
int64_t  ReadInt_8Byte(char *start);
int32_t  ReadInt_4Byte(char *start);
int16_t  ReadInt_2Byte(char *start);

#endif /* bplistReader_h */
