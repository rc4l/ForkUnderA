# join-screen

A loading screen while joining a server, in place of the bare full console. It shows the joined mod's title page (`gameinfo.TitlePage`) with a band along the bottom in its GAMEINFO `STARTUPCOLORS`. The band holds the `STARTUPTITLE`, a status line and a progress bar, the same title and colours the Win32 startup window uses.

| Stage | Shown while | Says |
|---|---|---|
| Loading files | a browser join is in flight but no connect has started (the frame JOIN is pressed in, which stays on screen through the WAD reload) | Loading files for *server* |
| Connecting | `CTS_ATTEMPTINGCONNECTION` | Connecting to *server* |
| Authenticating | `CTS_ATTEMPTINGAUTHENTICATION` | Verifying the map with *server* |
| Receiving | `CTS_REQUESTINGSNAPSHOT` / `CTS_RECEIVINGSNAPSHOT` | Receiving the game from *server* |

A refused or dropped connect is back to `CTS_DISCONNECTED`, so the console shows the reason. The console key swaps the screen for the console and back, and Escape still opens the menu over it. `fua_joinscreen 0` turns it off.

The reload itself cannot be drawn: the restart rebuilds textures and fonts before anything can use them. So the frame drawn as the join starts is what stays up meanwhile, since a restart is only thrown at the top of the next frame (`D_RunFrame`).

## Files

- `joinscreen.cpp` / `.h`: `JoinScreen_Draw`, `JoinScreen_TogglePeek` and `fua_joinscreen`.
- `computation/joinscreen_compute`: the stage, status line, bar fill and layout, tested.

## In-engine hooks

- `d_main.cpp` `D_Display`: the full-console branch draws the join screen instead of the console when it is up. The normal path draws it under the console and menu, which covers the frame a browser join starts in.
- `c_console.cpp` `C_ToggleConsole`: while the console cannot close mid-connect, the key toggles `JoinScreen_TogglePeek` instead.
- `features/server-browser/zx_joinserver.{h,cpp}`: `JoiningServerName`, the browser's name for the server being joined.
- `CMakeLists.txt`: `joinscreen.cpp` in `add_executable` (it defines a CVAR).
