// Resource identifiers shared by BibleTime.rc and the app sources.
#pragma once

// Windows reads string 1 as the screen saver's display name and icon 100 (ID_APP in scrnsave.h) as its icon.
#define IDS_DESCRIPTION 1
#define IDI_APP 100

// Embedded data (RCDATA), served by ResourceLoader.
#define IDR_BOOKS_JSON 201
#define IDR_CURATED_JSON 202
#define IDR_HOUR_VERSES_JSON 203
#define IDR_KJV_JSON 204
#define IDR_DBY_JSON 205

// Options dialog (2003 is DLG_SCRNSAVECONFIGURE in scrnsave.h).
#define IDD_OPTIONS 2003
#define IDC_TITLE 1001
#define IDC_SUBTITLE 1002
#define IDC_TRANSLATION_LABEL 1003
#define IDC_TRANSLATION 1004
#define IDC_IMPORT 1005
#define IDC_REMOVE 1006
#define IDC_HOURS 1007
#define IDC_THEME 1008
#define IDC_LAYOUT 1009
#define IDC_TYPEFACE 1010
#define IDC_TEXTSIZE 1011
#define IDC_TEXTSIZE_LABEL 1012
#define IDC_DISPLAYS 1013
#define IDC_SHOWCLOCK 1014
#define IDC_FAMOUS 1015
#define IDC_EXTEND 1016
#define IDC_PAD 1017
#define IDC_DEFAULTS 1018
#define IDC_STATIC_LABEL -1
