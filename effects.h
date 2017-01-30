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
};

class Effect {
	public:
		String name;
		std::function<void(EffectState *state)> run;
};

class EffectManager {
	Effect efx[25];
	int count;
	public:
		EffectManager();
		int add(String name, std::function<void(EffectState *state)> run);
		Effect get(int idx);
		int getCount();
};


#endif (ifndef EFFECTS_H)

