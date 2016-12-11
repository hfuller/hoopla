#include "config.h"

//Tell FastLED to use the raw ESP GPIO pin numbers.
//We have to do that before we load the FastLED library
#define FASTLED_ESP8266_RAW_PIN_ORDER

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <FastLED.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <FS.h>
#include <EEPROM.h>

//Effects loading
#include "effects.h"

//Hue emulation
#include "SSDP.h"
#include "LightService.h"
#include <aJSON.h>

#define VERSION			32

#define DEBUG			true
#define Serial			if(DEBUG)Serial		//Only log if we are in debug mode

#define FRAMERATE		60					//how many frames per second to we ideally want to run

const char* ssid = "";
const char* password = "";
char ssidTemp[32] = "";
char passwordTemp[32] = "";
char devHostName[32];
char passwordAP[32];
int numpixels = 1;
int maxLoadMilliamps = 400;

const byte DNS_PORT = 53;
DNSServer dnsServer;
ESP8266WebServer server(80);

CRGB leds[300];					//NOTE: we write 300 pixels in some cases, like when blanking the strip.

unsigned long timer1s;
unsigned long frameCount;
unsigned long lastWirelessChange;

//EFFECT SHIT
Effects effects; //lol
byte effect = 0;
CRGB color = CRGB::Teal;
CRGB nextColor = CRGB::Black;
CRGBPalette16 currentPalette;
//internal effect state vars follow...
bool boolEffectState; 
int intEffectState;


bool isAP = false;
bool doConnect = false;
bool doRestartServices = true;
bool doRestartDevice = false;
bool doEffects = true;

void runLeds();
void handleRoot();
void handleDebug();
void handleDebugReset();
void handleDebugDisconnect();
void handleEffectSave();
void handleStyle();
void handleSetup();
void handleSetupWifiPost();
void handleSetupLedsPost();
void handleNotFound();
boolean captivePortal();
boolean isIp(String str);
String toStringIp(IPAddress ip);
CHSV getCHSV(int hue, int sat, int bri);
CHSV getCHSV(const CRGB& color);

const char * header = R"(<!DOCTYPE html>
<html>
<head>
<title>hoopla</title>
<link rel="stylesheet" href="/style.css">
<meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no" />
</head>
<body>
<div id="top">
	<span id="title">hoopla</span>
	<a href="/">Controls</a>
	<a href="/setup">Setup</a>
	<a href="/debug">Debug</a>
</div>
)";

//Hue emulation handler
class StripHandler : public LightHandler {
  private:
    HueLightInfo _info;
  public:
    StripHandler() {
      //Give some values in the constructor so, when some Hue API user
	  //asks for our current status, it looks like we are lit with some color.
	  _info.on = true;
	  _info.brightness = 200;
    }

    void handleQuery(int lightNumber, HueLightInfo newInfo, aJsonObject* raw) {
      CHSV newColor = getCHSV(newInfo.hue, newInfo.saturation, newInfo.brightness);
      CHSV originalColor = getCHSV(leds[lightNumber]);
      _info = newInfo;

      if (newInfo.on)
      {
        aJsonObject* pattern = aJson.getObjectItem(raw, "pattern");
        if (_info.effect == EFFECT_COLORLOOP) {
          Serial.println("[emhue] Effect was set to color loop");
          return;
        } else if (pattern) {
          Serial.println("[emhue] Setting pattern");
          // pattern is an array of color settings objects
          // apply to first pattern_len lights
          int pattern_len = aJson.getArraySize(pattern);
          for (int i = 0; i < pattern_len && i < numpixels; i++) {
            aJsonObject* elem = aJson.getArrayItem(pattern, i);
            HueLightInfo elemInfo;
            parseHueLightInfo(newInfo, elem, &elemInfo);
            int num_patterns = ((numpixels - i - 1) / pattern_len) + 1;
            int brightness = elemInfo.brightness;
            if (!elemInfo.on) {
              brightness = 0;
            }
            for (int n = 0; n < num_patterns; n++) {
              int light_index = n * pattern_len + i;
              // no fade for patterns
              leds[light_index] = CHSV(elemInfo.hue, elemInfo.saturation, brightness);
            }
          }
          return;
        }
        Serial.println("[emhue] Changing color");
        //Serial.print("[emhue] H:"); Serial.print(newColor.h); Serial.print("S:"); Serial.print(newColor.s); Serial.print("V:"); Serial.println(newColor.v);
        color = newColor;
        //Serial.print("[emhue] R:"); Serial.print(color.r); Serial.print("G:"); Serial.print(color.g); Serial.print("B:"); Serial.println(color.b);
        effect = 3; //SolidAll
      }
      else
      {
        color = CRGB::Black;
		effect = 3; //SolidAll
      }
    }

    HueLightInfo getInfo(int lightNumber) {
      //TODO: Fill in this handler to return our actual color, or something.
      return _info;
    }
};



void setup() {
	
	Serial.begin(115200);
	Serial.print("[start] hoopla v"); Serial.println(VERSION);

	Serial.print("[start] Starting SPIFFS. ");
	FSInfo fs_info;
	SPIFFS.begin();
	Serial.print("info: ");
	if ( ! SPIFFS.info(fs_info) ) {
		//the FS info was not retrieved correctly. it's probably not formatted
		Serial.println("unformatted");
		Serial.println("[start] Formatting SPIFFS. This could take a long time!");
		SPIFFS.format();
		SPIFFS.begin();
		Serial.print("[start] spiffs format done, reloading info... ");
		SPIFFS.info(fs_info);
	}
	Serial.print(fs_info.usedBytes); Serial.print("/"); Serial.print(fs_info.totalBytes); Serial.println(" bytes used");
	
	Serial.println("[start] Loading configuration from spiffs");
	File f;
	if ( ! SPIFFS.exists("/name") ) {
		Serial.println("[start] Setting default host name");
		f = SPIFFS.open("/name", "w");
		f.println("hoopla-device");
		f.close();
	}
	f = SPIFFS.open("/name", "r");
	f.readStringUntil('\n').toCharArray(devHostName, 32);
	f.close();
	if ( ! SPIFFS.exists("/psk") ) {
		Serial.println("[start] Setting empty PSK");
		f = SPIFFS.open("/psk", "w");
		f.println();
		f.close();
	}
	f = SPIFFS.open("/psk", "r");
	f.readStringUntil('\n').toCharArray(passwordAP, 32);
	f.close();
	Serial.print("[start] Hello from "); Serial.println(devHostName);		

	Serial.println("[start] Loading configuration from eeprom");
	EEPROM.begin(256);
	byte x = EEPROM.read(0);
	if ( x != VERSION ) {
		//we just got upgrayedded or downgrayedded
		Serial.print("[start] We just moved from v"); Serial.println(x);
		EEPROM.write(0,VERSION);
		Serial.print("[start] Welcome to v"); Serial.println(VERSION);
	}
	byte hardwareType = EEPROM.read(1);
	if ( hardwareType > 3 ) {
		Serial.println("[start] Resetting hardware type");
		EEPROM.write(1, 0);
		hardwareType = 0;
	}
	numpixels = (EEPROM.read(2)*256)+(EEPROM.read(3)); //math devilry
	if ( numpixels > 300 ) { //absurd
		Serial.println("[start] Resetting number of pixels");
		EEPROM.write(2, 0);   //number of pixels (MSB)
		EEPROM.write(3, 100); //number of pixels (LSB)
		numpixels = 100;
	}
	maxLoadMilliamps = (EEPROM.read(4)*256)+(EEPROM.read(5));
	if ( maxLoadMilliamps < 400 || maxLoadMilliamps > 20000 ) { //absurd
		Serial.println("[start] Resetting maximum amperage");
		maxLoadMilliamps = 400;
		EEPROM.write(4, (maxLoadMilliamps>>8) & 0xFF);   //MSB
		EEPROM.write(5, maxLoadMilliamps & 0xFF); //LSB
	}
	EEPROM.end(); //write the "EEPROM" to flash (on an ESP anyway)

	Serial.print("[start] Starting "); Serial.print(numpixels); Serial.print(" ");
	switch (hardwareType) {
		case 0: //custom type
			FastLED.addLeds<LED_CONFIG>(leds, 300);
			Serial.print("Custom LEDs from config.h");
			break;
		case 1: //Wemos D1 Mini with NeoPixel shield
			FastLED.addLeds<NEOPIXEL, 4>(leds, 300);
			Serial.print("NeoPixels on D2");
			break;
		case 2: //horizontal LED strip at the shop; cloud at Synapse
			FastLED.addLeds<NEOPIXEL, 5>(leds, 300);
			Serial.print("NeoPixels on D1");
			break;
		case 3: //APA102 LED hoop with ESP-01
			FastLED.addLeds<APA102, 0, 2, BGR>(leds, 300);
			Serial.print("DotStars on 0,2");
			break;
	}
	Serial.println();
	Serial.print("[start] Setting maximum milliamps to "); Serial.println(maxLoadMilliamps);
	FastLED.setMaxPowerInVoltsAndMilliamps(5,maxLoadMilliamps); //assuming 5V
	FastLED.setCorrection(TypicalSMD5050);
	FastLED.setMaxRefreshRate(FRAMERATE);
	for ( int i=0; i<300; i++ ) {
		leds[i] = CRGB::Black;
	}
	leds[0] = CRGB::Red; FastLED.show();

	Serial.println("[start] Starting effects");
	effect = 2; //solid for status indication
	//Palette
	currentPalette = LavaColors_p;                           // RainbowColors_p; CloudColors_p; PartyColors_p; LavaColors_p; HeatColors_p;
	
	Serial.println("[start] Starting bleeding edge effects loader");
	color = CRGB::Orange; runLeds();
	effects = Effects(); //is there an echo in here?

	Serial.print("[start] Attempting to associate (STA) to "); Serial.print(WiFi.SSID()); Serial.print(" with key: "); Serial.println(WiFi.psk());
	WiFi.SSID().toCharArray(ssidTemp, sizeof(ssidTemp) - 1);
	WiFi.psk().toCharArray(passwordTemp, sizeof(passwordTemp) - 1);
	lastWirelessChange = millis();
	WiFi.mode(WIFI_STA);
	WiFi.setAutoReconnect(false);
	WiFi.hostname(devHostName);
	WiFi.begin();

	Serial.print("[start] Starting DNS on "); Serial.println(WiFi.softAPIP());
	dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
	dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

	Serial.println("[start] Setting up http firmware uploads");
	//the following handler is a hack. sorry
	server.on("/update", HTTP_GET, [&](){
		server.sendHeader("Connection", "close");
		server.sendHeader("Access-Control-Allow-Origin", "*");
		server.send(200, "text/html", R"(
<html><body><form method='POST' action='/update' enctype='multipart/form-data'>
<input type='file' name='update'>
<input type='submit' value='Update'>
</form>
</body></html>
		)");
	});
	// handler for the /update form POST (once file upload finishes)
	server.on("/update", HTTP_POST, [&](){
		server.sendHeader("Connection", "close");
		server.sendHeader("Access-Control-Allow-Origin", "*");
		server.send(200, "text/plain", (Update.hasError())?"FAIL":"OK");
	},[&](){
		// handler for the file upload, get's the sketch bytes, and writes
		// them through the Update object
		HTTPUpload& upload = server.upload();
		if(upload.status == UPLOAD_FILE_START){
			Serial.printf("Starting HTTP update from %s - other functions will be suspended.\r\n", upload.filename.c_str());
			effect = 2;
			color = CRGB::OrangeRed;

			//doRestartServices = true; //why would we do this when we are about to reboot anyway
			WiFiUDP::stopAll();

			uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
			if(!Update.begin(maxSketchSpace)){//start with max available size
				//if(DEBUG) Update.printError(Serial);
				color = CRGB::Red;
			}
		} else if(upload.status == UPLOAD_FILE_WRITE){
			Serial.print(upload.totalSize); Serial.printf(" bytes written\r");
			runLeds();

			offset = ( upload.totalSize / ( 350000 / numpixels ) );
			if ( offset >= numpixels ) {
				offset = numpixels-1;
			}

			if(Update.write(upload.buf, upload.currentSize) != upload.currentSize){
				//if(DEBUG) Update.printError(Serial);
				color = CRGB::Red;
			}
		} else if(upload.status == UPLOAD_FILE_END){
			if(Update.end(true)){ //true to set the size to the current progress
				Serial.printf("Update Success: %u\n", upload.totalSize);
				color = CRGB::Yellow;
				runLeds();
				doRestartDevice = true;
			} else {
				//if(DEBUG) Update.printError(Serial);
				color = CRGB::Red;
			}
		} else if(upload.status == UPLOAD_FILE_ABORTED){
			Update.end();
			Serial.println("Update was aborted");
			color = CRGB::Red;
		}
		delay(0);
	});

	Serial.println("[start] starting http");
	server.on("/style.css", handleStyle);
	server.on("/", [&](){
		if (captivePortal()) { // If caprive portal redirect instead of displaying the page.
			return;
		}
		server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
		server.sendHeader("Pragma", "no-cache");
		server.sendHeader("Expires", "-1");
		server.setContentLength(CONTENT_LENGTH_UNKNOWN);
		server.send(200, "text/html", header);
		server.sendContent(R"(
			<h1>Controls</h1>
			<form method="POST" action="/effect/save">
			<select name="e" id="e">
				<option value="1">Blink One</option>
				<option value="2">Solid One</option>
				<option value="3">Solid All</option>
				<option value="4">Dot Beat</option>
				<option value="5">Ease Me</option>
				<option value="6">FastCirc</option>
				<option value="7">Confetti 1</option>
				<option value="8">Confetti 2</option>
				<option value="9">Confetti 3</option>
				<option value="10">Rotating Rainbow</option>
				<option value="11">Juggle 1</option>
				<option value="12">Juggle 2</option>
				<option value="13">Juggle 3</option>
				<option value="14">Lightning</option>
				<option value="15">Solid Palette: Angry Cloud</option>
				<option value="16">Rotating Palette: Angry Cloud</option>
				<option value="100">Event loader subsystem beta</option>
			</select>
			<button type="submit">Set</button>
			</form>
			<script>
				document.getElementById("e").addEventListener("change", function() {
					var xhr = new XMLHttpRequest();
					xhr.open("POST","/effect/save", true);
					xhr.setRequestHeader("Content-type", "application/x-www-form-urlencoded");
					xhr.send("e=" + this.value);
					xhr.send();
				});
			</script>
		)");
		server.client().stop(); // Stop is needed because we sent no content length
	});
	server.on("/effect/save", handleEffectSave);
	server.on("/setup", handleSetup);
	server.on("/setup/wifi", HTTP_POST, handleSetupWifiPost);
	server.on("/setup/leds", HTTP_POST, handleSetupLedsPost);
	server.on("/debug", handleDebug);
	server.on("/debug/reset", handleDebugReset);
	server.on("/debug/disconnect", handleDebugDisconnect);
	server.on("/debug/test", [&](){
		server.setContentLength(CONTENT_LENGTH_UNKNOWN);
		server.send(200, "text/html", "Loading effects... ");
		Effects effects = Effects();
		server.sendContent("add result: ");
		server.sendContent(String(effects.add("Test2", [&](){
			Serial.println("Test effect 2");
		})));
		for ( int i=0; i < effects.getCount(); i++ ) {
			server.sendContent(effects.get(i).name + ", ");
		}
		server.sendContent("done.");
		server.client().stop();
	});
	server.on("/version.json", [&](){
		server.setContentLength(CONTENT_LENGTH_UNKNOWN);
		server.send(200, "text/html", String(VERSION));
		server.client().stop();
	});
	server.onNotFound ( handleNotFound );
	server.begin(); // Web server start

	color = CRGB::Yellow; runLeds();
	Serial.println("[start] Setting up OTA");
	// Port defaults to 8266
	//ArduinoOTA.setPort(8266);
	// Hostname defaults to esp8266-[ChipID]
	ArduinoOTA.setHostname(devHostName);
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
	
	color = CRGB::Green; runLeds();
	Serial.println("[start] Startup complete.");
}

void loop() {
	
	server.handleClient();
	
	if ( ! doRestartServices ) {
		//The services have been started at least once.
		//If we don't wait until they have been started,
		//the service will blow up because it tries to
		//work without the setup functions being run first.
		ArduinoOTA.handle();
		dnsServer.processNextRequest();
		LightService.update();
	}
	
	EVERY_N_MILLISECONDS(1000) {

		if ( doRestartDevice ) {
			ESP.restart();
		}

		//time to do our every-second tasks
		#ifdef DEBUG
		double fr = (double)frameCount/((double)(millis()-timer1s)/1000);
		Serial.print("[Hbeat] FRAME RATE: "); Serial.print(fr);
		uint32_t loadmw = calculate_unscaled_power_mW(leds,numpixels);
		Serial.print(" - LOAD: "); Serial.print(loadmw); Serial.print("mW ("); Serial.print(loadmw/5); Serial.print("mA) - ");
		Serial.print("Wi-Fi: "); Serial.print( (WiFi.status() == WL_CONNECTED) ? "Connected" : "Disconnected");
		Serial.println();
		#endif /*DEBUG*/

		timer1s = millis();
		frameCount = 0;

		if ( effect <= 2 && millis() < 10000 ) {
			effect = 6;
		}

	}
	EVERY_N_MILLISECONDS(5000) {

		//do Wi-Fi stuff

		#ifdef DEBUG
		Serial.print("[Wi-Fi] ");
		if ( WiFi.status() == WL_CONNECTED ) {
			//we are connected, presumably.
			Serial.print("WL_CONNECTED ");
			if ( WiFi.localIP() == IPAddress(0,0,0,0) ) {
				doConnect = true;
			}
		} else if ( WiFi.status() == WL_IDLE_STATUS ) {
			Serial.print("WL_IDLE_STATUS ");
		} else if ( WiFi.status() == WL_DISCONNECTED ) {
			Serial.print("WL_DISCONNECTED ");
		} else {
			Serial.print("(unknown state) ");
		}
		Serial.print(WiFi.SSID());
		Serial.print(" as "); Serial.print(WiFi.localIP());
		Serial.print(" at "); Serial.println(WiFi.RSSI());
		#endif /*DEBUG*/

		if ( (millis() - lastWirelessChange) > 60000 ) {
			//We tried to associate to wireless over 60 seconds ago.
			if ( ( WiFi.status() != WL_CONNECTED || WiFi.localIP() == IPAddress(0,0,0,0) ) && !isAP ) {
				//We were connected for at least a minute, but we must have lost connection.
				//In the first case, the SDK reports we are disconnected.
				//In the second case, the SDK claims we are associated but we are totally not	
				doConnect = true;
			}
		} else if ( (millis() - lastWirelessChange) > 15000 ) {
			//We tried to associate to wireless somewhere between 15 and 60 seconds ago.
			if ( WiFi.status() != WL_CONNECTED && !isAP ) { //We have been trying to associate for far too long.
				Serial.println("[Wi-Fi] Client giving up");
				//WiFi.disconnect(); //Don't do this or it will clear the ssid/pass in nvram!!!!!
				Serial.println("[Wi-Fi] Starting wireless AP");
				WiFi.mode(WIFI_AP);
				WiFi.softAP(devHostName,passwordAP);
				delay(100); //reliable IP retrieval.
				Serial.print("[Wi-Fi] AP started. I am "); Serial.println(WiFi.softAPIP());
				lastWirelessChange = millis();
				isAP = true;
				doRestartServices = true;
			}
			if ( doRestartServices ) {
				Serial.println("[Wi-Fi] Restarting services due to Wi-Fi state change");

				Serial.println("[Wi-Fi] Setting up OTA");
				ArduinoOTA.begin();

				Serial.println("[Wi-Fi] Setting up Philips Hue emulation");
				LightService.begin(&server);
				LightService.setLightsAvailable(1);
				LightService.setLightHandler(0, new StripHandler());

				doRestartServices = false;
			}
		}

		if ( doConnect ) { //we have a pending connect attempt from the config subsystem
			Serial.println("[Wi-Fi] Restarting wireless");
			isAP = false;
			WiFi.disconnect();
			WiFi.mode(WIFI_STA);
			WiFi.begin(ssidTemp,passwordTemp);
			doConnect = false; //shouldn't need this but sometimes we do... if WiFi.status() isn't updated by the underlying libs
			lastWirelessChange = millis();
			doRestartServices = true; //restart OTA
		}

	}

	runLeds();

}

void runLeds() {
	
	//WOW this function got way simpler than it used to be. I just want to talk about that.

	frameCount++; //for frame rate measurement
	effects.get(effect).run();
	show_at_max_brightness_for_power(); //FastLED.show();

}


//HTTP STUFF borrowed from https://github.com/esp8266/Arduino/blob/master/libraries/DNSServer/examples/CaptivePortalAdvanced/CaptivePortalAdvanced.ino

//Boring files
void handleStyle() {
	server.send(200, "text/css",R"(
		html {
			font-family:sans-serif;
			background-color:black;
			color: #e0e0e0;
		}
		div {
			background-color: #202020;
		}
		h1,h2,h3,h4,h5 {
			color: #e02020;
		}
		a {
			color: #5050f0;
		}
		form * {
			display:block;
			border: 1px solid #000;
			font-size: 14px;
			color: #fff;
			background: #444;
			padding: 5px;
		}
	)");
}

/** Handle root or redirect to captive portal */
void handleRoot() {
}

void handleDebug() {
	server.setContentLength(CONTENT_LENGTH_UNKNOWN);
	server.send(200, "text/html", header);
	server.sendContent("\
		<h1>Debug</h1>\
		<form method='POST' action='/debug/reset'>\
		<button type='submit'>Restart</button>\
		</form>\
		<form method='POST' action='/debug/disconnect'>\
		<button type='submit'>Forget connection info</button>\
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
	doRestartDevice = true;
}
void handleDebugDisconnect() {
	server.setContentLength(CONTENT_LENGTH_UNKNOWN);
	server.send(200, "text/html", header);
	server.sendContent("\
		<h1>Debug</h1>\
		OK. Disconnecting.\
	");
	server.client().stop();
	delay(500);
	WiFi.disconnect();
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
  if (!isIp(server.hostHeader()) && server.hostHeader() != (String(devHostName)+".local")) {
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
    server.sendContent(String() + "\r\n<tr onclick=\"document.getElementById('ssidinput').value=this.firstChild.innerHTML;\"><td>" + WiFi.SSID(i) + "</td><td>" + String((WiFi.encryptionType(i) == ENC_TYPE_NONE)?"Open":"Encrypted") + "</td><td>" + WiFi.RSSI(i) + "dBm</td></tr>");
  }
  //server.sendContent(String() + "<tr><td>SSID " + String(WiFi.SSID()) + "</td></tr>");
  server.sendContent(String() + "\
		</table>\
		<h4>Connect to a network</h4>\
		<form method='POST' action='/setup/wifi'>\
			<input type='text' id='ssidinput' placeholder='network' value='" + String(WiFi.SSID()) + "' name='n'>\
			<input type='password' placeholder='password' value='" + String(WiFi.psk()) + "' name='p'>\
			<button type='submit'>Save and Connect</button>\
		</form>\
  ");
  server.sendContent(R"(
<h4>LED setup</h4>
<form method="POST" action="/setup/leds">
	LED type:
	<select name="hardware_type" id="hardware_type">
		<option value="0">Custom setup from config.h (set at build)</option>
		<option value="1">NeoPixels on GPIO4(D2) (Wemos D1 Mini with NeoPixel shield)</option>
		<option value="2">NeoPixels on GPIO5(D1) (Makers Local 256; Synapse Wireless)</option>
		<option value="3">DotStars on GPIO0(D3)/GPIO2(D4) (JC/JB Hoop)</option>
	</select>
	<input name="numpixels" placeholder="Number of LEDs">
	<input name="maxLoadMilliamps" placeholder="Maximum milliamps to draw">
	<button type="submit">Save</button>
</form>
  )");
  server.client().stop(); // Stop is needed because we sent no content length
}

/** Handle the WLAN save form and redirect to WLAN config page again */
void handleSetupWifiPost() {
  Serial.print("[httpd]  wifi save. ");
  server.arg("n").toCharArray(ssidTemp, sizeof(ssidTemp) - 1);
  server.arg("p").toCharArray(passwordTemp, sizeof(passwordTemp) - 1);
  Serial.print("ssid: "); Serial.print(ssidTemp);
  Serial.print(" pass: "); Serial.println(passwordTemp);
  server.sendHeader("Location", "/?saved", true);
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");
  server.send ( 302, "text/plain", "");  // Empty content inhibits Content-length header so we have to close the socket ourselves.
  server.client().stop(); // Stop is needed because we sent no content length
  doConnect = true;

  //Commenting this out because the 'doConnect' process will do it.
  //WiFi.begin(ssidTemp,passwordTemp); //should also commit to nv
}

void handleSetupLedsPost() {
	Serial.print("[httpd] LED settings post. ");
	EEPROM.begin(256);

	EEPROM.write(1, server.arg("hardware_type").toInt());	
	Serial.print(EEPROM.read(1)); Serial.print("->1 ");

	numpixels = server.arg("numpixels").toInt();
	EEPROM.write(2, (numpixels>>8) & 0xFF);   //number of pixels (MSB)
	EEPROM.write(3, numpixels & 0xFF); //number of pixels (LSB)
	Serial.print(EEPROM.read(2)); Serial.print("->2 ");
	Serial.print(EEPROM.read(3)); Serial.print("->3 ");

	maxLoadMilliamps = server.arg("maxLoadMilliamps").toInt();
	EEPROM.write(4, (maxLoadMilliamps>>8) & 0xFF);
	EEPROM.write(5, maxLoadMilliamps & 0xFF);

	EEPROM.end(); //write it out on an ESP
	server.sendHeader("Location", "/?saved", true);
	server.send ( 302, "text/plain", "");
	server.client().stop();
	Serial.println("done. "); 
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

CHSV getCHSV(int hue, int sat, int bri) {
  //TODO: This is stupid inefficient (especially with regards to H value). Fix it.
  Serial.print("[gCHSV] H:"); Serial.print(hue); Serial.print("S:"); Serial.print(sat); Serial.print("V:"); Serial.println(bri);
  float H, S, B;
  H = ((float)hue) / 182.04 / 360.0;
  S = ((float)sat) / COLOR_SATURATION;
  B = ((float)bri) / COLOR_SATURATION;
  return CHSV(H*255, S*255, B*255);
}
CHSV getCHSV(const CRGB& color) { //from neopixelbus
    // convert colors to float between (0.0 - 1.0)
    float r = color.r / 255.0f;
    float g = color.g / 255.0f;
    float b = color.b / 255.0f;

    float max = (r > g && r > b) ? r : (g > b) ? g : b;
    float min = (r < g && r < b) ? r : (g < b) ? g : b;

    float d = max - min;

    float h = 0.0; 
    float v = max;
    float s = (v == 0.0f) ? 0 : (d / v);

    if (d != 0.0f)
    {
        if (r == max)
        {
            h = (g - b) / d + (g < b ? 6.0f : 0.0f);
        }
        else if (g == max)
        {
            h = (b - r) / d + 2.0f;
        }
        else
        {
            h = (r - g) / d + 4.0f;
        }
        h /= 6.0f;
    }

    return CHSV(h,s,v);
    return CHSV(h,s,v);
}
