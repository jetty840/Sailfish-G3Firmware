#ifndef _LOCALE_H__
#define _LOCALE_H__

#ifdef LOCALE

#define L_EXPAND(x) x
#define L_CONCAT(x,y) L_EXPAND(x)y
#define L_STR(x) #x
#define L_XSTR(x) L_STR(x)
#define L_INCLUDE_FILE L_XSTR(L_CONCAT(menu_,L_CONCAT(LOCALE,.h)))

#include L_INCLUDE_FILE

#undef L_CONCAT
#undef L_EXPAND
#undef L_STR
#undef L_XSTR
#undef L_INCLUDE_FILE

#else

#ifdef LOCALIZE
#undef LOCALIZE
#endif
#define LOCALIZE(s) (s)

#endif
#endif // _LOCALE_H__
