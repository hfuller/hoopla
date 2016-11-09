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
//Colorpal
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
//EaseMe
bool rev= false;
//FastCirc
int thiscount = 0;
int thisdir = 1;
int thisgap = 8;
//Juggle
uint8_t    numdots =   4;                                     // Number of dots in use.
uint8_t   faderate =   2;                                     // How long should the trails be. Very low value = longer trails.
uint8_t     hueinc =  16;                                     // Incremental change in hue between each dot.
uint8_t     curhue =   0;                                     // The current hue
uint8_t   basebeat =   5;                                     // Higher = faster movement.
//Lightning
uint8_t frequency = 50;                                       // controls the interval between strikes
uint8_t flashes = 8;                                          //the upper limit of flashes per strike
unsigned int dimmer = 1;
uint8_t ledstart;                                             // Starting location of a flash
uint8_t ledlen;                                               // Length of a flash


bool isAP = false;
bool doConnect = false;
bool doServiceRestart = false;
bool doEffects = true;

void runColorpal();
void runConfetti();
void runDotBeat();
void runEaseMe();
void runFastCirc();
void runRotatingRainbow();
void runJuggle();
void runLightning();
void runFill();
void runSolidOne();
void runBlinkOne();
void FillLEDsFromPaletteColors(uint8_t colorIndex);
void SetupRandomPalette();
void runLeds();
void handleRoot();
void handleDebug();
void handleDebugReset();
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
	effect = 2; //solid for status indication
	//Colorpal
	currentPalette = LavaColors_p;                           // RainbowColors_p; CloudColors_p; PartyColors_p; LavaColors_p; HeatColors_p;
	targetPalette = LavaColors_p;                           // RainbowColors_p; CloudColors_p; PartyColors_p; LavaColors_p; HeatColors_p;
	currentBlending = LINEARBLEND;

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
	server.on("/debug", handleDebug);
	server.on("/debug/reset", handleDebugReset);
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

		if ( doEffects && effect <= 2 ) {
			effect = 6;
		}

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
		case 15:
			runColorpal();
			break;
		case 1:
			runBlinkOne();
			break;
		case 2:
			runSolidOne();
			break;
		case 7:
		case 8:
		case 9:
			runConfetti();
			break;
		case 4:
			runDotBeat();
			break;
		case 5:
			runEaseMe();
			break;
		case 6:
			runFastCirc();
			break;
		case 10:
			runRotatingRainbow();
			break;
		case 11:
		case 12:
		case 13:
			runJuggle();
			break;
		case 14:
			runLightning();
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
	
void runColorpal() {
	uint8_t beatA = beat8(30); //, 0, 255); //was beatsin8
	FillLEDsFromPaletteColors(beatA);

	//nblendPaletteTowardPalette( currentPalette, targetPalette, maxChanges);
	EVERY_N_MILLISECONDS(5000) {
		SetupRandomPalette();
	}
}

void runConfetti() {
	EVERY_N_MILLISECONDS(5000) {
		switch(effect) {
			case 7: thisinc=1; thishue=192; thissat=255; thisfade=16; huediff=256; break;  // You can change values here, one at a time , or altogether.
			case 8: thisinc=2; thishue=128; thisfade=8; huediff=64; break;
			case 9: thisinc=1; thishue=random16(255); thisfade=4; huediff=16; break;      // Only gets called once, and not continuously for the next several seconds. Therefore, no rainbows.
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

void runFastCirc() {
	EVERY_N_MILLISECONDS(50) {
		thiscount = (thiscount + thisdir)%thisgap;
		for ( int i=thiscount; i<NUMPIXELS; i+=thisgap ) {
			leds[i] = color;
		}
	}
	fadeToBlackBy(leds, NUMPIXELS, 24);
}

void runEaseMe() {
	static uint8_t easeOutVal = 0;
	static uint8_t easeInVal  = 0;
	static uint8_t lerpVal    = 0;

	easeOutVal = ease8InOutQuad(easeInVal);
	if ( rev ) {
		easeInVal -= 3;
	} else {
		easeInVal += 3;
	}
	if ( easeInVal > 250 ) {
		rev = true;
	} else if ( easeInVal < 5 ) {
		rev = false;
	}

	lerpVal = lerp8by8(0, NUMPIXELS, easeOutVal);

	for ( int i = lerpVal; i < NUMPIXELS; i += 8 ) {
		leds[i] = color;
	}
	fadeToBlackBy(leds, NUMPIXELS, 32);                     // 8 bit, 1 = slow, 255 = fast
}

void runRotatingRainbow() {
	fill_rainbow(leds, NUMPIXELS, count, 5);
	count += 2;
}

void runJuggle() {
	switch(effect) {
			case 11: numdots = 1; basebeat = 20; hueinc = 16; faderate = 2; thishue = 0; break;                  // You can change values here, one at a time , or altogether.
			case 12: numdots = 4; basebeat = 10; hueinc = 16; faderate = 8; thishue = 128; break;
			case 13: numdots = 8; basebeat =  3; hueinc =  0; faderate = 8; thishue=random8(); break;           // Only gets called once, and not continuously for the next several seconds. Therefore, no rainbows.
	}
	curhue = thishue;                                           // Reset the hue values.
	fadeToBlackBy(leds, NUMPIXELS, faderate);
	for( int i = 0; i < numdots; i++) {
		leds[beatsin16(basebeat+i+numdots,0,NUMPIXELS)] += CHSV(curhue, thissat, thisbri);   //beat16 is a FastLED 3.1 function
		curhue += hueinc;
	}
}

void runLightning() {
	ledstart = random8(NUMPIXELS);           // Determine starting location of flash
	ledlen = random8(NUMPIXELS-ledstart);    // Determine length of flash (not to go beyond NUMPIXELS-1)
	for (int flashCounter = 0; flashCounter < random8(3,flashes); flashCounter++) {
		if(flashCounter == 0) dimmer = 5;     // the brightness of the leader is scaled down by a factor of 5
		else dimmer = random8(1,3);           // return strokes are brighter than the leader
		fill_solid(leds+ledstart,ledlen,CHSV(255, 0, 255/dimmer));
		show_at_max_brightness_for_power();                       // Show a section of LED's
		delay(random8(4,10));                 // each flash only lasts 4-10 milliseconds
		fill_solid(leds+ledstart,ledlen,CHSV(255,0,0));   // Clear the section of LED's
		show_at_max_brightness_for_power();     
		if (flashCounter == 0) delay (150);   // longer delay until next flash after the leader
		delay(50+random8(100));               // shorter delay between strokes  
	} // for()
	delay(random8(frequency)*100);          // delay between strikes
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
<meta name='viewport' content='width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no' />\
</head>\
<body>\
<div id='top'>\
	<span id='title'>hoopla</span>\
	<a href='/'>Controls</a>\
	<a href='/setup'>Setup</a>\
	<a href='/debug'>Debug</a>\
</div>\
";

//Boring files
void handleStyle() {
	server.send(200, "text/css","\
		html {\
			font-family:sans-serif;\
			background-color:black;\
			color: #e0e0e0;\
		}\
		div {\
			background-color: #202020;\
		}\
		h1,h2,h3,h4,h5 {\
			color: #e02020;\
		}\
		a {\
			color: #5050f0;\
		}\
		form * {\
			display:block;\
			border: 1px solid #000;\
			font-size: 14px;\
			color: #fff;\
			background: #444;\
			padding: 5px;\
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
		<h1>Controls</h1>\
		<form method='POST' action='/effect/save'>\
		<select name='e' id='e'>\
		<option value='15'>Palette</option>\
		<option value='4'>Dot Beat</option>\
		<option value='5'>Ease Me</option>\
		<option value='6'>FastCirc</option>\
		<option value='7'>Confetti 1</option>\
		<option value='8'>Confetti 2</option>\
		<option value='9'>Confetti 3</option>\
		<option value='10'>Rotating Rainbow</option>\
		<option value='11'>Juggle 1</option>\
		<option value='12'>Juggle 2</option>\
		<option value='13'>Juggle 3</option>\
		<option value='14'>Lightning</option>\
		<option value='1'>Blink One</option>\
		<option value='2'>Solid One</option>\
		</select>\
		<button type='submit'>Set</button>\
		</form>\
		<script>\
		\r\ndocument.getElementById('e').addEventListener('change', function() {\
		\r\n	var xhr = new XMLHttpRequest();\
		\r\n	xhr.open('POST','/effect/save', true);\
		\r\n	xhr.setRequestHeader('Content-type', 'application/x-www-form-urlencoded');\
		\r\n	xhr.send('e=' + this.value);\
		\r\n	xhr.send();\
		\r\n});\
		</script>\
	");
	server.client().stop(); // Stop is needed because we sent no content length
}

void handleDebug() {
	server.setContentLength(CONTENT_LENGTH_UNKNOWN);
	server.send(200, "text/html", header);
	server.sendContent("\
		<h1>Debug</h1>\
		<form method='POST' action='/debug/reset'>\
		<button type='submit'>Restart</button>\
		</form>\
	");
	server.client().stop();
}
void handleDebugReset() {
	server.setContentLength(CONTENT_LENGTH_UNKNOWN);
	server.send(200, "text/html", header);
	server.sendContent("\
		<h1>Debug</h1>\
		OK. Restarting. (Give it up to 30 seconds.)\
	");
	server.client().stop();
	delay(500);
	ESP.reset();
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
