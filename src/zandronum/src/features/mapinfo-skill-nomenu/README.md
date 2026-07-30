# mapinfo-skill-nomenu

Skill `nomenu` — hide a skill from the skill-select menu.
- **Provenance:** uzdoom@80e9763d6 · **Class:** PORTABLE
- **Hooks:** `g_level.h` (FSkillInfo::NoMenu, SKILLP_NoMenu); `g_skill.cpp` (default false, `nomenu` parse, G_SkillProperty, operator=); `menu/menudef.cpp` M_StartupSkillMenu (skip in the build loop).

## Conformance
```
skill zxhidden { nomenu }
```
Expect the skill to parse but not appear in the New Game skill menu.
