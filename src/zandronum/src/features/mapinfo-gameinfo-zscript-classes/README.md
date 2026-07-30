# mapinfo-gameinfo-zscript-classes

GAMEINFO keys that name a ZScript class or register VM event handlers. **Class: NOT-PORTABLE** —
this base has no ZScript VM, so there is nothing to resolve them to. Logged once "not supported in
this port (uzdoom@<sha>)". Tracker: `ZXUnhandledGameInfoKeys[]` in `src/zandronum/src/gi.cpp`.

statusbarclass, althudclass, MessageBoxClass, HelpMenuClass, MenuDelegateClass,
defaultconversationmenuclass, eventhandlers, addeventhandlers, statscreen_single/coop/dm.
(SHAs in the code manifest; several carry the a1cc548af reorg date — true origin older.)
