// Shared VERSIONINFO for ReResorep binaries. Before including, define:
//   RR_FILE_DESCRIPTION, RR_ORIGINAL_FILENAME, RR_INTERNAL_NAME, RR_FILE_TYPE (VFT_APP / VFT_DLL)
// and RR_PROXY to take the proxy's own version instead of ReResorep's.
#include <winver.h>
#include "version.h"

#ifdef RR_PROXY
#define RR_VERSION_MAJOR  RERESOREP_PROXY_VERSION_MAJOR
#define RR_VERSION_MINOR  RERESOREP_PROXY_VERSION_MINOR
#define RR_VERSION_PATCH  RERESOREP_PROXY_VERSION_PATCH
#define RR_VERSION_STRING RERESOREP_PROXY_VERSION_STRING
#else
#define RR_VERSION_MAJOR  RERESOREP_VERSION_MAJOR
#define RR_VERSION_MINOR  RERESOREP_VERSION_MINOR
#define RR_VERSION_PATCH  RERESOREP_VERSION_PATCH
#define RR_VERSION_STRING RERESOREP_VERSION_STRING
#endif

VS_VERSION_INFO VERSIONINFO
 FILEVERSION     RR_VERSION_MAJOR, RR_VERSION_MINOR, RR_VERSION_PATCH, 0
 PRODUCTVERSION  RR_VERSION_MAJOR, RR_VERSION_MINOR, RR_VERSION_PATCH, 0
 FILEFLAGSMASK   VS_FFI_FILEFLAGSMASK
 FILEFLAGS       0
 FILEOS          VOS_NT_WINDOWS32
 FILETYPE        RR_FILE_TYPE
 FILESUBTYPE     VFT2_UNKNOWN
BEGIN
    BLOCK "StringFileInfo"
    BEGIN
        BLOCK "040904b0"
        BEGIN
            VALUE "CompanyName",      "ReResorep"
            VALUE "FileDescription",  RR_FILE_DESCRIPTION
            VALUE "FileVersion",      RR_VERSION_STRING
            VALUE "InternalName",     RR_INTERNAL_NAME
            VALUE "LegalCopyright",   RERESOREP_COPYRIGHT
            VALUE "OriginalFilename", RR_ORIGINAL_FILENAME
            VALUE "ProductName",      "ReResorep"
            VALUE "ProductVersion",   RR_VERSION_STRING
            VALUE "Comments",         "Compatible rewrite of Resorep by RayWing. https://github.com/ocornut/imgui (MIT) used for the UI."
        END
    END
    BLOCK "VarFileInfo"
    BEGIN
        VALUE "Translation", 0x409, 1200
    END
END
