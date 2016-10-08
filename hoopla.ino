#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include "FastLED.h"

#define VERSION			5

#define DEBUG			true
#define Serial			if(DEBUG)Serial		//Only log if we are in debug mode

#define NUMPIXELS		24					//NOTE: we write 300 pixels in some cases, like when blanking the strip.
#define DATA_PIN		0
#define CLOCK_PIN		2
#define FRAMERATE		60					//how many frames per second to we ideally want to run
#define MAX_LOAD_MA		400					//how many mA are we allowed to draw, at 5 volts

const char* ssid = "";
const char* password = "";

CRGB leds[300];

unsigned long timer1s;
unsigned long frameCount;

byte effect = 0;
CRGB color = CRGB::Teal;
CRGB nextColor = CRGB::Black;
CRGBPalette16 currentPalette;
CRGBPalette16 targetPalette;
uint8_t maxChanges = 24; 
TBlendType currentBlending;

void runColorpalBeat();
void runFill();
void runSolidOne();
void runBlinkOne();
void FillLEDsFromPaletteColors(uint8_t colorIndex);
void SetupRandomPalette();
void runLeds();

void setup() {
	
	Serial.begin(115200);
	Serial.print("hoopla v"); Serial.println(VERSION);

	Serial.println("Starting LEDs");
	FastLED.addLeds<APA102, DATA_PIN, CLOCK_PIN, BGR>(leds, 300);
	FastLED.setMaxPowerInVoltsAndMilliamps(5,MAX_LOAD_MA); //assuming 5V
	FastLED.setCorrection(TypicalSMD5050);
	FastLED.setMaxRefreshRate(FRAMERATE);
	for ( int i=0; i<300; i++ ) {
		leds[i] = CRGB::Black;
	}
	leds[0] = CRGB::Red; FastLED.show();

	Serial.println("Starting effects");
	currentPalette = RainbowColors_p;                           // RainbowColors_p; CloudColors_p; PartyColors_p; LavaColors_p; HeatColors_p;
	targetPalette = RainbowColors_p;                           // RainbowColors_p; CloudColors_p; PartyColors_p; LavaColors_p; HeatColors_p;
	currentBlending = LINEARBLEND;
	effect = 2; //solid for status indication

	color = CRGB::Orange; runLeds();
	Serial.println("Starting wireless");
	WiFi.mode(WIFI_STA);

	Serial.print("Attempting to associate (STA) to "); Serial.println(WiFi.SSID());
	WiFi.begin();

	// Port defaults to 8266
	// ArduinoOTA.setPort(8266);

	// Hostname defaults to esp8266-[ChipID]
	// ArduinoOTA.setHostname("myesp8266");

	// No authentication by default
	// ArduinoOTA.setPassword((const char *)"123");

	color = CRGB::Yellow; runLeds();
	Serial.println("Setting up OTA");
	ArduinoOTA.onStart([]() {
		effect = 1;
		color = CRGB::OrangeRed;
		Serial.println("Starting OTA update. Other functions will be suspended.");
	});
	ArduinoOTA.onEnd([]() {
		effect = 2;
		color = CRGB::Lime;
		runLeds();
		Serial.println("\nOTA update complete. Reloading");
	});
	ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
		if ( leds[0] == CRGB(0,0,0) ) {
			color = CRGB::OrangeRed; 
	    } else {
	        color = CRGB::Black;
	    }
		runLeds();

		Serial.printf("OTA progress: %u%%\r", (progress / (total / 100)));
	});
	ArduinoOTA.onError([](ota_error_t error) {
		color = CRGB::Red;
		Serial.printf("OTA Error[%u]: ", error);
		if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
		else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
		else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
		else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
		else if (error == OTA_END_ERROR) Serial.println("End Failed");
	});
	ArduinoOTA.begin();

	color = CRGB::Green; runLeds();
	Serial.println("Startup complete.");
}

void loop() {
	
	ArduinoOTA.handle();

	EVERY_N_MILLISECONDS(1000) {

		//time to do our every-second tasks
		#ifdef DEBUG
		double fr = (double)frameCount/((double)(millis()-timer1s)/1000);
		Serial.print("#FRAME RATE: "); Serial.print(fr);
		uint32_t loadmw = calculate_unscaled_power_mW(leds,NUMPIXELS);
		Serial.print(" - LOAD: "); Serial.print(loadmw); Serial.print("mW ("); Serial.print(loadmw/5); Serial.print("mA) - ");
		Serial.print("Wi-Fi: "); Serial.print( (WiFi.status() == WL_CONNECTED) ? "Connected" : "Disconnected");
		Serial.println();
		#endif /*DEBUG*/

		timer1s = millis();
		frameCount = 0;

	}

	runLeds();

}

void runLeds() {
	
	frameCount++; //for frame rate measurement

	/*
	if ( leds[0] == CRGB(0,0,0) ) {
		leds[0] = CRGB::Green;
	} else {
		leds[0] = CRGB::Black;
	}
	*/

	switch (effect) {
		case 0:
			runColorpalBeat();
			break;
		case 1:
			runBlinkOne();
			break;
		case 2:
			runSolidOne();
			break;
		default:
			Serial.print("Unknown effect selected: "); Serial.println(effect);
			delay(10);
	}
	
	show_at_max_brightness_for_power(); //FastLED.show();

}

//EFFECTS

void runFill() {
	for ( int i=0; i<NUMPIXELS; i++ ) {
		leds[i] = CRGB::Black;
	}
}
	
void runColorpalBeat() {
	uint8_t beatA = beat8(30); //, 0, 255); //was beatsin8
	FillLEDsFromPaletteColors(beatA);

	//nblendPaletteTowardPalette( currentPalette, targetPalette, maxChanges);
	EVERY_N_MILLISECONDS(5000) {
		SetupRandomPalette();
	}
}

void runBlinkOne() {
	EVERY_N_MILLISECONDS(500) {
		if ( leds[0] == CRGB(0,0,0) ) {
			nextColor = color;
		} else {
			nextColor = CRGB::Black;
		}
	}

	runFill();
	leds[0] = nextColor;
}

void runSolidOne() {
	runFill();
	leds[0] = color;
}



//UTILITIES FOR EFFECTS

void FillLEDsFromPaletteColors(uint8_t colorIndex) {
	//uint8_t beatB = beatsin8(30, 10, 20);                       // Delta hue between LED's
    for (int i = 0; i < NUMPIXELS; i++) {
	    leds[i] = ColorFromPalette(currentPalette, colorIndex, 255, currentBlending);
	    //colorIndex += beatB;
	}
} //FillLEDsFromPaletteColors()
void SetupRandomPalette() {
	targetPalette = CRGBPalette16(CHSV(random8(), 255, 32), CHSV(random8(), random8(64)+192, 255), CHSV(random8(), 255, 32), CHSV(random8(), 255, 255)); 
} // SetupRandomPalette()
