#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include "FastLED.h"

#define VERSION			3

#define DEBUG			true
#define Serial			if(DEBUG)Serial		//Only log if we are in debug mode

#define NUMPIXELS		24
#define DATA_PIN		0
#define CLOCK_PIN		2
#define FRAMERATE		60					//how many frames per second to we ideally want to run
#define MAX_LOAD_MA		400					//how many mA are we allowed to draw, at 5 volts

const char* ssid = "";
const char* password = "";

CRGB leds[NUMPIXELS];

unsigned long timer1s;
unsigned long frameCount;

void setup() {
	
	#ifdef DEBUG
	delay(1000); //TODO debug
	#endif

	Serial.begin(115200);
	Serial.print("hoopla v"); Serial.println(VERSION);

	Serial.println("Starting LEDs");
	FastLED.addLeds<APA102, DATA_PIN, CLOCK_PIN, BGR>(leds, NUMPIXELS);
	FastLED.setMaxPowerInVoltsAndMilliamps(5,MAX_LOAD_MA); //assuming 5V
	FastLED.setCorrection(TypicalSMD5050);
	FastLED.setMaxRefreshRate(FRAMERATE);
	for ( int i=0; i<NUMPIXELS; i++ ) {
		leds[i] = CRGB::Black;
	}
	FastLED.show();

	Serial.println("Starting wireless");
	WiFi.mode(WIFI_STA);

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

	Serial.println("Setting up OTA");
	ArduinoOTA.onStart([]() {
		Serial.println("Starting OTA update. Other functions will be suspended.");
	});
	ArduinoOTA.onEnd([]() {
		Serial.println("\nOTA update complete. Reloading");
	});
	ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
		Serial.printf("OTA progress: %u%%\r", (progress / (total / 100)));
	});
	ArduinoOTA.onError([](ota_error_t error) {
		Serial.printf("OTA Error[%u]: ", error);
		if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
		else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
		else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
		else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
		else if (error == OTA_END_ERROR) Serial.println("End Failed");
	});
	ArduinoOTA.begin();

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

	//Serial.println("Doing LED stuff");
	frameCount++; //for frame rate measurement

	if ( leds[0] == CRGB(0,0,0) ) {
		leds[0] = CRGB::Green;
	} else {
		leds[0] = CRGB::Black;
	}
	FastLED.show();

}

