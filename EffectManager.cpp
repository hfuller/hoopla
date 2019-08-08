#include "EffectManager.h"
#include <FastLED.h>

int EffectManager::addEffect(String name, bool useForAttractMode, std::function<void(EffectState *state)> run) {
	if ( efxCount >= sizeof(efx) ) {
		Serial.println("[e.cpp] Couldn't add new effect, there's no room!");
		Serial.print("[e.cpp] sizeof(efx): "); Serial.print(sizeof(efx));
		Serial.print(" efxCount: "); Serial.println(efxCount);
		return -1;
	}
	Serial.print("[e.cpp] Adding effect "); Serial.print(name); Serial.print(" in slot "); Serial.println(efxCount);
	efx[efxCount].name = name;
	efx[efxCount].useForAttractMode = useForAttractMode;
	efx[efxCount].run = run;
	return(efxCount++);
}
int EffectManager::addPalette(String name, CRGBPalette16 palette) {
	if ( pltCount >= sizeof(plts) ) {
		Serial.println("[e.cpp] Couldn't add new palette, there is no room!");
		return -1;
	}
	Serial.print("[e.cpp] Adding palette "); Serial.println(name);
	plts[pltCount].name = name;
	plts[pltCount].palette = palette;
	return(pltCount++);
}

Effect EffectManager::getEffect(int idx) {
	if ( idx >= efxCount ) {
		Serial.print("[e.cpp] Returning effect 1, as someone tried to get effect "); Serial.print(idx); Serial.print(" but I only have "); Serial.print(efxCount); Serial.println(" effects loaded!");
		return efx[1];
	}
	return efx[idx];
}
Palette EffectManager::getPalette(int idx) {
	if ( idx > pltCount ) {
		Serial.print("[e.cpp] Tried to get palette "); Serial.print(idx); Serial.print(" but I only have "); Serial.println(pltCount);
		return plts[1];
	}
	return plts[idx];
}

int EffectManager::getEffectCount() {
	return efxCount;
}
int EffectManager::getPaletteCount() {
	return pltCount;
}

