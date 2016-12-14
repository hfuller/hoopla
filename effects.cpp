#include "effects.h"
#include "globals.h"
#include <FastLED.h>

int Effects::add(String name, std::function<void(void)> run) {
	if ( count >= sizeof(efx) ) {
		Serial.println("[e.cpp] Couldn't add new effect, the pattern is full!");
		Serial.print("[e.cpp] sizeof(efx): "); Serial.print(sizeof(efx));
		Serial.print(" count: "); Serial.println(count);
		return -1;
	}
	Serial.print("[e.cpp] Adding effect "); Serial.print(name); Serial.print(" in slot "); Serial.println(count);
	efx[count].name = name;
	efx[count].run = run;
	return(count++);
}

Effect Effects::get(int idx) {
	if ( idx >= count ) {
		Serial.print("[e.cpp] Returning effect 1, as someone tried to get effect "); Serial.print(idx); Serial.print(" but I only have "); Serial.print(count); Serial.println(" effects loaded!");
		return efx[1];
	}
	return efx[idx];
}

int Effects::getCount() {
	return count;
}

Effects::Effects() {
	count = 0;
	Serial.println("[e.cpp] Loading effects");

	add("Solid All", [](){
		fill_solid(leds, numpixels, color);
	});
	add("Blink One", [](){
		//intEffectState = where on the strip to blink.
		EVERY_N_MILLISECONDS(500) {
			if ( nextColor == CRGB(0,0,0) ) {
				nextColor = color;
			} else {
				nextColor = CRGB::Black;
			}
		}
		
		fill_solid(leds, numpixels, CRGB::Black);
		leds[intEffectState] = nextColor;
	});
	add("Solid One", [](){
		//intEffectState = where on the strip to write a solid LED.
		fill_solid(leds, numpixels, CRGB::Black);
		leds[intEffectState] = color;
	});
	add("Dot Beat", [](){
		uint8_t count = 0; //Count up to 255 and then reverts to 0
		uint8_t fadeval = 224; //Trail behind the LED's. Lower => faster fade.
		uint8_t bpm = 30;

		uint8_t inner = beatsin8(bpm, numpixels/4, numpixels/4*3);
		uint8_t outer = beatsin8(bpm, 0, numpixels-1);
		uint8_t middle = beatsin8(bpm, numpixels/3, numpixels/3*2);
	
		//leds[middle] = CRGB::Purple; leds[inner] = CRGB::Blue; leds[outer] = CRGB::Aqua;
		leds[middle] = CRGB::Aqua; leds[inner] = CRGB::Blue; leds[outer] = CRGB::Purple;
	
		nscale8(leds,numpixels,fadeval); // Fade the entire array. Or for just a few LED's, use  nscale8(&leds[2], 5, fadeval);
	
	});
	add("Ease Me", [](){
		//boolEffectState: Whether to reverse the thing
		static uint8_t easeOutVal = 0;
		static uint8_t easeInVal  = 0;
		static uint8_t lerpVal    = 0;
	
		easeOutVal = ease8InOutQuad(easeInVal);
		if ( boolEffectState ) {
			easeInVal -= 3;
		} else {
			easeInVal += 3;
		}
		if ( easeInVal > 250 ) {
			boolEffectState = true;
		} else if ( easeInVal < 5 ) {
			boolEffectState = false;
		}
	
		lerpVal = lerp8by8(0, numpixels, easeOutVal);
	
		for ( int i = lerpVal; i < numpixels; i += 8 ) {
			leds[i] = color;
		}
		fadeToBlackBy(leds, numpixels, 32);                     // 8 bit, 1 = slow, 255 = fast
	
	});
	add("Fast Circ", [](){
		//intEffectState is how far down the cycle we are (out of thisgap).
		//boolEffectState is whether it's reversed

		int thisdir = ( boolEffectState ? -1 : 1 );
		int thisgap = 8;

		EVERY_N_MILLISECONDS(50) {
			intEffectState = (intEffectState + thisdir)%thisgap;
			for ( int i=intEffectState; i<numpixels; i+=thisgap ) {
				leds[i] = color;
			}
		}
		fadeToBlackBy(leds, numpixels, 24);
	
	});
	add("Confetti", [](){
		uint8_t thisfade = 16; //How quickly does it fade? Lower = slower fade rate.
		int thishue = 50; //Starting hue.
		uint8_t thisinc = 1; //Incremental value for rotating hues
		uint8_t thissat = 100; //The saturation, where 255 = brilliant colours.
		uint8_t thisbri = 255; //Brightness of a sequence. Remember, max_bright is the overall limiter.
		int huediff = 256; //Range of random #'s to use for hue

		EVERY_N_MILLISECONDS(5000) {
			//THIS ENTIRE SWITCH STATEMENT IS BROKEN BTW
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
	add("Juggle", [](){
		uint8_t numdots = 4; //Number of dots in use.
		uint8_t faderate = 2; //How long should the trails be. Very low value = longer trails.
		uint8_t hueinc = 16; //Incremental change in hue between each dot.
		uint8_t curhue = 0; //The current hue
		uint8_t basebeat = 5; //Higher = faster movement.
		uint8_t thishue = 50; //starting hue
		uint8_t thissat = 100;
		uint8_t thisbri = 255;
		
		//THIS ENTIRE SWITCH STATEMENT IS TOTALLY BROKEN
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
	add("Lightning", [](){
		fill_solid(leds, numpixels, CRGB::Black);

		uint8_t frequency = 50; //controls the interval between strikes
		uint8_t flashes = 8; //the upper limit of flashes per strike
		unsigned int dimmer = 1;

		int flashCounter = intEffectState; //how many flashes have we done already, during this cycle?
		unsigned long lastFlashTime = ulongEffectState; //when did we last flash?
		unsigned long nextFlashDelay = ulongEffectState2; //how long do we wait since the last flash before flashing again?
		int ledstart = intEffectState2; // Starting location of a flash
		int ledlen = intEffectState3; // Length of a flash

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
		intEffectState = flashCounter;
		ulongEffectState = lastFlashTime;
		ulongEffectState2 = nextFlashDelay;
		intEffectState2 = ledstart;
		intEffectState3 = ledlen;
	
	});
	add("Fill from palette", [](){
		uint8_t beatA = beat8(30); //, 0, 255); //was beatsin8
		fill_palette(leds, numpixels, beatA, 0, currentPalette, 255, LINEARBLEND);
	});
	add("Rotate palette", [](){
		uint8_t beatA = beat8(30); //, 0, 255); //was beatsin8
		fill_palette(leds, numpixels, beatA, 6, currentPalette, 255, LINEARBLEND);
	});
	
	Serial.print("[e.cpp] "); Serial.print(count); Serial.println(" effects loaded. Please pull forward for your total");
}

