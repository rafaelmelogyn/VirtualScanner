#pragma once
//=============================================================================
// twain.h  –  TWAIN 1.9 minimal header  (no DS_Entry forward declaration)
//=============================================================================
#include <windows.h>
#pragma pack(push, 2)

#undef FAR
#define FAR

#define TWON_PROTOCOLMAJOR 1
#define TWON_PROTOCOLMINOR 9

/* ── Basic types ─────────────────────────────────────────────────────────── */
typedef char           TW_STR32[34],  FAR *pTW_STR32;
typedef char           TW_STR64[66],  FAR *pTW_STR64;
typedef char           TW_STR128[130],FAR *pTW_STR128;
typedef char           TW_STR255[256],FAR *pTW_STR255;
typedef char           TW_INT8,  FAR *pTW_INT8;
typedef short          TW_INT16, FAR *pTW_INT16;
typedef long           TW_INT32, FAR *pTW_INT32;
typedef unsigned char  TW_UINT8, FAR *pTW_UINT8;
typedef unsigned short TW_UINT16,FAR *pTW_UINT16;
typedef unsigned long  TW_UINT32,FAR *pTW_UINT32;
typedef unsigned short TW_BOOL,  FAR *pTW_BOOL;
typedef HANDLE         TW_HANDLE;
typedef LPVOID         TW_MEMREF;

/* ── Composite types ─────────────────────────────────────────────────────── */
typedef struct { TW_INT16 Whole; TW_UINT16 Frac; }  TW_FIX32,  FAR *pTW_FIX32;
typedef struct { TW_FIX32 Left,Top,Right,Bottom; }   TW_FRAME,  FAR *pTW_FRAME;
typedef struct {
    TW_UINT32 Flags; TW_UINT32 Length; TW_MEMREF TheMem;
} TW_MEMORY, FAR *pTW_MEMORY;

typedef struct {
    TW_UINT16 MajorNum, MinorNum, Language, Country; TW_STR32 Info;
} TW_VERSION, FAR *pTW_VERSION;

typedef struct {
    TW_UINT32  Id;
    TW_VERSION Version;
    TW_UINT16  ProtocolMajor, ProtocolMinor;
    TW_UINT32  SupportedGroups;
    TW_STR32   Manufacturer, ProductFamily, ProductName;
} TW_IDENTITY, FAR *pTW_IDENTITY;

typedef struct { TW_UINT16 ConditionCode, Reserved; }    TW_STATUS,   FAR *pTW_STATUS;
typedef struct { TW_BOOL ShowUI, ModalUI; TW_HANDLE hParent; } TW_USERINTERFACE, FAR *pTW_USERINTERFACE;
typedef struct { TW_MEMREF pEvent; TW_UINT16 TWMessage; }TW_EVENT,   FAR *pTW_EVENT;
typedef struct { TW_UINT16 Cap, ConType; TW_HANDLE hContainer; } TW_CAPABILITY, FAR *pTW_CAPABILITY;
typedef struct { TW_UINT16 ItemType; TW_UINT32 Item; }   TW_ONEVALUE, FAR *pTW_ONEVALUE;
typedef struct {
    TW_UINT16 ItemType; TW_UINT32 MinValue, MaxValue, StepSize, DefaultValue, CurrentValue;
} TW_RANGE, FAR *pTW_RANGE;
typedef struct {
    TW_UINT16 ItemType; TW_UINT32 NumItems; TW_UINT8 ItemList[1];
} TW_ARRAY, FAR *pTW_ARRAY;
typedef struct {
    TW_UINT16 ItemType; TW_UINT32 NumItems, CurrentIndex, DefaultIndex; TW_UINT8 ItemList[1];
} TW_ENUMERATION, FAR *pTW_ENUMERATION;
typedef struct {
    TW_FIX32  XResolution, YResolution;
    TW_INT32  ImageWidth, ImageLength;
    TW_INT16  SamplesPerPixel, BitsPerSample[8], BitsPerPixel;
    TW_BOOL   Planar; TW_INT16 PixelType; TW_UINT16 Compression;
} TW_IMAGEINFO, FAR *pTW_IMAGEINFO;
typedef struct {
    TW_UINT16 Count;
    union { TW_UINT32 EOJ; TW_UINT32 Reserved; } u;
} TW_PENDINGXFERS, FAR *pTW_PENDINGXFERS;
typedef struct { TW_UINT32 MinBufSize, MaxBufSize, Preferred; } TW_SETUPMEMXFER, FAR *pTW_SETUPMEMXFER;
typedef struct {
    TW_UINT16 Compression;
    TW_UINT32 BytesPerRow, Columns, Rows, XOffset, YOffset, BytesWritten;
    TW_MEMORY Memory;
} TW_IMAGEMEMXFER, FAR *pTW_IMAGEMEMXFER;
typedef struct { TW_STR255 FileName; TW_UINT16 Format; TW_INT16 VRefNum; } TW_SETUPFILEXFER, FAR *pTW_SETUPFILEXFER;
typedef struct {
    TW_FRAME  Frame;
    TW_UINT32 DocumentNumber, PageNumber, FrameNumber;
} TW_IMAGELAYOUT, FAR *pTW_IMAGELAYOUT;
typedef struct { TW_UINT32 InfoLength; TW_HANDLE hData; } TW_CUSTOMDSDATA, FAR *pTW_CUSTOMDSDATA;

/* ── Container types ─────────────────────────────────────────────────────── */
#define TWON_ARRAY       3
#define TWON_ENUMERATION 4
#define TWON_ONEVALUE    5
#define TWON_RANGE       6
#define TWON_DONTCARE16  0xffff
#define TWON_DONTCARE32  0xffffffffL

/* ── Item types ──────────────────────────────────────────────────────────── */
#define TWTY_INT8   0x0000
#define TWTY_INT16  0x0001
#define TWTY_INT32  0x0002
#define TWTY_UINT8  0x0003
#define TWTY_UINT16 0x0004
#define TWTY_UINT32 0x0005
#define TWTY_BOOL   0x0006
#define TWTY_FIX32  0x0007
#define TWTY_FRAME  0x0008
#define TWTY_STR32  0x0009
#define TWTY_STR64  0x000a
#define TWTY_STR128 0x000b
#define TWTY_STR255 0x000c

/* ── Data Groups ─────────────────────────────────────────────────────────── */
#define DG_CONTROL 0x0001L
#define DG_IMAGE   0x0002L
#define DG_AUDIO   0x0004L

/* ── DAT values ──────────────────────────────────────────────────────────── */
#define DAT_NULL            0x0000
#define DAT_CAPABILITY      0x0001
#define DAT_EVENT           0x0002
#define DAT_IDENTITY        0x0003
#define DAT_PARENT          0x0004
#define DAT_PENDINGXFERS    0x0005
#define DAT_SETUPMEMXFER    0x0006
#define DAT_SETUPFILEXFER   0x0007
#define DAT_STATUS          0x0008
#define DAT_USERINTERFACE   0x0009
#define DAT_XFERGROUP       0x000a
#define DAT_CUSTOMDSDATA    0x000c
#define DAT_IMAGEINFO       0x0101
#define DAT_IMAGELAYOUT     0x0102
#define DAT_IMAGEMEMXFER    0x0103
#define DAT_IMAGENATIVEXFER 0x0104
#define DAT_IMAGEFILEXFER   0x0105
#define DAT_EXTIMAGEINFO    0x010b

/* ── MSG values ──────────────────────────────────────────────────────────── */
#define MSG_NULL           0x0000
#define MSG_GET            0x0001
#define MSG_GETCURRENT     0x0002
#define MSG_GETDEFAULT     0x0003
#define MSG_GETFIRST       0x0004
#define MSG_GETNEXT        0x0005
#define MSG_SET            0x0006
#define MSG_RESET          0x0007
#define MSG_QUERYSUPPORT   0x0008
#define MSG_SETCONSTRAINT  0x000c
#define MSG_XFERREADY      0x0101
#define MSG_CLOSEDSREQ     0x0102
#define MSG_CLOSEDSOK      0x0103
#define MSG_DEVICEEVENT    0x0104
#define MSG_OPENDS         0x0201
#define MSG_CLOSEDS        0x0202
#define MSG_USERSELECT     0x0203
#define MSG_ENABLEDS       0x0301
#define MSG_DISABLEDS      0x0302
#define MSG_ENABLEDSUIONLY 0x0303
#define MSG_PROCESSEVENT   0x0401
#define MSG_ENDXFER        0x0501
#define MSG_STOPFEEDER     0x0502
#define MSG_RESETALL       0x0aaa

/* ── Return codes ────────────────────────────────────────────────────────── */
#define TWRC_SUCCESS          0
#define TWRC_FAILURE          1
#define TWRC_CHECKSTATUS      2
#define TWRC_CANCEL           3
#define TWRC_DSEVENT          4
#define TWRC_NOTDSEVENT       5
#define TWRC_XFERDONE         6
#define TWRC_ENDOFLIST        7
#define TWRC_INFONOTSUPPORTED 8
#define TWRC_DATANOTAVAILABLE 9
#define TWRC_BUSY             10

/* ── Condition codes ─────────────────────────────────────────────────────── */
#define TWCC_SUCCESS          0
#define TWCC_BUMMER           1
#define TWCC_LOWMEMORY        2
#define TWCC_NODS             3
#define TWCC_MAXCONNECTIONS   4
#define TWCC_OPERATIONERROR   5
#define TWCC_BADCAP           6
#define TWCC_SEQERROR         11
#define TWCC_CAPUNSUPPORTED   13
#define TWCC_CAPBADOPERATION  14

/* ── Capability codes ────────────────────────────────────────────────────── */
#define CAP_XFERCOUNT          0x0001
#define CAP_SUPPORTEDCAPS      0x0002
#define CAP_UICONTROLLABLE     0x000b
#define CAP_DEVICEONLINE       0x000c
#define CAP_DUPLEX             0x000f
#define CAP_DUPLEXENABLED      0x0010
#define CAP_ENABLEDSUIONLY     0x0011
#define CAP_FEEDERENABLED      0x0012
#define CAP_FEEDERLOADED       0x0013
#define CAP_AUTOFEED           0x0014
#define CAP_CLEARPAGE          0x0015
#define CAP_FEEDPAGE           0x0016
#define CAP_REWINDPAGE         0x0017
#define CAP_INDICATORS         0x0019
#define CAP_PAPERDETECTABLE    0x0033

#define ICAP_COMPRESSION       0x0100
#define ICAP_PIXELTYPE         0x0101
#define ICAP_UNITS             0x0102
#define ICAP_XFERMECH          0x0103
#define ICAP_BITDEPTH          0x112b
#define ICAP_XRESOLUTION       0x1118
#define ICAP_YRESOLUTION       0x1119
#define ICAP_XNATIVERESOLUTION 0x1116
#define ICAP_YNATIVERESOLUTION 0x1117
#define ICAP_PHYSICALWIDTH     0x1111
#define ICAP_PHYSICALHEIGHT    0x1112
#define ICAP_SUPPORTEDSIZES    0x1122
#define ICAP_IMAGEFILEFORMAT   0x110c
#define ICAP_PLANARCHUNKY      0x1120
#define ICAP_BITDEPTHREDUCTION 0x112c
#define ICAP_PIXELFLAVOR       0x0106

/* ── Capability values ───────────────────────────────────────────────────── */
#define TWCP_NONE     0
#define TWSX_NATIVE   0
#define TWSX_FILE     1
#define TWSX_MEMORY   2
#define TWPT_BW       0
#define TWPT_GRAY     1
#define TWPT_RGB      2
#define TWUN_INCHES   0
#define TWPF_CHOCOLATE 0   // TWPF_CHOCOLATE = 0 (0=black=0)
#define TWPF_VANILLA   1
#define TWPC_CHUNKY   0
#define TWFF_BMP      2
#define TWFF_JFIF     4
#define TWFF_PNG      7
#define TWSS_NONE     0
#define TWSS_A4       1
#define TWSS_USLETTER 3
#define TWLG_USA      13
#define TWCY_USA      1

/* ── DSM entry proc typedef (no DS_Entry declaration here) ───────────────── */
typedef TW_UINT16 (FAR __stdcall *DSMENTRYPROC)(
    TW_IDENTITY FAR*, TW_IDENTITY FAR*,
    TW_UINT32, TW_UINT16, TW_UINT16, TW_MEMREF);

#pragma pack(pop)
