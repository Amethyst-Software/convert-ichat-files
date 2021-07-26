//
//  ichatReader.h
//  Convert ichat Files
//
//  Created on 12/8/18.
//  Copyright © 2018 Amethyst Software (contact@amethystsoftware.com). All rights reserved.
//

#ifndef ichatReader_h
#define ichatReader_h

typedef struct ICMessage
{
    bool     mHiccup;       // if true, this message is an "SMS hiccup" and should be ignored
    bool     mFromClient;   // if true, this is a message from the IM client, not a human
    uint64_t mFileTransfer; // if zero, this message is a regular text message; otherwise, the number of files being sent
    char    *mSenderID;     // account ID of this user with their IM service
    char    *mTime;         // string with date and time that message was sent
    char    *mText;         // the text of the message, or the name(s) of the file(s) if "mFileTransfer" is non-zero
    uint64_t mWideStrSize;  // size of "mText" in 2-byte Unicode chars, if "mText" is not ASCII; doubles as flag marking msg as Unicode
} ICMessage;

bool     Validate_ichat(void);
bool     Load_ichat(void);
void     Browse_ichatObjects(void);
void     Browse_ichatMessages(void);
void     Convert_ichat(bool useRTF);
void     InitMessage(ICMessage *msg);
bool     LoadMessage(BPObject *BPmsg, ICMessage *ICmsg, bool firstMsg);
void     PrintMessage(ICMessage *msg);
void     ConvertMessageToRTF(ICMessage *msg);
void     ConvertMessageToTXT(ICMessage *msg);
void     DeleteMessage(ICMessage *msg);
uint64_t ReturnMessageRef(uint64_t msgNum);
void     ConvertUnicodeToUTF8(char *unicodeStr, char **utf8Str);
void     WriteSenderName(ICMessage *msg, bool useRTF);
void     WriteRTFHeader(void);
void     WriteRTFFooter(void);
void     WriteTimeHeader(bool useRTF);

#endif /* ichatReader_h */
