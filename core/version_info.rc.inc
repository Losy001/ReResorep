// Shared VERSIONINFO for ReResorep binaries. Before including, define:
//   RR_FILE_DESCRIPTION, RR_ORIGINAL_FILENAME, RR_INTERNAL_NAME, RR_FILE_TYPE (VFT_APP / VFT_DLL)
#include <winver.h>
#include "version.h"

VS_VERSION_INFO VERSIONINFO
 FILEVERSION     RERESOREP_VERSION_MAJOR, RERESOREP_VERSION_MINOR, RERESOREP_VERSION_PATCH, 0
 PRODUCTVERSION  RERESOREP_VERSION_MAJOR, RERESOREP_VERSION_MINOR, RERESOREP_VERSION_PATCH, 0
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
            VALUE "FileVersion",      RERESOREP_VERSION_STRING
            VALUE "InternalName",     RR_INTERNAL_NAME
            VALUE "LegalCopyright",   RERESOREP_COPYRIGHT
            VALUE "OriginalFilename", RR_ORIGINAL_FILENAME
            VALUE "ProductName",      "ReResorep"
            VALUE "ProductVersion",   RERESOREP_VERSION_STRING
            VALUE "Comments",         "Compatible rewrite of Resorep by RayWing. https://github.com/ocornut/imgui (MIT) used for the UI."
        END
    END
    BLOCK "VarFileInfo"
    BEGIN
        VALUE "Translation", 0x409, 1200
    END
END
