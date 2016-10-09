#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include "FastLED.h"
#include <DNSServer.h>
#include <ESP8266WebServer.h>

#define VERSION			11

#define DEBUG			true
#define Serial			if(DEBUG)Serial		//Only log if we are in debug mode

#define NUMPIXELS		24					//NOTE: we write 300 pixels in some cases, like when blanking the strip.
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

byte effect = 0;
CRGB color = CRGB::Teal;
CRGB nextColor = CRGB::Black;
CRGBPalette16 currentPalette;
CRGBPalette16 targetPalette;
uint8_t maxChanges = 24; 
TBlendType currentBlending;
bool isAP = false;
bool doConnect = false;

void runColorpalBeat();
void runFill();
void runSolidOne();
void runBlinkOne();
void FillLEDsFromPaletteColors(uint8_t colorIndex);
void SetupRandomPalette();
void runLeds();
void handleRoot();
void handleStyle();
void handleWifi();
void handleWifiSave();
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
	server.on("/wifi", handleWifi);
	server.on("/wifisave", handleWifiSave);
	server.onNotFound ( handleNotFound );
	server.begin(); // Web server start

	// Port defaults to 8266
	// ArduinoOTA.setPort(8266);

	// Hostname defaults to esp8266-[ChipID]
	ArduinoOTA.setHostname(name);

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
		}
		if ( (millis() - lastWirelessChange) > 15000 && WiFi.status() != WL_CONNECTED && !isAP ) { //We have been trying to associate for far too long.
			Serial.println("[Wi-Fi] Client giving up");
			WiFi.disconnect();
			Serial.println("[Wi-Fi] Starting wireless AP");
			WiFi.mode(WIFI_AP);
			WiFi.softAP(name,passwordAP);
			delay(100); //reliable IP retrieval.
			Serial.print("[Wi-Fi] AP started. I am "); Serial.println(WiFi.softAPIP());
			lastWirelessChange = millis();
			isAP = true;
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

//Boring files
void handleStyle() {
	server.send(200, "text/plain","\
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
	server.send(200, "text/html", "\
		<!DOCTYPE html>\
		<html>\
		<head>\
		<title>hoopla</title>\
		<link rel=\"stylesheet\" href=\"/style.css\">\
		</head>\
		<body>\
		<h1>HELLO WORLD!!</h1>\
		<p>You may want to <a href='/wifi'>config the wifi connection</a>.</p>\
		</body></html>\
	");
	//server.client().stop(); // Stop is needed because we sent no content length
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
void handleWifi() {
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", ""); // Empty content inhibits Content-length header so we have to close the socket ourselves.
  server.sendContent(
    "<html><head></head><body>"
    "<h1>Wifi config</h1>"
  );
  server.sendContent(
    "\r\n<br />"
    "<table><tr><th align='left'>SoftAP config</th></tr>"
  );
  server.sendContent(String() + "<tr><td>SSID " + String(name) + "</td></tr>");
  server.sendContent(String() + "<tr><td>IP " + toStringIp(WiFi.softAPIP()) + "</td></tr>");
  server.sendContent(
    "</table>"
    "\r\n<br />"
    "<table><tr><th align='left'>WLAN config</th></tr>"
  );
  server.sendContent(String() + "<tr><td>SSID " + String(WiFi.SSID()) + "</td></tr>");
  server.sendContent(String() + "<tr><td>IP " + toStringIp(WiFi.localIP()) + "</td></tr>");
  server.sendContent(
    "</table>"
    "\r\n<br />"
    "<table><tr><th align='left'>WLAN list (refresh if any missing)</th></tr>"
  );
  Serial.println("[httpd] scan start");
  int n = WiFi.scanNetworks();
  Serial.println("[httpd] scan done");
  if (n > 0) {
    for (int i = 0; i < n; i++) {
      server.sendContent(String() + "\r\n<tr><td>SSID " + WiFi.SSID(i) + String((WiFi.encryptionType(i) == ENC_TYPE_NONE)?" ":" *") + " (" + WiFi.RSSI(i) + ")</td></tr>");
    }
  } else {
    server.sendContent(String() + "<tr><td>No WLAN found</td></tr>");
  }
  server.sendContent(
    "</table>"
    "\r\n<br /><form method='POST' action='wifisave'><h4>Connect to network:</h4>"
    "<input type='text' placeholder='network' name='n'/>"
    "<br /><input type='password' placeholder='password' name='p'/>"
    "<br /><input type='submit' value='Connect/Disconnect'/></form>"
    "<p>You may want to <a href='/'>return to the home page</a>.</p>"
    "</body></html>"
  );
  server.client().stop(); // Stop is needed because we sent no content length
}

/** Handle the WLAN save form and redirect to WLAN config page again */
void handleWifiSave() {
  Serial.print("[httpd]  wifi save. ");
  server.arg("n").toCharArray(ssidTemp, sizeof(ssidTemp) - 1);
  server.arg("p").toCharArray(passwordTemp, sizeof(passwordTemp) - 1);
  Serial.print("ssid: "); Serial.print(ssidTemp);
  Serial.print(" pass: "); Serial.println(passwordTemp);
  doConnect = true;
  WiFi.begin(ssidTemp,passwordTemp); //should also commit to nv
  server.sendHeader("Location", "/setup/saved", true);
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
