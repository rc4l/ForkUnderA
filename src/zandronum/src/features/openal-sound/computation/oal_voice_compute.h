// [Doruk-Aksoy] Which playing voice a sound may take when every voice is busy: the order FindLowestChannel ranks them in, and the answer kept through a run of starts until a voice starts or stops.
#ifndef ZX_OAL_VOICE_COMPUTE_H
#define ZX_OAL_VOICE_COMPUTE_H

// [Doruk-Aksoy] Whether a voice of prio at squared distance dist ranks below the lowest found so far: a lower priority, or the same one farther away; a tie keeps the one met first.
bool ComputeVoiceLower(int prio, float dist, int lowestPrio, float lowestDist);

// [Doruk-Aksoy] The lowest voice as last found, kept while a run is open (depth) and until a voice changes (valid).
struct LowestVoiceMemo
{
	int depth = 0;
	bool valid = false;
	const void *lowest = nullptr;
};

// [Doruk-Aksoy] A run opens and closes, nesting; the outermost close forgets what was kept.
void ComputeMemoBegin(LowestVoiceMemo &m);
void ComputeMemoEnd(LowestVoiceMemo &m);

// [Doruk-Aksoy] The kept answer, true only inside a run with one kept, which can be no voice at all.
bool ComputeMemoRecall(const LowestVoiceMemo &m, const void *&lowest);

// [Doruk-Aksoy] Keeps an answer just found, inside a run only.
void ComputeMemoKeep(LowestVoiceMemo &m, const void *lowest);

// [Doruk-Aksoy] Forgets the kept answer, as a voice starting or stopping or a voice's distance changing makes it stale.
void ComputeMemoForget(LowestVoiceMemo &m);

#endif
