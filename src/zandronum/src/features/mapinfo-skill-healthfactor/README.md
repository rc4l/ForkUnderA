# mapinfo-skill-healthfactor

Skill `HealthFactor` — scales monster/friendly spawn health.
- **Provenance:** uzdoom@f7cdb28ea · **Class:** PORTABLE
- **Hooks:** `g_level.h` (FSkillInfo::HealthFactor, SKILLP_HealthFactor); `g_skill.cpp` (default FRACUNIT, `healthfactor` parse, G_SkillProperty, operator=); `p_mobj.cpp` AActor::GetSpawnHealth (scales on top of Monster/FriendlyHealth).

## Conformance
```
skill zxhf { HealthFactor = 2.0 }
```
Load a ZMAPINFO with this; expect no `Unknown property` warning and doubled monster health on skill `zxhf`.
