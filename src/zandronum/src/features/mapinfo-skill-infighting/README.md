# mapinfo-skill-infighting

Skill `NoInfighting` / `TotalInfighting` — per-skill infighting override.
- **Provenance:** uzdoom@1ad02a6ce · **Class:** PORTABLE
- **Hooks:** `g_level.h` (FSkillInfo::Infighting, SKILLP_Infight); `g_skill.cpp` (default 0, `noinfighting`/`totalinfighting` parse, G_SkillProperty 1/-1/0, operator=); `p_interaction.cpp` infight decision (skill overrides level/global, never MF5_NOINFIGHTING).

## Conformance
```
skill zxti { TotalInfighting }
skill zxni { NoInfighting }
```
