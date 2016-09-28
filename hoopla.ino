#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include "FastLED.h"

#define VERSION			4

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

CRGBPalette16 currentPalette;
CRGBPalette16 targetPalette;
uint8_t maxChanges = 24; 
TBlendType currentBlending;

void runColorpalBeat();
void runFill();
void FillLEDsFromPaletteColors(uint8_t colorIndex);
void SetupRandomPalette();

void setup() {
	
	#ifdef DEBUG
	delay(1000); //TODO debug
	#endif

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

	Serial.println("Starting wireless");
	WiFi.mode(WIFI_STA);

	runFill(); leds[0] = CRGB::Orange; FastLED.show();
	Serial.print("Attempting to associate (STA) to "); Serial.println(WiFi.SSID());
	WiFi.begin();
	if (WiFi.waitForConnectResult() != WL_CONNECTED) {
		Serial.println("Could not associate (STA)");
		//delay(5000);
		//ESP.restart();
	}

	// Port defaults to 8266
	// ArduinoOTA.setPort(8266);

	// Hostname defaults to esp8266-[ChipID]
	// ArduinoOTA.setHostname("myesp8266");

	// No authentication by default
	// ArduinoOTA.setPassword((const char *)"123");

	runFill(); leds[0] = CRGB::Yellow; FastLED.show();
	Serial.println("Setting up OTA");
	ArduinoOTA.onStart([]() {
		runFill(); leds[0] = CRGB::OrangeRed; FastLED.show();
		Serial.println("Starting OTA update. Other functions will be suspended.");
	});
	ArduinoOTA.onEnd([]() {
		runFill(); leds[0] = CRGB::Lime; FastLED.show();
		Serial.println("\nOTA update complete. Reloading");
	});
	ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
		if ( leds[0] == CRGB(0,0,0) ) {
			leds[0] = CRGB::OrangeRed; 
	    } else {
	        leds[0] = CRGB::Black;
	    }
		FastLED.show();

		Serial.printf("OTA progress: %u%%\r", (progress / (total / 100)));
	});
	ArduinoOTA.onError([](ota_error_t error) {
		runFill(); leds[0] = CRGB::Red; FastLED.show();
		Serial.printf("OTA Error[%u]: ", error);
		if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
		else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
		else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
		else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
		else if (error == OTA_END_ERROR) Serial.println("End Failed");
	});
	ArduinoOTA.begin();

	runFill(); leds[0] = CRGB::Green; FastLED.show();
	Serial.print("Startup complete. IP address: ");
	Serial.println(WiFi.localIP());
}

void loop() {
	
	ArduinoOTA.handle();

	EVERY_N_MILLISECONDS(1000) {

		//time to do our every-second tasks
		#ifdef DEBUG
		double fr = (double)frameCount/((double)(millis()-timer1s)/1000);
		Serial.print("#FRAME RATE: "); Serial.print(fr);
		uint32_t loadmw = calculate_unscaled_power_mW(leds,NUMPIXELS);
		Serial.print(" - LOAD: "); Serial.print(loadmw); Serial.print("mW ("); Serial.print(loadmw/5); Serial.print("mA)");
		Serial.println();
		#endif /*DEBUG*/

		timer1s = millis();
		frameCount = 0;

	}

	frameCount++; //for frame rate measurement

	/*
	if ( leds[0] == CRGB(0,0,0) ) {
		leds[0] = CRGB::Green;
	} else {
		leds[0] = CRGB::Black;
	}
	*/

	runColorpalBeat();

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

	show_at_max_brightness_for_power(); //FastLED.show();

	//nblendPaletteTowardPalette( currentPalette, targetPalette, maxChanges);
	EVERY_N_MILLISECONDS(5000) {
		SetupRandomPalette();
	}
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
