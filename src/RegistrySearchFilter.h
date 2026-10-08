/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#define kPdfFilterClsid "{51CB356F-63C4-4E43-9C49-9C0ADE1B7BAF}"
#define kPdfFilterHandler "{26CA6565-F22A-4f5e-B688-0AD051D56E96}"

#define kTexFilterClsid "{F24B73B4-E11A-43CD-B6BA-B99761A30E11}"
#define kTexFilterHandler "{3FAB27F8-08EC-4b9e-9EEE-181A6E846B8D}"

#define kEpubFilterClsid "{8F5994C8-7CFE-4EF7-A636-7F13121C91B8}"
#define kEpubFilterHandler "{FF68D1A0-DA54-4fbf-A406-06CFDB764CA9}"

bool InstallSearchFilter(Str dllPath, bool allUsers);
bool UninstallSearchFilter();
bool IsSearchFilterInstalled();