# features/udmf-desaturation

A UDMF sector's `desaturation` is a fraction, 0 for none and 1 for fully grey. The loader multiplied it by
255 and kept whatever came out, so a map that wrote a percentage (WDI09's sector 655 says `99`) handed GL's
shader a desaturation factor of 99. `mix(texel, grey, 99)` runs 99 times past grey, and brown wood and red
blood came out bright blue. `ComputeUdmfDesaturation` holds the amount to 0..255, which is what GZDoom
does with it today, so that sector is drawn as the grey its author meant.

## In-engine hooks

- `p_udmf.cpp` `UDMFParser::ParseSector`, `NAME_Desaturation`: the key's value through `ComputeUdmfDesaturation`.
