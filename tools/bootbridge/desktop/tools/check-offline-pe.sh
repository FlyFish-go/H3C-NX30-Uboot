#!/bin/sh
# Static import audit for the BootBridge Windows executable.
# winhttp.dll is allowed only for the H3C official-firmware download module.
# This checks the PE import table; it does not prove control-flow behavior.
set -eu
if [ "$#" -ne 2 ]; then echo 'Usage: check-offline-pe.sh x86|x64 executable' >&2; exit 2; fi
case "$1" in
    x86) inspect=i686-w64-mingw32-objdump; format=pei-i386 ;;
    x64) inspect=x86_64-w64-mingw32-objdump; format=pei-x86-64 ;;
    *) exit 2 ;;
esac
command -v "$inspect" >/dev/null
"$inspect" -f "$2" | grep -F "file format $format" >/dev/null
"$inspect" -p "$2" | awk '
    /MajorSubsystemVersion/ { if ($2 == 6) major = 1 }
    /MinorSubsystemVersion/ { if ($2 == 1) minor = 1 }
    /^Subsystem/ { if ($2 == "00000002") gui = 1 }
    /DLL Name:/ {
        dll = tolower($3); count++
        if (dll != "comdlg32.dll" && dll != "comctl32.dll" &&
            dll != "advapi32.dll" && dll != "dwmapi.dll" &&
            dll != "gdi32.dll" && dll != "gdiplus.dll" && dll != "kernel32.dll" &&
            dll != "msvcrt.dll" && dll != "shell32.dll" &&
            dll != "user32.dll" && dll != "version.dll" && dll != "winhttp.dll") {
            print "Unexpected imported DLL: " $3 > "/dev/stderr"; bad = 1
        }
    }
    END { if (!major || !minor || !gui || !count || bad) exit 1 }
'
echo "Static offline PE checks passed: $1, GUI subsystem 6.01, restricted system DLL imports."
