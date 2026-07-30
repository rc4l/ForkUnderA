# mapinfo-gameinfo-parseonly

GAMEINFO keys ZandroX recognises but leaves inert (menu/statscreen/renderer values our C++ menu +
base renderer don't consume). **Class: PARSE-ONLY** — logged once "parsed but not yet wired
(uzdoom@<sha>)"; genuinely-unknown gameinfo keys stay silently ignored as before.

Authoritative tracker: `ZXUnhandledGameInfoKeys[]` in `src/zandronum/src/gi.cpp`.

| key | uzdoom |
|---|---|
| usepausestring | c98042ed0 |
| cheatKey / easyKey | a1cc548af* |
| menuslidercolor | a1cc548af* |
| menusliderbackcolor | a2b8ad79e |
| statscreen_authorfont | 3e9921696 |
| statscreen_contentfont | 2fd170b06 |
| bloodsplatdecaldistance | 47f6f4cb1 |
| bluramount | a1cc548af* |
| forcenogfxsubstitution | ba13a540e |
| forcetextinmenus | 2874a36fb |

\* `a1cc548af` is the 2019 gamedata-reorg commit (the literal's earliest touch in the current path);
the true introduction predates it. See scratchpad/zmapinfo_port_trace.md §3g.
