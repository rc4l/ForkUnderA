// [Doruk-Aksoy] The engine side of TRNSLATE: lumps read, tables built and stored, names looked up (uzdoom@a505e910, uzdoom@0c04cddd, uzdoom@15d4fb8f).
#include "features/trnslate/trnslate.h"
#include "features/trnslate/computation/trnslate_compute.h"

#include "doomerrors.h"
#include "i_system.h"
#include "r_data/r_translate.h"
#include "tarray.h"
#include "w_wad.h"
#include "zstring.h"

#include <string>

using namespace zx::trnslate;

static TMap<FName, int> customTranslationMap;

int R_FindCustomTranslation(FName name)
{
	int player = 0;
	switch (ComputeTrnslateBuiltin(name.GetChars(), &player))
	{
	case kBuiltinNone:
		return 0;
	case kBuiltinIce:
		return TRANSLATION(TRANSLATION_Standard, 7);
	case kBuiltinPlayer:
		return TRANSLATION(TRANSLATION_Players, player);
	default:
		break;
	}
	int *found = customTranslationMap.CheckKey(name);
	return found != NULL ? *found : INVALID_TRANSLATION;
}

// [Doruk-Aksoy] The lump a quoted entry names, by full name and then as a short name, as UZDoom's parser finds it.
static int PaletteLumpNum(const std::string &name)
{
	return Wads.CheckNumForFullName(name.c_str(), true, ns_global);
}

static void TrnslateError(int lump, int line, const char *message)
{
	I_Error("Script error, \"%s\" line %d:\n%s\n", Wads.GetLumpFullName(lump), line, message);
}

static void AddPaletteLump(FRemapTable &table, const TrnslateEntry &entry)
{
	const int lump = PaletteLumpNum(entry.text);
	FMemLump data = Wads.ReadLump(lump);
	BYTE palette[kPaletteColors * 3];
	std::string error;
	const int count = ComputeReadPaletteLump((const BYTE *)data.GetMem(), Wads.LumpLength(lump), palette, &error);
	if (count < 0)
	{
		I_Error("%s: %s\n", Wads.GetLumpFullName(lump), error.c_str());
	}
	table.AddColors(entry.start, count, palette);
}

static void StoreDefinition(const TrnslateDef &def, int lump)
{
	FRemapTable table;
	if (def.baseKind == kBaseStandard)
	{
		table = *translationtables[TRANSLATION_Standard][def.baseStandard];
	}
	else if (def.baseKind == kBaseNamed)
	{
		const int base = R_FindCustomTranslation(def.baseName.c_str());
		if (base == INVALID_TRANSLATION)
		{
			FString message;
			message.Format("Base translation '%s' not found in '%s'", def.baseName.c_str(), def.name.c_str());
			TrnslateError(lump, def.line, message.GetChars());
		}
		FRemapTable *from = TranslationToTable(base);
		if (from != NULL) table = *from;
		else table.MakeIdentity();
	}
	else
	{
		table.MakeIdentity();
	}

	for (const TrnslateEntry &entry : def.entries)
	{
		if (entry.palette) AddPaletteLump(table, entry);
		else table.AddToTranslation(entry.text.c_str());
	}

	TAutoGrowArray<FRemapTablePtr, FRemapTable *> &slot = translationtables[TRANSLATION_Custom];
	if (slot.Size() >= MAX_DECORATE_TRANSLATIONS)
	{
		I_Error("Too many TRNSLATE translations");
	}
	FRemapTable *stored = new FRemapTable;
	*stored = table;
	customTranslationMap[FName(def.name.c_str())] = TRANSLATION(TRANSLATION_Custom, slot.Push(stored));
}

void R_ParseTrnslate()
{
	customTranslationMap.Clear();
	TAutoGrowArray<FRemapTablePtr, FRemapTable *> &slot = translationtables[TRANSLATION_Custom];
	for (unsigned int i = 0; i < slot.Size(); i++)
	{
		delete slot[i];
	}
	slot.Clear();

	int lump;
	int lastlump = 0;
	while ((lump = Wads.FindLump("TRNSLATE", &lastlump)) != -1)
	{
		FMemLump text = Wads.ReadLump(lump);
		const TrnslateParse parsed = ComputeParseTrnslate((const char *)text.GetMem(), Wads.LumpLength(lump),
			[](const std::string &name) { return PaletteLumpNum(name) >= 0; });
		for (const TrnslateDef &def : parsed.defs)
		{
			StoreDefinition(def, lump);
		}
		if (!parsed.error.empty())
		{
			TrnslateError(lump, parsed.errorLine, parsed.error.c_str());
		}
	}
}
