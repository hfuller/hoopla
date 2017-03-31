#ifndef EFFECTS_H
#define EFFECTS_H


#include <Arduino.h>
#include <functional>
#include <FastLED.h>

struct EffectState {
	CRGB color;
	CRGB nextColor;
	bool boolEffectState;
	int intEffectState;
	int intEffectState2;
	int intEffectState3;
	unsigned long ulongEffectState;
	unsigned long ulongEffectState2;
	CRGBPalette16 currentPalette;
	bool lowPowerMode;
};

class Effect {
	public:
		String name;
		bool useForAttractMode;
		std::function<void(EffectState *state)> run;
};

class Palette {
	public:
		String name;
		CRGBPalette16 palette;
};

class EffectManager {
	Effect efx[25];
	Palette plts[25];
	int efxCount;
	int pltCount;
	public:
		EffectManager();
		int addEffect(String name, bool useForAttractMode, std::function<void(EffectState *state)> run);
		int addPalette(String name, CRGBPalette16 palette);
		Effect getEffect(int idx);
		Palette getPalette(int idx);
		int getEffectCount();
		int getPaletteCount();
};


#endif (ifndef EFFECTS_H)

