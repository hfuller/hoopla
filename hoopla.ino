#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include "FastLED.h"

#define VERSION		1
#define DEBUG

#define NUMPIXELS	1
#define DATA_PIN	0 
#define CLOCK_PIN	2

const char* ssid = "";
const char* password = "";

CRGB leds[NUMPIXELS];

bool updating = false;

void setup() {
	
	#ifdef debug
	delay(1000); //TODO debug
	#endif

	Serial.begin(115200);
	Serial.print("hoopla v"); Serial.println(VERSION);

	Serial.println("Starting LEDs");
	FastLED.addLeds<APA102, DATA_PIN, CLOCK_PIN, BGR>(leds, NUMPIXELS);

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

	Serial.println("Doing LED stuff");
	leds[0] = CRGB::Green;
	FastLED.show();
	delay(500);
	leds[0] = CRGB::Black;
	FastLED.show();
	delay(500);

}
