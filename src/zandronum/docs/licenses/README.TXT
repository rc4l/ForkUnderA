ZandroX licensing
=================

ZandroX is distributed under the GNU General Public License v3.0. The full text
is at LICENSE.txt in the root of this repository, and it ships in every binary
download alongside THIRD-PARTY-NOTICES.txt, which lists the bundled libraries.

The Doom source
---------------
The original Doom source was released by id Software under the GPL v2. ZandroX
distributes it under the GPL v3, which is the same position the GZDoom project
takes for the same code.

That step deserves to be stated plainly rather than buried, because it is the
one part of this tree's licensing that is arguable rather than settled:

  - id's release specifies GPL v2. GPL v2 alone and GPL v3 are not compatible,
    and id did not attach an "or any later version" grant.
  - GPL v2 section 9 says that if a program "does not specify a version number
    of this License, you may choose any version ever published". id's source
    files name no GPL version at all -- they still carry the older DOOM Source
    Code License header -- which is the basis on which the move to v3 rests.
  - GZDoom has distributed this code as GPL v3 since 3.0.0 (2017) on that
    footing.

Anyone shipping a commercial product on this engine should have that reasoning
reviewed by a lawyer rather than relying on this file.

Why the file headers changed
----------------------------
74 source files in this tree carried the 1997 DOOM Source Code License header,
which permits educational use and forbids commercial distribution. id superseded
that licence by re-releasing the same code under the GPL, so the header was
stale, not accurate. Replacing it corrects the statement; it is not a claim of
ownership, and every copyright line was preserved.

id's own repository still has the same inconsistency: its LICENSE.TXT is the GPL
while its source files carry the older header. docs/doomlic.txt held a copy of
that superseded licence and was removed, because leaving a document asserting
"under no circumstances shall you commercially exploit the Software" next to a
GPL v3 claim is a contradiction with no reader-visible resolution.

Other components
----------------
Zandronum's own code       src/zandronum/LICENSE.txt
Skulltag's own code        src/zandronum/docs/Skulltag-license.txt
                           (BSD-3-clause style, plus a source-availability
                           clause; both cover source and binary form only)
Bundled libraries          THIRD-PARTY-NOTICES.txt in the repository root
Game data (the .pk3s)      THIRD-PARTY-NOTICES.txt, "Game data" section

Note in particular that brightmaps.pk3 is derived from commercial game artwork
and must not be shipped with a commercial standalone game, and that the artwork
in skulltag_actors.pk3 has no licence recorded upstream.
