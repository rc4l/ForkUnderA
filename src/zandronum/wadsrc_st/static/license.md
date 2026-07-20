ZandroX Skulltag actors

Copyright 2000-2007 Brad Carney
Copyright 2007-2012 Skulltag Development Team
Copyright 2012-2024 Zandronum Development Team

The licensing of the artwork in here is unresolved, which is why it is gated.

The Skulltag license (src/zandronum/docs/Skulltag-license.txt) closes with
"The above copyright and license notice applies to distributions of Skulltag in
source and binary form." It does not mention artwork, sprites or sounds, and no
license, artist credit or provenance statement for the graphics in this
directory exists anywhere in this repository or upstream.

That is not the same as the brightmaps, whose terms are known and restrictive.
Here the terms are simply unknown, and unknown is not a basis for shipping with
a commercial product.

Build with -DBUILD_NONFREE=OFF to omit skulltag_actors.pk3 entirely. The engine
runs without it; the actors it defines are looked up by class name and their
absence is already handled. One exception: instagib deathmatch needs the Railgun
class from this pk3 and will refuse to start without it.

Note that this pk3 is not the only place Skulltag-derived content lives.
wadsrc/static/actors/Skulltag/ contributes DECORATE definitions to the main
zandronum.pk3. Those are definition text rather than artwork, so they are not
gated here, but the provenance question has not been settled for them either.
