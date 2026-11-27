/* The HUD split's state (profile .hud: Mario Kart's race HUD, Super Mario
 * Advance's status bar, Advance Wars' conversations...) and the fights'
 * picture (profile .combat), in one struct for the libretro core (discussion
 * #75): gathered out of game.c's file statics unchanged, the fields keeping
 * their names. The SDL textures they end up in stay the app's. */
#ifndef HUD_H
#define HUD_H
#include "emu.h"
#include "profiles.h"
#include <stdbool.h>
#include <stdint.h>

#define HUD_CW (GBA_W * 2)
#define HUD_CH (GBA_H * 2)

/* the HUD's pieces as last seen (hud.overMap): the pixels inside each
 * move's box, cut to what's there (0: nothing); latched pieces wait for
 * a new value at their latch address, then for the piece to hold still */
typedef struct {
	bool valid;
	int w, h;
	uint32_t px[GBA_W * GBA_H];
	uint32_t key;
	int cw, ch, cx, cy, still; /* a latched piece's size and place (a settling one's size), and for how long */
	uint32_t cand[GBA_W * GBA_H]; /* a settling piece's pixels */
	int gone; /* frames missing while the HUD's on (HudMove.dropAfter) */
	bool stale; /* a state was loaded: taken again (latched or not) as soon as it holds still */
	int sx, sy; /* where it was on the game screen (its top left) */
	bool atBottom; /* it reached the game screen's bottom edge */
} Piece;

/* What the app does for the HUD split (set once per game): put the HUD's
 * canvas (HUD_CW x HUD_CH) or the fights' picture (sec.mapW x sec.mapH) on the
 * second screen, whether it has somewhere to put the fights' picture, and the
 * debug dump of the backdrop taken (GBADS_DEBUG) */
typedef struct {
	void (*presentHud)(const uint32_t* canvas);
	void (*presentCombat)(const uint32_t* px);
	bool (*hasCombat)(void);
	void (*saveBmp)(const uint32_t* px, int w, int hgt, const char* path);
} HudHooks;

typedef struct {
	/* profile.combat: in a fight, the second screen is the map's background
	 * with the logo small and dimmed (combatTex, mapW x mapH, made once) */
	const MapProfile* combatProf;
	bool combatOn, combatMade;
	uint32_t* combatPx; /* its pixels */
	bool combatInGame; /* (hud.logoUnder) the HUD's first probe matches: in a chapter */
	/* Mario Kart: the race HUD on the second screen (profile .hud) */
	const MapProfile* hudProf;
	uint32_t hudPx[GBA_W * GBA_H];
	bool hudOn; /* the frame being emulated is split */
	unsigned long long onWithAt; /* hud.onWith: when a sprite like it last showed */
	uint32_t hudCanvas[HUD_CW * HUD_CH], hudBack[HUD_CW * HUD_CH], hudGrab[GBA_W * GBA_H];
	uint32_t hudHeld[HUD_CW * HUD_CH]; /* hud.alt.rows: the picture from before the other layer came on */
	bool hudHeldOk;
	bool hudBackValid;
	int hudFrames; /* split frames since the race (re)started */
	int mapX0, mapY0, mapX1, mapY1; /* the map's box on the HUD, measured over the first frames */
	bool mapFound;
	SpriteRule hudWorldObjs[4]; /* hud.world.objs, the same way */
	SpriteRule hudObjs[12]; /* hudProf's hud.objs, as used (see romPatch) */
	int hudFadeEff, hudFadeIn; /* the last full fade (2: white, 3: black); frames to fade the next backdrop in */
	long hudPalRef[8]; /* bg_pal_levels() as the backdrop was taken */
	unsigned hudKeep; /* BG layers kept on the game screen (FFTA's spells) */
	bool hudWorld; /* FFTA's world map (hud.world) */
	bool hudScene; /* the profile's other scene (Mario Kart's podium): logo backdrop, its own sprites */
	bool hudCard; /* hud.card: the title card's sprites are what's split off */
	int cardScore; /* hud.card: how far off centre the best take so far is (-1: none) */
	/* hud.card.hideTillTake: this card's take is made (the HUD over it from
	 * then), and how long its start has been seen / not seen */
	bool cardTaken;
	int cardPreFor, cardAway;
	uint32_t cardBest[GBA_W * GBA_H];
	int sideScore; /* hud.card.side: how far off centre its best take is (-1: none) */
	uint32_t sideBest[GBA_W * GBA_H];
	bool hudAlt; /* the HUD on its other layer (hud.alt: SMA2's boss rooms) */
	bool hudOffLatch; /* hud.offAfter has matched: off till on[0] stops matching */
	uint32_t hudLogoBack[HUD_CW * HUD_CH];
	bool hudLogoDone;
	bool hudShown; /* the second screen shows the HUD picture (hudTex) */
	bool overAlone; /* a piece that goes alone is in the map picture (shown even in a fight) */
	Piece pieces[4];
	bool cardLogoMade; /* card_logo's picture is made */
	uint32_t hudLogoBelow[HUD_CW * HUD_CH]; /* profile hud.logoBelow: dark grey, the logo below the HUD */
	/* profile hud.mapTitleRows: those rows of the last map picture, held over
	 * the HUD (mapTitleN rows; 0: none taken yet) */
	uint32_t mapTitle[GBA_W * 32];
	int mapTitleN;
	bool hudLogoBelowDone;
	int hudRegrab; /* hudFrames at which to grab the map layer again (0: kept) */
	bool hudSettle; /* after a fade: wait for the brightness to settle */
	uint32_t hudMapSum; /* the grabbed map's tiles (map_tiles_sum) */
	/* hud.held: the picture kept from the game screen (heldN rows: none) */
	uint32_t heldPic[GBA_W * GBA_H];
	int heldN;
	bool heldArmed; /* (taken once each time it's shown) */
	/* hud.held.over: the number from the last capture since then (w 0: none) */
	uint32_t heldNum[GBA_W * GBA_H];
	int heldNumW, heldNumH;
	/* hud.mapAt.holdTile: the map as last drawn (HUD pixels; mapHeldOk false:
	 * none yet this race), and the dots among the sprites read after the last
	 * frame */
	uint32_t mapHeld[GBA_W * GBA_H];
	bool mapHeldOk;
	int mapUndrawn; /* frames since the map was last drawn */
	uint16_t mapDots[32][3];
	int nMapDots;
	/* hud.mirror: the game screen's last picture from before */
	uint32_t mirrorHeld[GBA_W * GBA_H];
	bool mirrorHave, mirrorOn;
	HudHooks hooks;
} HudState;
extern HudState hud;

/* once a frame for a game with a HUD split (hud.hudProf): this frame's HUD
 * picture, put up through hud.hooks */
void update_hud(void);
/* the map picture with the HUD's pieces in it (upload_map_frame) */
const uint32_t* hud_over_map(const uint32_t* px);
/* a new map picture is in: what the HUD keeps of it (map_show_new) */
void hud_new_map(bool fresh);
/* profile.combat's picture: the logo as on any other second screen */
void make_combat_canvas(void);
/* hud.card: the best take as the backdrop */
void card_backdrop(void);
/* the HUD split's and the fights' picture's set-up for a game (second.h) */
void hud_setup(const MapProfile* lp, int screens);
/* fights on or off, once a frame (hud.combatProf) */
void hud_combat_step(void);

#endif
