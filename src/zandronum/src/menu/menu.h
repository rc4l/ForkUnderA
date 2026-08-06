#ifndef __M_MENU_MENU_H__
#define __M_MENU_MENU_H__




#include "dobject.h"
#include "d_player.h"
#include "r_data/r_translate.h"
#include "c_cvars.h"
#include "v_font.h"
#include "version.h"
#include "textures/textures.h"

EXTERN_CVAR(Float, snd_menuvolume)
EXTERN_CVAR(Int, m_use_mouse);


struct event_t;
class FTexture;
class FFont;
enum EColorRange;
class FPlayerClass;
class FKeyBindings;

enum EMenuKey
{
	MKEY_Up,
	MKEY_Down,
	MKEY_Left,
	MKEY_Right,
	MKEY_PageUp,
	MKEY_PageDown,
	//----------------- Keys past here do not repeat.
	MKEY_Enter,
	MKEY_Back,		// Back to previous menu
	MKEY_Clear,		// Clear keybinding/flip player sprite preview
	NUM_MKEYS,

	// These are not buttons but events sent from other menus 

	MKEY_Input,		// Sent when input is confirmed
	MKEY_Abort,		// Input aborted
	MKEY_MBYes,
	MKEY_MBNo,
};

enum EGravity
{
	GRAV_CENTER_HORIZONTAL = 1,
	GRAV_CENTER_VERTICAL = 2,
	GRAV_LEFT = 4,
	GRAV_RIGHT = 8,
	GRAV_TOP = 16,
	GRAV_BOTTOM = 32,
};


struct FGameStartup
{
	const char *PlayerClass;
	int Episode;
	int Skill;
};

extern FGameStartup GameStartupInfo;

struct FSaveGameNode
{
	char Title[SAVESTRINGSIZE];
	FString Filename;
	bool bOldVersion;
	bool bMissingWads;
	bool bNoDelete;

	FSaveGameNode() { bNoDelete = false; }
};

void M_DrawConText(int color, int x, int y, const char* str);

//=============================================================================
//
// menu descriptor. This is created from the menu definition lump
// Items must be inserted in the order they are cycled through with the cursor
//
//=============================================================================

enum EMenuDescriptorType
{
	MDESC_ListMenu,
	MDESC_FreeformMenu,
	MDESC_OptionsMenu,
};

struct FMenuDescriptor
{
	FName mMenuName;
	FString mNetgameMessage;
	int mType;
	const PClass *mClass;

	virtual ~FMenuDescriptor() {}
};

class FListMenuItem;
class FFreeformMenuItem;
class FOptionMenuItem;

struct FListMenuDescriptor : public FMenuDescriptor
{
	TDeletingArray<FListMenuItem *> mItems;
	int mSelectedItem;
	int mSelectOfsX;
	int mSelectOfsY;
	FTextureID mSelector;
	int mDisplayTop;
	int mXpos, mYpos;
	int mWLeft, mWRight;
	int mLinespacing;	// needs to be stored for dynamically created menus
	int mAutoselect;	// this can only be set by internal menu creation functions
	FFont *mFont;
	EColorRange mFontColor;
	EColorRange mFontColor2;
	FMenuDescriptor *mRedirect;	// used to redirect overlong skill and episode menus to option menu based alternatives
	bool mCenter;

	void Reset()
	{
		// Reset the default settings (ignore all other values in the struct)
		mSelectOfsX = 0;
		mSelectOfsY = 0;
		mSelector.SetInvalid();
		mDisplayTop = 0;
		mXpos = 0;
		mYpos = 0;
		mLinespacing = 0;
		mNetgameMessage = "";
		mFont = NULL;
		mFontColor = CR_UNTRANSLATED;
		mFontColor2 = CR_UNTRANSLATED;
	}
};

struct FFreeformMenuDescriptor : public FMenuDescriptor
{
	TDeletingArray<FFreeformMenuItem*> mItems;
	int mDefaultSelection;
	int mSelectedItem;
	int mScrollPos;
	int mScrollSpeed;
	int mTopPadding;
	int mHeightOverride;
	int mAutoselect;	// this can only be set by internal menu creation functions
	bool mShowBackButton;
	bool mCenter;
	bool mDontDim;
	bool mResetScroll;
	bool mNetgameOnly; // [TP]

	// [BB] The default constructor initializes our custom members.
	FFreeformMenuDescriptor() : mNetgameOnly(false) {}

	FFreeformMenuItem* GetItem(FName name);
	bool DeleteItem(FName name);
	void Reset()
	{
		// Reset the default settings (ignore all other values in the struct)
		mTopPadding = 0;
		mDontDim = false;
		mResetScroll = false;
		mNetgameOnly = false; // [TP]
	}

};

struct FOptionMenuSettings
{
	EColorRange mTitleColor;
	EColorRange mFontColor;
	EColorRange mFontColorValue;
	EColorRange mFontColorMore;
	EColorRange mFontColorHeader;
	EColorRange mFontColorHighlight;
	EColorRange mFontColorSelection;
	int mLinespacing;
};

struct FOptionMenuDescriptor : public FMenuDescriptor
{
	TDeletingArray<FOptionMenuItem *> mItems;
	FString mTitle;
	int mSelectedItem;
	int mDrawTop;
	int mScrollTop;
	int mScrollPos;
	int mIndent;
	int mPosition;
	bool mDontDim;
	bool mNetgameOnly; // [TP]
	bool mRequiresRCON; // [AK]

	// [BB] The default constructor initializes our custom members.
	FOptionMenuDescriptor ( ) : mNetgameOnly ( false ), mRequiresRCON ( false ) {}

	void CalcIndent();
	FOptionMenuItem *GetItem(FName name);
	void Reset()
	{
		// Reset the default settings (ignore all other values in the struct)
		mPosition = 0;
		mScrollTop = 0;
		mIndent = 0;
		mDontDim = 0;
		mNetgameOnly = false; // [TP]
		mRequiresRCON = false; // [AK]
	}

};
						

typedef TMap<FName, FMenuDescriptor *> MenuDescriptorList;

extern FOptionMenuSettings OptionSettings;
extern MenuDescriptorList MenuDescriptors;

#define CURSORSPACE (14 * CleanXfac_1)

//=============================================================================
//
//
//
//=============================================================================

struct FMenuRect
{
	int x, y;
	int width, height;

	void set(int _x, int _y, int _w, int _h)
	{
		x = _x;
		y = _y;
		width = _w;
		height = _h;
	}

	bool inside(int _x, int _y)
	{
		return _x >= x && _x < x+width && _y >= y && _y < y+height;
	}

};


class DMenu : public DObject
{
	DECLARE_CLASS (DMenu, DObject)
	HAS_OBJECT_POINTERS

protected:
	bool mMouseCapture;
	bool mBackbuttonSelected;

public:
	enum
	{
		MOUSE_Click,
		MOUSE_Move,
		MOUSE_Release,
		MOUSE_Click2,
		MOUSE_Release2
	};

	enum
	{
		BACKBUTTON_TIME = 4*TICRATE
	};

	static DMenu *CurrentMenu;
	static int MenuTime;

	TObjPtr<DMenu> mParentMenu;

	DMenu(DMenu *parent = NULL);
	virtual bool Responder (event_t *ev);
	virtual bool MenuEvent (int mkey, bool fromcontroller);
	virtual void Ticker ();
	virtual void Drawer ();
	virtual bool DimAllowed ();
	virtual bool TranslateKeyboardEvents();
	virtual void Close();
	virtual bool MouseEvent(int type, int x, int y);
	virtual bool MouseEventBack(int type, int x, int y);
	void SetCapture();
	void ReleaseCapture();
	bool HasCapture()
	{
		return mMouseCapture;
	}

	virtual void CVarChanged ( FBaseCVar* ) {} // [TP]
};

//=============================================================================
//
// base class for menu items
//
//=============================================================================

class FListMenuItem
{
protected:
	int mXpos, mYpos;
	FName mAction;

public:
	bool mEnabled;

	FListMenuItem(int xpos = 0, int ypos = 0, FName action = NAME_None)
	{
		mXpos = xpos;
		mYpos = ypos;
		mAction = action;
		mEnabled = true;
	}

	virtual ~FListMenuItem();

	virtual bool CheckCoordinate(int x, int y);
	virtual void Ticker();
	virtual void Drawer(bool selected);
	virtual bool Selectable();
	virtual bool Activate();
	virtual FName GetAction(int *pparam);
	virtual bool SetString(int i, const char *s);
	virtual bool GetString(int i, char *s, int len);
	virtual bool SetValue(int i, int value);
	virtual bool GetValue(int i, int *pvalue);
	virtual void Enable(bool on);
	virtual bool MenuEvent (int mkey, bool fromcontroller);
	virtual bool MouseEvent(int type, int x, int y);
	virtual bool CheckHotkey(int c);
	virtual int GetWidth();
	// [rc4l] Where the item actually PAINTS, which is not always where it is positioned.
	//
	// DrawTexture honours a patch's own offsets, so a StaticPatch lands at (x - leftoffset,
	// y - topoffset), not at (x, y). Freedoom's M_DOOM is offset (13,-16): given `StaticPatch 94, 2`
	// it paints at (81, 18) -- sixteen rows lower and thirteen left of its stated position. Layout
	// code that measures the stated position therefore computes a rectangle the content does not sit
	// in: too high (the panel ran off the top of the screen and got clamped to the edge) and skewed
	// right (the rows looked off-centre inside it).
	//
	// Text reports its own position unchanged, so the base is the identity.
	virtual int GetDrawnX() { return mXpos; }
	virtual int GetDrawnY() { return mYpos; }
	// [rc4l] How tall the item's own pixels are, or 0 when it cannot say -- the caller then falls
	// back to the descriptor's linespacing. Layout that pads below the LINE BOX rather than below the
	// glyphs leaves the leftover leading as extra gap, so a panel ends up with more space under its
	// last row than above its first: the same class of error as measuring the stated position instead
	// of the drawn one.
	virtual int GetDrawnHeight() { return 0; }
	void DrawSelector(int xofs, int yofs, FTextureID tex);
	void OffsetPositionY(int ydelta) { mYpos += ydelta; }
	int GetY() { return mYpos; }
	int GetX() { return mXpos; }
	void SetX(int x) { mXpos = x; }
	void SetY(int y) { mYpos = y; }
	void SetAction(FName action) { mAction = action; }
};

class FListMenuItemStaticPatch : public FListMenuItem
{
protected:
	FTextureID mTexture;
	bool mCentered;

public:
	FListMenuItemStaticPatch(int x, int y, FTextureID patch, bool centered);
	void Drawer(bool selected);
	int GetWidth();	// [rc4l] so layout code (e.g. FUAPanelListMenu) can measure the logo
	// [rc4l] Corrected for the patch's own offsets -- see the base declarations.
	int GetDrawnX();
	int GetDrawnY();
	int GetDrawnHeight();
};

class FListMenuItemStaticText : public FListMenuItem
{
protected:
	const char *mText;
	FFont *mFont;
	EColorRange mColor;
	bool mCentered;

public:
	FListMenuItemStaticText(int x, int y, const char *text, FFont *font, EColorRange color, bool centered);
	~FListMenuItemStaticText();
	void Drawer(bool selected);
};

//=============================================================================
//
// the player sprite window
//
//=============================================================================

class FListMenuItemPlayerDisplay : public FListMenuItem
{
	FListMenuDescriptor *mOwner;
	FTexture *mBackdrop;
	FRemapTable mRemap;
	FPlayerClass *mPlayerClass;
	FState *mPlayerState;
	int mPlayerTics;
	bool mNoportrait;
	BYTE mRotation;
	BYTE mMode;	// 0: automatic (used by class selection), 1: manual (used by player setup)
	BYTE mTranslate;
	int mSkin;
	int mRandomClass;
	int mRandomTimer;
	int mClassNum;

	void SetPlayerClass(int classnum, bool force = false);
	bool UpdatePlayerClass();
	void UpdateRandomClass();
	void UpdateTranslation();

public:

	enum
	{
		PDF_ROTATION = 0x10001,
		PDF_SKIN = 0x10002,
		PDF_CLASS = 0x10003,
		PDF_MODE = 0x10004,
		PDF_TRANSLATE = 0x10005,
	};

	FListMenuItemPlayerDisplay(FListMenuDescriptor *menu, int x, int y, PalEntry c1, PalEntry c2, bool np, FName action);
	~FListMenuItemPlayerDisplay();
	virtual void Ticker();
	virtual void Drawer(bool selected);
	bool SetValue(int i, int value);
};


//=============================================================================
//
// selectable items
//
//=============================================================================

class FListMenuItemSelectable : public FListMenuItem
{
protected:
	int mHotkey;
	int mHeight;
	int mParam;

public:
	FListMenuItemSelectable(int x, int y, int height, FName childmenu, int mParam = -1);
	bool CheckCoordinate(int x, int y);
	bool Selectable();
	bool CheckHotkey(int c);
	bool Activate();
	bool MouseEvent(int type, int x, int y);
	FName GetAction(int *pparam);
};

class FListMenuItemText : public FListMenuItemSelectable
{
	const char *mText;
	FFont *mFont;
	EColorRange mColor;
	EColorRange mColorSelected;
public:
	FListMenuItemText(int x, int y, int height, int hotkey, const char *text, FFont *font, EColorRange color, EColorRange color2, FName child, int param = 0);
	~FListMenuItemText();
	void Drawer(bool selected);
	int GetWidth();
	int GetDrawnHeight();	// [rc4l] the font's glyph height, not the row's line box
};

class FListMenuItemPatch : public FListMenuItemSelectable
{
	FTextureID mTexture;
public:
	FListMenuItemPatch(int x, int y, int height, int hotkey, FTextureID patch, FName child, int param = 0);
	void Drawer(bool selected);
	int GetWidth();
	// [rc4l] Corrected for the patch's own offsets, same as the static variant.
	int GetDrawnX();
	int GetDrawnY();
	int GetDrawnHeight();
};

//=============================================================================
//
// items for the player menu
//
//=============================================================================

class FPlayerNameBox : public FListMenuItemSelectable
{
	const char *mText;
	FFont *mFont;
	EColorRange mFontColor;
	int mFrameSize;
	// [AK] Increased the size to MAXPLAYERNAMEBUFFER.
	char mPlayerName[MAXPLAYERNAMEBUFFER+1];
	char mEditName[MAXPLAYERNAMEBUFFER+2];
	bool mEntering;
	int mNameboxWidth; // [AK]

	void DrawBorder (int x, int y, int len);

public:

	FPlayerNameBox(int x, int y, int height, int frameofs, const char *text, FFont *font, EColorRange color, FName action);
	~FPlayerNameBox();
	bool SetString(int i, const char *s);
	bool GetString(int i, char *s, int len);
	void Drawer(bool selected);
	bool MenuEvent (int mkey, bool fromcontroller);
};

//=============================================================================
//
// items for the player menu
//
//=============================================================================

class FValueTextItem : public FListMenuItemSelectable
{
	TArray<FString> mSelections;
	const char *mText;
	int mSelection;
	FFont *mFont;
	EColorRange mFontColor;
	EColorRange mFontColor2;

public:

	FValueTextItem(int x, int y, int height, const char *text, FFont *font, EColorRange color, EColorRange valuecolor, FName action, FName values);
	~FValueTextItem();
	bool SetString(int i, const char *s);
	bool SetValue(int i, int value);
	bool GetValue(int i, int *pvalue);
	bool MenuEvent (int mkey, bool fromcontroller);
	void Drawer(bool selected);
};

//=============================================================================
//
// items for the player menu
//
//=============================================================================

class FSliderItem : public FListMenuItemSelectable
{
	const char *mText;
	FFont *mFont;
	EColorRange mFontColor;
	int mMinrange, mMaxrange;
	int mStep;
	int mSelection;

	void DrawSlider (int x, int y);

public:

	FSliderItem(int x, int y, int height, const char *text, FFont *font, EColorRange color, FName action, int min, int max, int step);
	~FSliderItem();
	bool SetValue(int i, int value);
	bool GetValue(int i, int *pvalue);
	bool MenuEvent (int mkey, bool fromcontroller);
	void Drawer(bool selected);
	bool MouseEvent(int type, int x, int y);
};

//=============================================================================
//
// list menu class runs a menu described by a FListMenuDescriptor
//
//=============================================================================

class DListMenu : public DMenu
{
	DECLARE_CLASS(DListMenu, DMenu)

protected:
	FListMenuDescriptor *mDesc;
	FListMenuItem *mFocusControl;

	// [rc4l] Update-notice ("update available" chip) state, on the BASE class deliberately.
	//
	// It lived on a DListMenu subclass wired via `Class "UpdateMainMenu"` in menudef, which silently
	// stopped every mod from replacing the main menu: ReplaceMenu() rejects an override whose class
	// does not match the existing descriptor's, and mods declare no class. Keeping the notice here
	// means the stock MainMenu descriptor needs no class at all, so it stays overridable, AND any
	// list menu that ends up as the main menu shows the notice -- there is no class left to get
	// wrong. Everything below is inert unless this menu IS the main menu and an update is pending.
	bool mNoticeFocused;
	int mNoticePrevSelected;   // list item selected before the chip took focus, restored on exit
	int mNoticeLastMouseX, mNoticeLastMouseY; // last pointer position, so a parked cursor can't fight the keyboard
	int mNoticeL, mNoticeT, mNoticeR, mNoticeB; // chip rect in screen pixels, cached for hit-testing

	bool NoticeApplies() const;      // this is the main menu and an update is pending
	void NoticeFocusChip();          // focus the chip, remembering (and clearing) the list selection
	void NoticeActivate();           // open the download confirmation
	void NoticeDrawer();             // draw the chip (call after the list is drawn)
	bool NoticeMenuEvent(int mkey, bool fromcontroller, bool &handled);
	bool NoticeMouseEvent(int type, int x, int y, bool &handled);

public:
	DListMenu(DMenu *parent = NULL, FListMenuDescriptor *desc = NULL);
	virtual void Init(DMenu *parent = NULL, FListMenuDescriptor *desc = NULL);
	FListMenuItem *GetItem(FName name);
	bool Responder (event_t *ev);
	bool MenuEvent (int mkey, bool fromcontroller);
	bool MouseEvent(int type, int x, int y);
	void Ticker ();
	void Drawer ();
	void SetFocus(FListMenuItem *fc)
	{
		mFocusControl = fc;
	}
	bool CheckFocus(FListMenuItem *fc)
	{
		return mFocusControl == fc;
	}
	void ReleaseFocus()
	{
		mFocusControl = NULL;
	}
};


//=============================================================================
//
// Freeform menu class runs a menu described by a FFreeformMenuDescriptor
//
//=============================================================================

class DFreeformMenu : public DMenu
{
	DECLARE_CLASS(DFreeformMenu, DMenu)

	int CenteredOffset;
	int LowestScroll;
	bool CanScrollUp;
	bool CanScrollDown;
	FFreeformMenuItem* mFocusControl;

protected:
	FFreeformMenuDescriptor* mDesc;

public:
	FFreeformMenuItem* GetItem(FName name);
	DFreeformMenu(DMenu* parent = NULL, FFreeformMenuDescriptor* desc = NULL);
	virtual void Init(DMenu* parent = NULL, FFreeformMenuDescriptor* desc = NULL);
	int FirstSelectable();
	bool Responder(event_t* ev);
	bool MenuEvent(int mkey, bool fromcontroller);
	bool MouseEvent(int type, int x, int y);
	bool MouseEventBack(int type, int x, int y);
	void Ticker();
	void Drawer();
	const FFreeformMenuDescriptor* GetDescriptor() const { return mDesc; }
	void SetFocus(FFreeformMenuItem* fc)
	{
		mFocusControl = fc;
	}
	bool CheckFocus(FFreeformMenuItem* fc)
	{
		return mFocusControl == fc;
	}
	void ReleaseFocus()
	{
		mFocusControl = NULL;
	}
	bool DimAllowed()
	{
		return !mDesc->mDontDim;
	}
};

//=============================================================================
//
// base class for menu items
//
//=============================================================================

class FFreeformMenuItem : public FListMenuItem
{
protected:
	bool mIsStatic;
	bool mIsVisibleOnTitlemap;
	bool mIsVisibleInSingleplayer;
	bool mIsVisibleInBotplay;
	bool mIsVisibleInMultiplayer;
	int mWidth, mHeight;
	int mMouseAreaWidth, mMouseAreaHeight;
	int mXPadding, mYPadding;
	int mGravity;
	int mAnchor;
	fixed_t mAlpha;
	FBaseCVar* mVisibilityCVar;
	int mVisibilityCVarMin, mVisibilityCVarMax;
	int mBelowItemsExtraOffset;
public:

	FFreeformMenuItem(FName action) : FListMenuItem(0, 0, action)
	{
		mIsStatic = false;
		mIsVisibleOnTitlemap = true;
		mIsVisibleInSingleplayer = true;
		mIsVisibleInBotplay = true;
		mIsVisibleInMultiplayer = true;
		mWidth = 0;
		mHeight = 0;
		mMouseAreaWidth = -1;
		mMouseAreaHeight = -1;
		mXPadding = 0;
		mYPadding = 0;
		mGravity = GRAV_CENTER_HORIZONTAL | GRAV_TOP;
		mAnchor = GRAV_CENTER_HORIZONTAL | GRAV_TOP;
		mAlpha = FRACUNIT;
		mVisibilityCVar = NULL;
		mVisibilityCVarMin = 1;
		mVisibilityCVarMax = INT_MAX;
		mBelowItemsExtraOffset = 0;
	}

	~FFreeformMenuItem() {};

	virtual FFreeformMenuItem* Clone();
	virtual bool CopyTo(FFreeformMenuItem* other);
	virtual bool CheckCoordinate(int x, int y);
	virtual void Draw(FFreeformMenuDescriptor* desc, int yoffset, bool selected);
	virtual bool IsEnabled();
	virtual bool Selectable();

	void SetWidth(int width)	 { mWidth = width;		}	int GetWidth()	{ return mWidth;	}
	void SetHeight(int height)	 { mHeight = height;	}	int GetHeight()	{ return mHeight;	}
	void SetGravity(int gravity) { mGravity = gravity;	}
	void SetAnchor(int anchor)	 { mAnchor = anchor;	}	int GetAnchor()	{ return mAnchor;	}
	void SetAlpha(fixed_t alpha) { mAlpha = alpha;		}
	void SetStatic(bool isStatic){ mIsStatic = isStatic;}	bool IsStatic()	{ return mIsStatic;	}

	void SetVisibleOnTitlemap(bool visible)		{ mIsVisibleOnTitlemap = visible;		}	bool IsVisibleOnTitlemap()		{ return mIsVisibleOnTitlemap;		}
	void SetVisibleInSingleplayer(bool visible)	{ mIsVisibleInSingleplayer = visible;	}	bool IsVisibleInSingleplayer()	{ return mIsVisibleInSingleplayer;	}
	void SetVisibleInBotplay(bool visible)		{ mIsVisibleInBotplay = visible;		}	bool IsVisibleInBotplay()		{ return mIsVisibleInBotplay;		}
	void SetVisibleInMultiplayer(bool visible)	{ mIsVisibleInMultiplayer = visible;	}	bool IsVisibleInMultiplayer()	{ return mIsVisibleInMultiplayer;	}

	void SetMouseArea(int width, int height) { mMouseAreaWidth = width; mMouseAreaHeight = height; }
	int GetMouseAreaHeight() { return mMouseAreaHeight >= 0 ? mMouseAreaHeight : GetHeight(); }
	void SetPadding(int xPadding, int yPadding) { mXPadding = xPadding; mYPadding = yPadding; }
	void SetVisibilityCVar(const char* visiblecheck, int min, int max)
	{
		mVisibilityCVar = FindCVar(visiblecheck, NULL);
		mVisibilityCVarMin = min;
		mVisibilityCVarMax = max;
		if (mVisibilityCVarMax < mVisibilityCVarMin)
			swapvalues(mVisibilityCVarMin, mVisibilityCVarMax);
	}

	void SetBelowItemsExtraOffset(int offset) { mBelowItemsExtraOffset = offset; }
	int GetBelowItemsExtraOffset() { return mBelowItemsExtraOffset; }
	void CalcDrawScreenPos(int &x, int &y, int width, int height, bool withPadding);
	virtual bool MouseEvent(int type, int x, int y);

	virtual bool IsServerInfo() const { return false; }
	bool IsVisible() const;
};


//=============================================================================
//
// base class for menu items
//
//=============================================================================

class FOptionMenuItem : public FListMenuItem
{
protected:
	char *mLabel;
	bool mCentered;
	bool mDisabled; // [TP]

	void drawLabel(int indent, int y, EColorRange color, bool grayed = false);
public:

	FOptionMenuItem(const char *text, FName action = NAME_None, bool center = false)
		: FListMenuItem(0, 0, action)
	{
		mLabel = copystring(text);
		mCentered = center;
		mDisabled = false; // [TP]
	}

	~FOptionMenuItem();
	const char *GetLabel() const { return mLabel; } // [rc4l] for measuring text extents (zx_consentmenu.cpp)
	virtual int Draw(FOptionMenuDescriptor *desc, int y, int indent, bool selected);
	virtual bool Selectable();
	virtual int GetIndent();
	virtual bool MouseEvent(int type, int x, int y);

	// [TP]
	virtual bool IsServerCVar() const { return false; }
	virtual bool IsDisabled() const;
	void SetDisabled ( bool a ) { mDisabled = a; }
};	

//=============================================================================
//
//
//
//=============================================================================
struct FOptionValues
{
	struct Pair
	{
		double Value;
		FString TextValue;
		FString Text;
	};

	TArray<Pair> mValues;
};

typedef TMap< FName, FOptionValues* > FOptionMap;

extern FOptionMap OptionValues;


//=============================================================================
//
// Option menu class runs a menu described by a FOptionMenuDescriptor
//
//=============================================================================

class DOptionMenu : public DMenu
{
	DECLARE_CLASS(DOptionMenu, DMenu)

	bool CanScrollUp;
	bool CanScrollDown;
	int VisBottom;
	FOptionMenuItem *mFocusControl;

protected:
	FOptionMenuDescriptor *mDesc;

public:
	FOptionMenuItem *GetItem(FName name);
	DOptionMenu(DMenu *parent = NULL, FOptionMenuDescriptor *desc = NULL);
	virtual void Init(DMenu *parent = NULL, FOptionMenuDescriptor *desc = NULL);
	int FirstSelectable();
	bool Responder (event_t *ev);
	bool MenuEvent (int mkey, bool fromcontroller);
	bool MouseEvent(int type, int x, int y);
	void Ticker ();
	void Drawer ();
	const FOptionMenuDescriptor *GetDescriptor() const { return mDesc; }
	void SetFocus(FOptionMenuItem *fc)
	{
		mFocusControl = fc;
	}
	bool CheckFocus(FOptionMenuItem *fc)
	{
		return mFocusControl == fc;
	}
	void ReleaseFocus()
	{
		mFocusControl = NULL;
	}
};


//=============================================================================
//
// Input some text
//
//=============================================================================

class DTextEnterMenu : public DMenu
{
	DECLARE_ABSTRACT_CLASS(DTextEnterMenu, DMenu)

	char *mEnterString;
	unsigned int mEnterSize;
	unsigned int mEnterPos;
	int mSizeMode; // 1: size is length in chars. 2: also check string width
	bool mInputGridOkay;

	int InputGridX;
	int InputGridY;

	// [TP]
	bool AllowColors;

public:

	// [TP] Added allowcolors
	DTextEnterMenu(DMenu *parent, char *textbuffer, int maxlen, int sizemode, bool showgrid, bool allowcolors = false);

	void Drawer ();
	bool MenuEvent (int mkey, bool fromcontroller);
	bool Responder(event_t *ev);
	bool TranslateKeyboardEvents();
	bool MouseEvent(int type, int x, int y);

};


//=============================================================================
//
// This class is used to capture the key to be used as the new key binding
// for a control item
//
//=============================================================================

class DEnterKey : public DMenu
{
	DECLARE_ABSTRACT_CLASS(DEnterKey, DMenu)

	int *pKey;

public:
	DEnterKey(DMenu *parent, int *keyptr);

	bool TranslateKeyboardEvents();
	void SetMenuMessage(int which);
	bool Responder(event_t* ev);
	void Drawer();
};


struct event_t;
void M_EnableMenu (bool on) ;
bool M_Responder (event_t *ev);
void M_Ticker (void);
void M_Drawer (void);
void M_Init (void);
void M_CreateMenus();
void M_ActivateMenu(DMenu *menu);
void M_ClearMenus ();
void M_ParseMenuDefs();
void M_StartupSkillMenu(FGameStartup *gs);
int M_GetDefaultSkill();
void M_StartControlPanel (bool makeSound);

// [rc4l] Release every latched menu key. Call it when something takes the keyboard away from the
// menu's own translation -- see the definition for what goes wrong otherwise.
void M_ReleaseMenuButtons ();
void M_SetMenu(FName menu, int param = -1);
void M_NotifyNewSave (const char *file, const char *title, bool okForQuicksave);
void M_StartMessage(const char *message, int messagemode, FName action = NAME_None);
// [rc4l] Pop a yes/no dialog that opens `url` in the browser on Yes. The dialog text ALWAYS shows the
// full URL (this builds it — no caller can substitute hidden text), and non-http/https URLs are
// refused outright. This is the only sanctioned way to reach I_OpenURL from menudef/CCMD/a mod.
void M_ConfirmOpenURL(const char *url);
// [rc4l] Same dialog, but for a GitHub release `tag`: opens the download for the running OS on Yes.
void M_ConfirmDownloadRelease(const char *tag);
DMenu *StartPickerMenu(DMenu *parent, const char *name, FColorCVar *cvar);
void M_RefreshModesList ();
void M_InitVideoModesMenu ();
void M_RconAccessGranted();
void M_SetLastRconAccessRequest(int tic); // [AK]
bool M_InServerSetupMenu(); // [AK]
bool M_IsValidMenu(const char *name); // [AK]



#endif