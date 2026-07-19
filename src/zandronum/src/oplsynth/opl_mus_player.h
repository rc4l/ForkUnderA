#include "critsec.h"
#include "musicblock.h"
#include "doomtype.h"

class OPLmusicBlock : public musicBlock
{
public:
	OPLmusicBlock();
	virtual ~OPLmusicBlock();

	bool ServiceStream(void *buff, int numbytes);
	void ResetChips();

	virtual void Restart();

protected:
	virtual int PlayTick() = 0;
	void OffsetSamples(float *buff, int count);

	// [rc4l] score/scoredata/playingcount lived in the old muslib musicBlock; the clean
	// musicblock.h does not carry them, so the raw-OPL player owns them here now.
	BYTE *score;
	BYTE *scoredata;
	double NextTickIn;
	double SamplesPerTick;
	int NumChips;
	int playingcount;
	bool Looping;
	double LastOffset;
	bool FullPan;

	FCriticalSection ChipAccess;
};

class OPLmusicFile : public OPLmusicBlock
{
public:
	OPLmusicFile(FILE *file, BYTE *musiccache, int len);
	OPLmusicFile(const OPLmusicFile *source, const char *filename);
	virtual ~OPLmusicFile();

	bool IsValid() const;
	void SetLooping(bool loop);
	void Restart();
	void Dump();

protected:
	OPLmusicFile() {}
	int PlayTick();

	enum { RDosPlay, IMF, DosBox1, DosBox2 } RawPlayer;
	int ScoreLen;
	int WhichChip;
};
