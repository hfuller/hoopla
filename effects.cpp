#include "effects.h"
#include "globals.h"
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

void blankEveryOtherPixel() {
	for ( int i=0; i < numpixels; i = i+2 ) {
		leds[i] = CRGB::Black;
	}
}

EffectManager::EffectManager() {
	Serial.println("[e.cpp] Effect Manager!! wooo");
	efxCount = 0;
	pltCount = 0;

	Serial.println("[e.cpp] Loading effects");

	addEffect("Solid All", false, [](EffectState *state){
		fill_solid(leds, numpixels, state->color);
		if ( state->lowPowerMode ) { blankEveryOtherPixel(); }
	});
	addEffect("Blink One", false, [](EffectState *state){
		//state->intEffectState = where on the strip to blink.
		EVERY_N_MILLISECONDS(500) {
			if ( state->nextColor == CRGB(0,0,0) ) {
				state->nextColor = state->color;
			} else {
				state->nextColor = CRGB::Black;
			}
		}
		
		fill_solid(leds, numpixels, CRGB::Black);
		leds[state->intEffectState] = state->nextColor;
	});
	addEffect("Solid One", false, [](EffectState *state){
		//state->intEffectState = where on the strip to write a solid LED.
		fill_solid(leds, numpixels, CRGB::Black);
		leds[state->intEffectState] = state->color;
	});
	addEffect("Lightning", false, [](EffectState *state){
		fill_solid(leds, numpixels, CRGB::Black);

		uint8_t frequency = 50; //controls the interval between strikes
		uint8_t flashes = 8; //the upper limit of flashes per strike
		unsigned int dimmer = 1;

		int flashCounter = state->intEffectState; //how many flashes have we done already, during this cycle?
		unsigned long lastFlashTime = state->ulongEffectState; //when did we last flash?
		unsigned long nextFlashDelay = state->ulongEffectState2; //how long do we wait since the last flash before flashing again?
		int ledstart = state->intEffectState2; // Starting location of a flash
		int ledlen = state->intEffectState3; // Length of a flash

		//Serial.print("[ltnng] entered. millis()="); Serial.print(millis()); Serial.print(" lastFlashTime="); Serial.print(lastFlashTime); Serial.print(" nextFlashDelay="); Serial.println(nextFlashDelay);
		if ( (millis() - lastFlashTime) > nextFlashDelay ) { //time to flash
			//Serial.print("[ltnng] flashCounter: ");
			//Serial.println(flashCounter);
			nextFlashDelay = 0;
			if ( flashCounter == 0 ) {
				//Serial.println("[ltnng] New strike");
				//new strike. init our values for this set of flashes
				ledstart = random16(numpixels);           // Determine starting location of flash
				ledlen = random16(numpixels-ledstart);    // Determine length of flash (not to go beyond numpixels-1)
				if ( state->lowPowerMode && ledlen > 20 ) {
					ledlen = 20;
				}
				dimmer = 5;
				nextFlashDelay += 150;   // longer delay until next flash after the leader
			} else {
				dimmer = random8(1,3);           // return strokes are brighter than the leader
			}
	
			if ( flashCounter < random8(3,flashes) ) {
				//Serial.println("[ltnng] Time to flash");
				flashCounter++;
				fill_solid(leds+ledstart,ledlen,CHSV(255, 0, 255/dimmer));
				show_at_max_brightness_for_power();                       // Show a section of LED's
				delay(random8(4,10));                 // each flash only lasts 4-10 milliseconds. We will use delay() because the timing has to be tight. still will run shorter than 10ms.
				fill_solid(leds+ledstart,ledlen,CHSV(255,0,0));   // Clear the section of LED's
				show_at_max_brightness_for_power();     
				nextFlashDelay += 50+random8(100);               // shorter delay between strokes  
			} else {
				//Serial.println("[ltnng] Strike complete");
				flashCounter = 0;
				nextFlashDelay = random8(frequency)*100;          // delay between strikes
			}
			lastFlashTime = millis();
		} 

		//Save shit until next time (I AM THE WORST)
		state->intEffectState = flashCounter;
		state->ulongEffectState = lastFlashTime;
		state->ulongEffectState2 = nextFlashDelay;
		state->intEffectState2 = ledstart;
		state->intEffectState3 = ledlen;
	
	});
	addEffect("Dot Beat", true, [](EffectState *state){
		uint8_t fadeval = 224; //Trail behind the LED's. Lower => faster fade.
		uint8_t bpm = 30;

		uint8_t inner = beatsin8(bpm, numpixels/4, numpixels/4*3);
		uint8_t outer = beatsin8(bpm, 0, numpixels-1);
		uint8_t middle = beatsin8(bpm, numpixels/3, numpixels/3*2);
	
		//leds[middle] = CRGB::Purple; leds[inner] = CRGB::Blue; leds[outer] = CRGB::Aqua;
		//leds[middle] = CRGB::Aqua; leds[inner] = CRGB::Blue; leds[outer] = CRGB::Purple;
		leds[middle] = state->currentPalette[0]; leds[inner] = state->currentPalette[6]; leds[outer] = state->currentPalette[10];
	
		nscale8(leds,numpixels,fadeval); // Fade the entire array. Or for just a few LED's, use  nscale8(&leds[2], 5, fadeval);
	
	});
	addEffect("Ease Me", true, [](EffectState *state){
		//state->boolEffectState: Whether to reverse the thing
		static uint8_t easeOutVal = 0;
		static uint8_t easeInVal  = 0;
		static uint8_t lerpVal    = 0;
	
		easeOutVal = ease8InOutQuad(easeInVal);
		if ( state->boolEffectState ) {
			easeInVal -= 3;
		} else {
			easeInVal += 3;
		}
		if ( easeInVal > 250 ) {
			state->boolEffectState = true;
		} else if ( easeInVal < 5 ) {
			state->boolEffectState = false;
		}
	
		lerpVal = lerp8by8(0, numpixels, easeOutVal);
	
		for ( int i = lerpVal; i < numpixels; i += 8 ) {
			leds[i] = state->color;
		}
		fadeToBlackBy(leds, numpixels, 32);                     // 8 bit, 1 = slow, 255 = fast
	
	});
	addEffect("Fast Circ", true, [](EffectState *state){
		//state->intEffectState is how far down the cycle we are (out of thisgap).
		//state->boolEffectState is whether it's reversed

		int thisdir = ( state->boolEffectState ? -1 : 1 );
		int thisgap = 8;

		EVERY_N_MILLISECONDS(50) {
			state->intEffectState = (state->intEffectState + thisdir)%thisgap;
			for ( int i=state->intEffectState; i<numpixels; i+=thisgap ) {
				leds[i] = state->color;
			}
		}
		fadeToBlackBy(leds, numpixels, 24);
	
	});
	addEffect("Confetti", true, [](EffectState *state){
		uint8_t thisfade = 16; //How quickly does it fade? Lower = slower fade rate.
		int thishue = 50; //Starting hue.
		uint8_t thisinc = 1; //Incremental value for rotating hues
		uint8_t thissat = 100; //The saturation, where 255 = brilliant colours.
		uint8_t thisbri = 255; //Brightness of a sequence. Remember, max_bright is the overall limiter.
		int huediff = 256; //Range of random #'s to use for hue

		EVERY_N_MILLISECONDS(5000) {
			//THIS ENTIRE SWITCH STATEMENT IS BROKEN BTW HACK HACK HACK
			switch(0) {
				case 7: thisinc=1; thishue=192; thissat=255; thisfade=16; huediff=256; break;  // You can change values here, one at a time , or altogether.
				case 8: thisinc=2; thishue=128; thisfade=8; huediff=64; break;
				case 9: thisinc=1; thishue=random16(255); thisfade=4; huediff=16; break;      // Only gets called once, and not continuously for the next several seconds. Therefore, no rainbows.
			}
		}
	
		fadeToBlackBy(leds, numpixels, thisfade); //Low values = slower fade.
		int pos = random16(numpixels); //Pick an LED at random.
		leds[pos] += CHSV((thishue + random16(huediff))/4 , thissat, thisbri); //I use 12 bits for hue so that the hue increment isn't too quick.
		thishue = thishue + thisinc; //It increments here.
	});
	addEffect("Juggle", true, [](EffectState *state){
		uint8_t numdots = 4; //Number of dots in use.
		uint8_t faderate = 10; //How long should the trails be. Very low value = longer trails.
		if ( state->lowPowerMode ) {
			faderate = 40;
		}
		uint8_t hueinc = 16; //Incremental change in hue between each dot.
		uint8_t curhue = 0; //The current hue
		uint8_t basebeat = 5; //Higher = faster movement.
		uint8_t thishue = 50; //starting hue
		uint8_t thissat = 100;
		uint8_t thisbri = 255;
		
		//THIS ENTIRE SWITCH STATEMENT IS TOTALLY BROKEN HACK HACK HACK
		switch(0) {
			case 11: numdots = 1; basebeat = 20; hueinc = 16; faderate = 2; thishue = 0; break;
			case 12: numdots = 4; basebeat = 10; hueinc = 16; faderate = 8; thishue = 128; break;
			case 13: numdots = 8; basebeat =  3; hueinc =  0; faderate = 8; thishue=random8(); break; // Only gets called once, and not continuously for the next several seconds. Therefore, no rainbows.
		}
		curhue = thishue; //Reset the hue values.
		fadeToBlackBy(leds, numpixels, faderate);
		for( int i = 0; i < numdots; i++) {
			leds[beatsin16(basebeat+i+numdots,0,numpixels)] += CHSV(curhue, thissat, thisbri); //beat16 is a FastLED 3.1 function
			curhue += hueinc;
		}
	
	});
	addEffect("Palette spectrum", false, [](EffectState *state){
		uint8_t beatA = beat8(30); //, 0, 255); //was beatsin8
		fill_palette(leds, numpixels, beatA, 6, state->currentPalette, 255, LINEARBLEND);
		if ( state->lowPowerMode ) { blankEveryOtherPixel(); }
	});
	addEffect("Glitter + palette spectrum", true, [](EffectState *state) {
		uint8_t beatA = beat8(30); //, 0, 255); //was beatsin8
		fill_palette(leds, numpixels, beatA, 6, state->currentPalette, 255, LINEARBLEND);
		if ( random8() < 200 ) {
			leds[random16(numpixels)] += CRGB::White;
		}
		if ( state->lowPowerMode ) { blankEveryOtherPixel(); }
	});
	addEffect("% Noise16", true, [](EffectState *state) {
		uint8_t beatA = beat8(30); //, 0, 255); //was beatsin8
		//          led array  led count  octaves  x  scale  hue_octaves  hue_x  hue_scale  time
		//fill_noise8(leds,      numpixels, 1,       0, 1,     1,           beatA, 20,        millis());
		fill_noise16(leds,      numpixels, 10,     99,1,     2,           0,     5,         millis()/2);
		//                                            ^ does nothing???          ^ bigger = smaller bands
		if ( state->lowPowerMode ) { blankEveryOtherPixel(); }
	});

	Serial.println("[e.cpp] Loading palettes");
	addPalette("Rainbow gradient", RainbowColors_p);
	addPalette("Rainbow stripe", RainbowStripeColors_p);
	addPalette("Ocean", OceanColors_p);
	addPalette("Cloud", CloudColors_p);
	addPalette("Lava", LavaColors_p);
	addPalette("Forest", ForestColors_p);
	addPalette("Party", PartyColors_p);

	CRGBPalette16 tempPalette;
	
	for( int i = 0; i < 16; i++) {
		tempPalette[i] = CHSV( random8(), 255, random8());
	}
	addPalette("Random colors", tempPalette);
	
	Serial.print("[e.cpp] "); Serial.print(efxCount); Serial.println(" effects loaded. Please pull forward for your total");
}

