#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include "FastLED.h"
#include <DNSServer.h>
#include <ESP8266WebServer.h>

#define VERSION			13

#define DEBUG			true
#define Serial			if(DEBUG)Serial		//Only log if we are in debug mode

#define NUMPIXELS		48					//NOTE: we write 300 pixels in some cases, like when blanking the strip.
#define DATA_PIN		0
#define CLOCK_PIN		2
#define FRAMERATE		60					//how many frames per second to we ideally want to run
#define MAX_LOAD_MA		400					//how many mA are we allowed to draw, at 5 volts

const char* ssid = "";
const char* password = "";
char ssidTemp[32] = "";
char passwordTemp[32] = "";
const char* name = "jennifers-hoop";
const char* passwordAP = "deeznuts";

const byte DNS_PORT = 53;
DNSServer dnsServer;
ESP8266WebServer server(80);

CRGB leds[300];

unsigned long timer1s;
unsigned long frameCount;
unsigned long lastWirelessChange;

//EFFECT SHIT
byte effect = 0;
CRGB color = CRGB::Teal;
CRGB nextColor = CRGB::Black;
//ColorpalBeat
CRGBPalette16 currentPalette;
CRGBPalette16 targetPalette;
uint8_t maxChanges = 24; 
TBlendType currentBlending;
//Confetti
uint8_t  thisfade = 16;                                        // How quickly does it fade? Lower = slower fade rate.
int       thishue = 50;                                       // Starting hue.
uint8_t   thisinc = 1;                                        // Incremental value for rotating hues
uint8_t   thissat = 100;                                      // The saturation, where 255 = brilliant colours.
uint8_t   thisbri = 255;                                      // Brightness of a sequence. Remember, max_bright is the overall limiter.
int       huediff = 256;                                      // Range of random #'s to use for hue
//DotBeat
uint8_t   count =   0;                                        // Count up to 255 and then reverts to 0
uint8_t fadeval = 224;                                        // Trail behind the LED's. Lower => faster fade.
uint8_t bpm = 30;

bool isAP = false;
bool doConnect = false;
bool doServiceRestart = false;

void runColorpalBeat();
void runConfetti();
void runDotBeat();
void runFill();
void runSolidOne();
void runBlinkOne();
void FillLEDsFromPaletteColors(uint8_t colorIndex);
void SetupRandomPalette();
void runLeds();
void handleRoot();
void handleEffectSave();
void handleStyle();
void handleSetup();
void handleSetupSave();
void handleNotFound();
boolean captivePortal();
boolean isIp(String str);
String toStringIp(IPAddress ip);


void setup() {
	
	Serial.begin(115200);
	Serial.print("[start] hoopla v"); Serial.println(VERSION);

	Serial.println("[start] Starting LEDs");
	FastLED.addLeds<APA102, DATA_PIN, CLOCK_PIN, BGR>(leds, 300);
	FastLED.setMaxPowerInVoltsAndMilliamps(5,MAX_LOAD_MA); //assuming 5V
	FastLED.setCorrection(TypicalSMD5050);
	FastLED.setMaxRefreshRate(FRAMERATE);
	for ( int i=0; i<300; i++ ) {
		leds[i] = CRGB::Black;
	}
	leds[0] = CRGB::Red; FastLED.show();

	Serial.println("[start] Starting effects");
	currentPalette = RainbowColors_p;                           // RainbowColors_p; CloudColors_p; PartyColors_p; LavaColors_p; HeatColors_p;
	targetPalette = RainbowColors_p;                           // RainbowColors_p; CloudColors_p; PartyColors_p; LavaColors_p; HeatColors_p;
	currentBlending = LINEARBLEND;
	effect = 2; //solid for status indication

	color = CRGB::Orange; runLeds();

	Serial.print("[start] Attempting to associate (STA) to "); Serial.println(WiFi.SSID());
	lastWirelessChange = millis();
	WiFi.mode(WIFI_STA);
	WiFi.setAutoReconnect(false);
	WiFi.hostname(name);
	WiFi.begin();

	Serial.println("[start] Starting DNS");
	dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
	dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

	Serial.println("[start] starting http");
	server.on("/style.css", handleStyle);
	server.on("/", handleRoot);
	server.on("/effect/save", handleEffectSave);
	server.on("/setup", handleSetup);
	server.on("/setup/save", handleSetupSave);
	server.onNotFound ( handleNotFound );
	server.begin(); // Web server start

	color = CRGB::Yellow; runLeds();
	Serial.println("[start] Setting up OTA");
	// Port defaults to 8266
	//ArduinoOTA.setPort(8266);
	// Hostname defaults to esp8266-[ChipID]
	ArduinoOTA.setHostname(name);
	// No authentication by default
	//ArduinoOTA.setPassword((const char *)"123");
	ArduinoOTA.onStart([]() {
		effect = 1;
		color = CRGB::OrangeRed;
		Serial.println("Starting OTA update. Other functions will be suspended.");
	});
	ArduinoOTA.onEnd([]() {
		effect = 2;
		color = CRGB::Yellow;
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
	dnsServer.processNextRequest();
	server.handleClient();
	
	EVERY_N_MILLISECONDS(1000) {

		//time to do our every-second tasks
		#ifdef DEBUG
		double fr = (double)frameCount/((double)(millis()-timer1s)/1000);
		Serial.print("[Hbeat] FRAME RATE: "); Serial.print(fr);
		uint32_t loadmw = calculate_unscaled_power_mW(leds,NUMPIXELS);
		Serial.print(" - LOAD: "); Serial.print(loadmw); Serial.print("mW ("); Serial.print(loadmw/5); Serial.print("mA) - ");
		Serial.print("Wi-Fi: "); Serial.print( (WiFi.status() == WL_CONNECTED) ? "Connected" : "Disconnected");
		Serial.println();
		#endif /*DEBUG*/

		timer1s = millis();
		frameCount = 0;

	}
	EVERY_N_MILLISECONDS(5000) {

		//do Wi-Fi stuff

		#ifdef DEBUG
		if ( WiFi.status() == WL_CONNECTED ) {
			//we are connected
			Serial.print("[Wi-Fi] Client: "); Serial.print(WiFi.SSID());
			Serial.print(" as "); Serial.print(WiFi.localIP());
			Serial.print(" at "); Serial.println(WiFi.RSSI());
		} else if ( WiFi.status() == WL_IDLE_STATUS ) {
			Serial.print("[Wi-Fi] Associating to "); Serial.println(WiFi.SSID());
		} else {
			Serial.print("[Wi-Fi] No association to "); Serial.println(WiFi.SSID());
		}
		#endif /*DEBUG*/

		if ( doConnect ) { //we have a pending connect attempt from the config subsystem
			Serial.println("[Wi-Fi] Trying to associate to AP due to config change");
			isAP = false;
			WiFi.disconnect();
			WiFi.mode(WIFI_STA);
			WiFi.begin(ssidTemp,passwordTemp);
			doConnect = false; //shouldn't need this but sometimes we do... if WiFi.status() isn't updated by the underlying libs
			lastWirelessChange = millis();
			doServiceRestart = true; //restart OTA
		}
		if ( (millis() - lastWirelessChange) > 15000 ) {
			if ( WiFi.status() != WL_CONNECTED && !isAP ) { //We have been trying to associate for far too long.
				Serial.println("[Wi-Fi] Client giving up");
				//WiFi.disconnect(); //Don't do this or it will clear the ssid/pass in nvram!!!!!
				Serial.println("[Wi-Fi] Starting wireless AP");
				WiFi.mode(WIFI_AP);
				WiFi.softAP(name,passwordAP);
				delay(100); //reliable IP retrieval.
				Serial.print("[Wi-Fi] AP started. I am "); Serial.println(WiFi.softAPIP());
				lastWirelessChange = millis();
				isAP = true;
			}
			if ( doServiceRestart ) {
				Serial.println("[Wi-Fi] Restarting services due to Wi-Fi state change");
				ArduinoOTA.begin();
				doServiceRestart = false;
			}
		}

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
		case 3:
			runConfetti();
			break;
		case 4:
			runDotBeat();
			break;
		default:
			Serial.print("[blink] Unknown effect selected: "); Serial.println(effect);
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

void runConfetti() {
	uint8_t secondHand = (millis() / 1000) % 15;                // IMPORTANT!!! Change '15' to a different value to change duration of the loop.
	static uint8_t lastSecond = 99;                             // Static variable, means it's only defined once. This is our 'debounce' variable.
	if (lastSecond != secondHand) {                             // Debounce to make sure we're not repeating an assignment.
		lastSecond = secondHand;
		switch(secondHand) {
			case  0: thisinc=1; thishue=192; thissat=255; thisfade=2; huediff=256; break;  // You can change values here, one at a time , or altogether.
			case  5: thisinc=2; thishue=128; thisfade=8; huediff=64; break;
			case 10: thisinc=1; thishue=random16(255); thisfade=1; huediff=16; break;      // Only gets called once, and not continuously for the next several seconds. Therefore, no rainbows.
			case 15: break;                                                                // Here's the matching 15 for the other one.
		}
	}

	fadeToBlackBy(leds, NUMPIXELS, thisfade);                    // Low values = slower fade.
	int pos = random16(NUMPIXELS);                               // Pick an LED at random.
	leds[pos] += CHSV((thishue + random16(huediff))/4 , thissat, thisbri);  // I use 12 bits for hue so that the hue increment isn't too quick.
	thishue = thishue + thisinc;                                // It increments here.
}

void runDotBeat() {
	uint8_t inner = beatsin8(bpm, NUMPIXELS/4, NUMPIXELS/4*3);
	uint8_t outer = beatsin8(bpm, 0, NUMPIXELS-1);
	uint8_t middle = beatsin8(bpm, NUMPIXELS/3, NUMPIXELS/3*2);

	//leds[middle] = CRGB::Purple; leds[inner] = CRGB::Blue; leds[outer] = CRGB::Aqua;
	leds[middle] = CRGB::Aqua; leds[inner] = CRGB::Blue; leds[outer] = CRGB::Purple;

	nscale8(leds,NUMPIXELS,fadeval);                             // Fade the entire array. Or for just a few LED's, use  nscale8(&leds[2], 5, fadeval);
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


//HTTP STUFF borrowed from https://github.com/esp8266/Arduino/blob/master/libraries/DNSServer/examples/CaptivePortalAdvanced/CaptivePortalAdvanced.ino
const char * header = "\
<!DOCTYPE html>\
<html>\
<head>\
<title>hoopla</title>\
<link rel='stylesheet' href='/style.css'>\
<meta name='viewport' content='width=device-width, initial-scale=1'>\
</head>\
<body>\
<div id='top'>\
	<span id='title'>hoopla</span>\
	<a href='/'>Controls</a>\
	<a href='/setup'>Setup</a>\
</div>\
";

//Boring files
void handleStyle() {
	server.send(200, "text/css","\
		html {\
			font-family:sans-serif;\
		}\
	");
}

/** Handle root or redirect to captive portal */
void handleRoot() {
	if (captivePortal()) { // If caprive portal redirect instead of displaying the page.
		return;
	}
	server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
	server.sendHeader("Pragma", "no-cache");
	server.sendHeader("Expires", "-1");
	server.setContentLength(CONTENT_LENGTH_UNKNOWN);
	server.send(200, "text/html", header);
	server.sendContent("\
		<form method='POST' action='/effect/save'>\
		<select name='e'>\
		<option value='0'>Color Palette Beat</option>\
		<option value='4'>Dot Beat</option>\
		<option value='3'>Confetti</option>\
		<option value='1'>Blink One</option>\
		<option value='2'>Solid One</option>\
		</select>\
		<button type='submit'>Set</button>\
		</form>\
	");
	server.client().stop(); // Stop is needed because we sent no content length
}

void handleEffectSave() {
  Serial.print("[httpd] effect save. ");
  effect = server.arg("e").toInt();
  Serial.println(effect);
  server.sendHeader("Location", "/?ok", true);
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");
  server.send ( 302, "text/plain", "");  // Empty content inhibits Content-length header so we have to close the socket ourselves.
  server.client().stop(); // Stop is needed because we sent no content length
}


/** Redirect to captive portal if we got a request for another domain. Return true in that case so the page handler do not try to handle the request again. */
boolean captivePortal() {
  if (!isIp(server.hostHeader()) && server.hostHeader() != (String(name)+".local")) {
    Serial.println("[httpd] Request redirected to captive portal");
    server.sendHeader("Location", String("http://") + toStringIp(server.client().localIP()), true);
    server.send ( 302, "text/plain", ""); // Empty content inhibits Content-length header so we have to close the socket ourselves.
    server.client().stop(); // Stop is needed because we sent no content length
    return true;
  }
  return false;
}

/** Wifi config page handler */
void handleSetup() {
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", header); 
  server.sendContent("\
		<h1>Setup</h1>\
		<h4>Nearby networks</h4>\
		<table>\
		<tr><th>Name</th><th>Security</th><th>Signal</th></tr>\
  ");
  Serial.println("[httpd] scan start");
  int n = WiFi.scanNetworks();
  Serial.println("[httpd] scan done");
  for (int i = 0; i < n; i++) {
    server.sendContent(String() + "\r\n<tr><td>" + WiFi.SSID(i) + "</td><td>" + String((WiFi.encryptionType(i) == ENC_TYPE_NONE)?"Open":"Encrypted") + "</td><td>" + WiFi.RSSI(i) + "dBm</td></tr>");
  }
  //server.sendContent(String() + "<tr><td>SSID " + String(WiFi.SSID()) + "</td></tr>");
  server.sendContent(String() + "\
		</table>\
		<h4>Connect to a network</h4>\
		<form method='POST' action='/setup/save'>\
			<input type='text' placeholder='network' value='" + String(WiFi.SSID()) + "' name='n'>\
			<input type='password' placeholder='password' value='" + String(WiFi.psk()) + "' name='p'>\
			<button type='submit'>Save and Connect</button>\
		</form>\
  ");
  server.client().stop(); // Stop is needed because we sent no content length
}

/** Handle the WLAN save form and redirect to WLAN config page again */
void handleSetupSave() {
  Serial.print("[httpd]  wifi save. ");
  server.arg("n").toCharArray(ssidTemp, sizeof(ssidTemp) - 1);
  server.arg("p").toCharArray(passwordTemp, sizeof(passwordTemp) - 1);
  Serial.print("ssid: "); Serial.print(ssidTemp);
  Serial.print(" pass: "); Serial.println(passwordTemp);
  doConnect = true;
  WiFi.begin(ssidTemp,passwordTemp); //should also commit to nv
  server.sendHeader("Location", "/?saved", true);
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");
  server.send ( 302, "text/plain", "");  // Empty content inhibits Content-length header so we have to close the socket ourselves.
  server.client().stop(); // Stop is needed because we sent no content length
}

void handleNotFound() {
  if (captivePortal()) { // If caprive portal redirect instead of displaying the error page.
    return;
  }
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += ( server.method() == HTTP_GET ) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";

  for ( uint8_t i = 0; i < server.args(); i++ ) {
    message += " " + server.argName ( i ) + ": " + server.arg ( i ) + "\n";
  }
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");
  server.send ( 404, "text/plain", message );
}

boolean isIp(String str) {
  for (int i = 0; i < str.length(); i++) {
    int c = str.charAt(i);
    if (c != '.' && (c < '0' || c > '9')) {
      return false;
    }
  }
  return true;
}
String toStringIp(IPAddress ip) {
  String res = "";
  for (int i = 0; i < 3; i++) {
    res += String((ip >> (8 * i)) & 0xFF) + ".";
  }
  res += String(((ip >> 8 * 3)) & 0xFF);
  return res;
}
