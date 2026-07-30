# mapinfo-parseonly-render

UZDoom MAPINFO **map** properties ZandroX recognises but does not act on because they drive GL/Vulkan
renderer state (fog, sky layers, lighting/tonemap, shadowmap, ambient occlusion) absent from this
base. **Class: PARSE-ONLY** — accepted and skipped cleanly (no parse error), logged once as
"parsed but not yet wired (uzdoom@<sha>)". Values could be wired later if the renderer feature lands.

Authoritative per-keyword provenance is the `ZXUnhandledMapKeys[]` manifest in
`src/zandronum/src/g_mapinfo.cpp` (keyword → uzdoom SHA), consulted at the map "Unknown property"
fallback via `ZX_ReportUnhandledMapInfo()`.

Keywords: attenuatelights, compat_emulatemikoportals, disableshadowmap/enableshadowmap,
disableskyboxao/enableskyboxao, noambientocclusion, forcefakecontrast, nocoloredspritelighting,
nofogofwar, nolightfade, useskymist, brightfog, fogdensity, outsidefogdensity, skyfog, skymist,
skymistyscale, skyrotate, skyrotate2, lightadditivesurfaces, lightblendmode, lightmode,
notexturefill, thickfogdistance, thickfogmultiplier.
