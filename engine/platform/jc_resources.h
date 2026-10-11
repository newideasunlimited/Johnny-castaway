/*
 *  Win32 resource identifiers, shared by the resource script and the C code.
 *
 *  Both vs/jc_reborn/jc_reborn.rc and platform/platform_windows.c need the icon
 *  id, and a literal 101 written independently in each is precisely how the two
 *  drift apart: the .rc would still compile, the window class would still
 *  register, and the only symptom would be the blank default icon reappearing
 *  with nothing in the build log to explain it.
 *
 *  rc.exe understands #include, so this file is the single definition.
 */

#ifndef JC_RESOURCES_H
#define JC_RESOURCES_H

#define JC_REBORN_ICON_ID   101
#define JC_CONFIG_DIALOG_ID 200
#define JC_STYLE_COMBO_ID   201

#endif /* JC_RESOURCES_H */
