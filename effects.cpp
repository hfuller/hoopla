#include "EffectManager.h"
#include "globals.h"
#include <FastLED.h>

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
	addEffect("% Lightning", false, [](EffectState *state){
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
	addEffect("% Noise16", true, [](EffectState *state) {
		uint8_t beatA = beat8(30); //, 0, 255); //was beatsin8
		//          led array  led count  octaves  x  scale  hue_octaves  hue_x  hue_scale  time
		//fill_noise8(leds,      numpixels, 1,       0, 1,     1,           beatA, 20,        millis());
		fill_noise16(leds,      numpixels, 10,     99,1,     2,           0,     5,         millis()/2);
		//                                            ^ does nothing???          ^ bigger = smaller bands
		if ( state->lowPowerMode ) { blankEveryOtherPixel(); }
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

		fadeToBlackBy(leds, numpixels, thisfade); //Low values = slower fade.
		int pos = random16(numpixels); //Pick an LED at random.
		leds[pos] += state->color;
	});
	addEffect("Juggle", true, [](EffectState *state){
		uint8_t numdots = 4; //Number of dots in use.
		uint8_t faderate = 10; //How long should the trails be. Very low value = longer trails.
		if ( state->lowPowerMode ) {
			faderate = 40;
		}
		uint8_t basebeat = 5; //Higher = faster movement.
		
		fadeToBlackBy(leds, numpixels, faderate);
		for( int i = 0; i < numdots; i++) {
			uint8_t paletteIndex = (i*4)%16;
			//leds[beatsin16(basebeat+i+numdots,0,numpixels)] += state->color; //beat16 is a FastLED 3.1 function
			leds[beatsin16(basebeat+i+numdots,0,numpixels)] += state->currentPalette[paletteIndex];
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
	addEffect("DEBUG Alignment", false, [](EffectState *state) {
		fill_solid(leds, numpixels, CRGB::Black);
		//first pixel green; last pixel red
		leds[0] = CRGB::Green;
		leds[numpixels-1] = CRGB::Red;
		//skip 15, then every nth pixel blue (don't ask)
		for ( int x = 15; x < numpixels; x += 30 ) {
			leds[x] = CRGB::Blue;
		}
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

