ADC_MODE(ADC_VCC);

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
#include <ESP8266httpUpdate.h>

//Effects loading
#include "effects.h"

//Hue emulation
#include "SSDP.h"
#include "LightService.h"
#include <aJSON.h>

#define VERSION			53

#define DEBUG			true
#define Serial			if(DEBUG)Serial		//Only log if we are in debug mode

#define TARGET_FRAMERATE		60					//how many frames per second to we ideally want to run
#define BATTERY_LOW_MV			2800
#define BATTERY_DEAD_MV			2550

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

CRGB leds[400];					//NOTE: we write all pixels in some cases, like when blanking the strip.

unsigned long timer1s;
unsigned long frameCount;
unsigned long lastWirelessChange;
double actualFrameRate;

//EFFECT SHIT
EffectManager emgr; //lol
int emgrLoadedCount = 0;
int currentEffectId = 2;
int currentPaletteId = 0;
boolean attractMode = true;
boolean rotateColorFromPalette = true;
EffectState state;

bool isAP = false;
bool doConnect = false;
bool doRestartServices = true;
bool doRestartDevice = false;
bool doEffects = true;
bool allowApMode = true;

void runLeds();
void handleSetupWifiPost();
void handleNotFound();
boolean captivePortal();
boolean isIp(String str);
String toStringIp(IPAddress ip);
CHSV getCHSV(int hue, int sat, int bri);
CHSV getCHSV(const CRGB& color);
void spiffsWrite(String path, String contents);
String spiffsRead(String path);

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
	<a href="/debug">About</a>
	<a href="/update">Update</a>
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
        state.color = newColor;
        //Serial.print("[emhue] R:"); Serial.print(state.color.r); Serial.print("G:"); Serial.print(state.color.g); Serial.print("B:"); Serial.println(state.color.b);
        currentEffectId = 0; //SolidAll
      }
      else
      {
        state.color = CRGB::Black;
		currentEffectId = 0; //SolidAll
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
		spiffsWrite("/name", "hoopla-device");
	}
	spiffsRead("/name").toCharArray(devHostName, 32);
	if ( ! SPIFFS.exists("/psk") ) {
		Serial.println("[start] Setting empty PSK");
		spiffsWrite("/psk", "");
	}
	spiffsRead("/psk").toCharArray(passwordAP, 32);
	Serial.println("[start] Overriding saved PSK to wide-open"); //HACK HACK HACK
	String().toCharArray(passwordAP, 32);
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
	if ( numpixels > (sizeof(leds)/sizeof(leds[0])) ) { //well we can't drive this.
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
	byte ota = EEPROM.read(6);
	if ( ota ) {
		//OTA update was just installed
		Serial.println("[start] We just got OTA'd. Forcing client mode");
		allowApMode = false;
		EEPROM.write(6,0);
	}
	int brightness = EEPROM.read(7);
	if ( brightness < 1 ) {
		Serial.println("[start] 0 is not a valid brightness. Setting it to 10");
		brightness = 10;
		EEPROM.write(7, brightness);
	}
	FastLED.setBrightness(brightness);
	EEPROM.end(); //write the "EEPROM" to flash (on an ESP anyway)

	Serial.print("[start] Starting "); Serial.print(numpixels); Serial.print(" ");
	switch (hardwareType) {
		case 0: //custom type
			FastLED.addLeds<LED_CONFIG>(leds, numpixels);
			Serial.print("Custom LEDs from config.h");
			break;
		case 1: //Wemos D1 Mini with NeoPixel shield
			FastLED.addLeds<NEOPIXEL, 4>(leds, numpixels);
			Serial.print("NeoPixels on D2");
			break;
		case 2: //horizontal LED strip at the shop; cloud at Synapse
			FastLED.addLeds<NEOPIXEL, 5>(leds, numpixels);
			Serial.print("NeoPixels on D1");
			break;
		case 3: //APA102 LED hoop with ESP-01
			FastLED.addLeds<APA102, 0, 2, BGR>(leds, numpixels);
			Serial.print("DotStars on 0,2");
			break;
	}
	Serial.println();
	Serial.print("[start] Setting maximum milliamps to "); Serial.println(maxLoadMilliamps);
	FastLED.setMaxPowerInVoltsAndMilliamps(5,maxLoadMilliamps); //assuming 5V
	FastLED.setCorrection(TypicalSMD5050);
	FastLED.setMaxRefreshRate(TARGET_FRAMERATE);
	FastLED.setDither(0); //For POV stuff, so, yeah. HACK maybe
	for ( int i=0; i<numpixels; i++ ) { //TODO
		leds[i] = CRGB::Black; 
	}
	leds[0] = CRGB::Red; FastLED.show();

	Serial.println("[start] Starting effects");
	currentEffectId = 2; //solid for status indication
	//Palette
	state.currentPalette = RainbowColors_p;                        // RainbowColors_p; CloudColors_p; PartyColors_p; LavaColors_p; HeatColors_p;
	currentPaletteId = 0;
	state.lowPowerMode = false;
	
	Serial.println("[start] Starting bleeding edge effects loader");
	state.color = CRGB::Orange; runLeds();
	emgr = EffectManager(); //is there an echo in here?
	emgrLoadedCount = emgr.getEffectCount();
	Serial.print("[start] loader loaded "); Serial.println(emgrLoadedCount);

	Serial.print("[start] Attempting to associate (STA) to "); Serial.println(WiFi.SSID()); //Serial.print(" with key: "); Serial.println(WiFi.psk());
	WiFi.SSID().toCharArray(ssidTemp, sizeof(ssidTemp) - 1);
	WiFi.psk().toCharArray(passwordTemp, sizeof(passwordTemp) - 1);
	lastWirelessChange = millis();
	WiFi.mode(WIFI_STA);
	WiFi.setAutoReconnect(false);
	WiFi.hostname(devHostName);
	WiFi.begin();

	Serial.print("[start] Starting DNS on "); Serial.println(WiFi.softAPIP());
	dnsServer.setErrorReplyCode(DNSReplyCode::NoError);

	Serial.println("[start] starting http");
	//the following handler is a hack. sorry
	server.on("/update", HTTP_GET, [&](){
		String content = header;
		content += ("<h1>Update</h1>");

		content += (R"(
			<h2>Update from the Internet</h2>
			<p>Updates will immediately download from the Internet, if there are updates available.</p>
			<p>Only click this button if the device is plugged in, or has a full battery.</p>
			<button id="check-for-updates">Check for updates now</button>
			
			<script>
				document.getElementById("check-for-updates").addEventListener("click", function() {
					let xhr = new XMLHttpRequest();
					xhr.addEventListener("error", function(evt) {
						//This transfer will basically ALWAYS return an error when an update is being applied,
						//because the ESP just closes the connection when the update starts!
						console.log(evt);
						alert("Updates are being applied. Do not touch the device until the effects start running again! This could take up to one minute.");
					});
					xhr.addEventListener("load", function() {
						let response = this.responseText;
						if ( response.includes("Wrong HTTP code") ) {
							response = "No updates were necessary."; //HACK HACK HACK
						}

						if ( response.length == 0 ) {
							alert("Updates are being applied. Do not touch the device until the effects start running again! This could take up to one minute.");
						} else {
							alert("Updates were not applied, for this reason: " + response);
						}	
					});
					xhr.open("POST", "/update/check", true);
					xhr.send();
				});
			</script>
		)");
		
		content += R"(
			<h2>Update from a file</h2>
			<p>If you have a software file for this device on your computer, upload it here.</p>
			<p>Feedback will be displayed on the LEDs regarding the progress of the update.</p>
			<form method='POST' action='/update' enctype='multipart/form-data'>
				<input type='file' name='update'>
				<input type='submit' value='Do it'>
			</form>
		)";

		server.send(200, "text/html", content);
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
			currentEffectId = 2;
			state.color = CRGB::OrangeRed;

			//doRestartServices = true; //why would we do this when we are about to reboot anyway
			WiFiUDP::stopAll();

			uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
			if(!Update.begin(maxSketchSpace)){//start with max available size
				//if(DEBUG) Update.printError(Serial);
				state.color = CRGB::Red;
			}
		} else if(upload.status == UPLOAD_FILE_WRITE){
			Serial.print(upload.totalSize); Serial.printf(" bytes written\r");
			runLeds();

			state.intEffectState = ( upload.totalSize / ( 350000 / numpixels ) );
			if ( state.intEffectState >= numpixels ) {
				state.intEffectState = numpixels-1;
			}

			if(Update.write(upload.buf, upload.currentSize) != upload.currentSize){
				//if(DEBUG) Update.printError(Serial);
				state.color = CRGB::Red;
			}
		} else if(upload.status == UPLOAD_FILE_END){
			if(Update.end(true)){ //true to set the size to the current progress
				Serial.printf("Update Success: %u\n", upload.totalSize);
				state.color = CRGB::Yellow;
				runLeds();
				doRestartDevice = true;
				EEPROM.begin(256);
				EEPROM.write(6,1);
				EEPROM.end(); //notify that we just installed an update OTA.
			} else {
				//if(DEBUG) Update.printError(Serial);
				state.color = CRGB::Red;
			}
		} else if(upload.status == UPLOAD_FILE_ABORTED){
			Update.end();
			Serial.println("Update was aborted");
			state.color = CRGB::Red;
		}
		delay(0);
	});
	server.on("/style.css", [&](){
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
				color: #f05050;
				margin-left:12px;
			}
			form *, button {
				display:block;
				border: 1px solid #000;
				font-size: 14px;
				color: #fff;
				background: #444;
				padding: 5px;
				margin-bottom:12px;
			}
			#color-buttons {
				display:table;
				max-width:144px;
			}
			.color-button {
				width:36px;
				height:36px;
				display:inline;
				margin:0px !important;
			}
		)");
	});
	server.on("/", [&](){
		if (captivePortal()) { // If caprive portal redirect instead of displaying the page.
			return;
		}
		server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
		server.sendHeader("Pragma", "no-cache");
		server.sendHeader("Expires", "-1");

		String content = header;
		content += R"(
			<h1>Controls</h1>
			<form method="PUT" action="/effects/current">
			<select name="id" id="id">
		)";
		for ( int i=0; i < emgrLoadedCount; i++ ) {
			content += String("<option value=\"") + i + "\">" + emgr.getEffect(i).name + "</option>";
		}
		content += R"(
			</select>
			<select name="palette" id="palette">
		)";
		for ( int i=0; i < emgr.getPaletteCount(); i++ ) {
			content += String("<option value=\"") + i + "\">" + emgr.getPalette(i).name + "</option>";
		}
		content += R"(
			</select>
			<!-- <button type="submit">Set</button> -->
			<div id="color-buttons"></div>
			<button id="color-rotate">Rotate automatically through palette colors</button>
			</form>
			<script>
				function setEffect() {
					let xhr = new XMLHttpRequest();
					xhr.open("PUT","/effects/current", true);
					xhr.setRequestHeader("Content-type", "application/x-www-form-urlencoded");
					xhr.send("id=" + document.getElementById("id").value);
				}
				function setPalette() {
					let xhr = new XMLHttpRequest();
					xhr.addEventListener("load", loadColors); //reload the colors in case the palette just changed
					xhr.open("PUT","/palettes/current", true);
					xhr.setRequestHeader("Content-type", "application/x-www-form-urlencoded");
					xhr.send("id=" + document.getElementById("palette").value);
				}
				function setColorToThis(event) {
					console.log(this.style.backgroundColor);
					rgb = this.style.backgroundColor.match(/^rgba?[\s+]?\([\s+]?(\d+)[\s+]?,[\s+]?(\d+)[\s+]?,[\s+]?(\d+)[\s+]?/i);
					let r = rgb[1];
					let g = rgb[2];
					let b = rgb[3];
					
					let xhr = new XMLHttpRequest();
					xhr.open("PUT", "/color", true);
					xhr.setRequestHeader("Content-type", "application/x-www-form-urlencoded");
					xhr.send("r=" + r + "&g=" + g + "&b=" + b);

					event.preventDefault();					
				}
				function setColorToRotate(event) {
					let xhr = new XMLHttpRequest();
					xhr.open("PUT", "/color", true);
                                        xhr.setRequestHeader("Content-type", "application/x-www-form-urlencoded");
                                        xhr.send("rotate=true");
					event.preventDefault();
				}
				function loadColors() {
					let xhr = new XMLHttpRequest();
					xhr.addEventListener("load", function() {
						let container = document.getElementById("color-buttons");
						while ( container.firstChild ) {
							container.removeChild(container.firstChild);
						} //done removing all elements from the container.

						data = JSON.parse(this.responseText);
						data.colors.forEach(function(color) {
							let el = document.createElement("button");
							el.className = "color-button";
							el.style.backgroundColor = color;
							el.addEventListener("click", setColorToThis);
							container.appendChild(el);
						});
					});
					xhr.open("GET", "/palettes/current", true);
					xhr.send();
				}

				document.getElementById("id").addEventListener("change", setEffect);
				document.getElementById("palette").addEventListener("change", setPalette);
				document.getElementById("color-rotate").addEventListener("click", setColorToRotate);
				
				let xhrE = new XMLHttpRequest();
				xhrE.addEventListener("load", function() {
					data = JSON.parse(this.responseText);
					console.log(data);
					document.getElementById("id").value = data.id;
				});
				xhrE.open("GET", "/effects/current", true);
				xhrE.send();

				let xhrP = new XMLHttpRequest();
				xhrP.addEventListener("load", function() {
					data = JSON.parse(this.responseText);
					console.log(data);
					document.getElementById("palette").value = data.id;
					loadColors();
				});
				xhrP.open("GET", "/palettes/current", true);
				xhrP.send();
				
			</script>
		)";
		server.send(200, "text/html", content);
	});
	server.on("/effects", HTTP_GET, [&](){
		String content = "[";
		
		for ( int i=0; i < emgrLoadedCount; i++ ) {
			String json = String() + "{\"id\":" + String(i) + ", \"name\":\"" + emgr.getEffect(i).name + "\"}";
			content += json;
			if ( i < emgrLoadedCount-1 ) {
				content += ", ";
			}
		}
		content += ("]");

		server.send(200, "application/json", content);
	});
	server.on("/color", HTTP_PUT, [&](){
		state.color.r = server.arg("r").toInt();
		state.color.g = server.arg("g").toInt();
		state.color.b = server.arg("b").toInt();
		rotateColorFromPalette = ( ( server.arg("rotate").length() > 0 ) ? true : false );
		
		server.send(200, "text/plain", "");
		server.client().stop();
	});
	server.on("/effects/current", HTTP_GET, [&](){
		String json = String() + "{\"id\":" + String(currentEffectId) + ", \"name\":\"" + emgr.getEffect(currentEffectId).name + "\"}";
		
		server.send(200, "text/html", json);
		server.client().stop();
	});
	server.on("/effects/current", HTTP_PUT, [&](){
		Serial.print("[httpd] effect save. ");
		currentEffectId = server.arg("id").toInt();
		if ( currentEffectId < 0 ) { //HACK to enable attract mode
			currentEffectId = 3; //move past the single LED ones.
			attractMode = true;
		} else {
			attractMode = false;
		}
		Serial.print(currentEffectId); Serial.print(" ");

		server.sendHeader("Location", "/?ok", true);
		server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
		server.sendHeader("Pragma", "no-cache");
		server.sendHeader("Expires", "-1");
		server.send ( 302, "text/plain", "");  // Empty content inhibits Content-length header so we have to close the socket ourselves.
		server.client().stop(); // Stop is needed because we sent no content length
	});
	server.on("/palettes/current", HTTP_GET, [&](){
		String json = String("{\"id\":") + String(currentPaletteId) + ", \"name\":\"" + emgr.getPalette(currentPaletteId).name + "\", \"colors\":[";
		for ( int i=0; i<16; i++ ) {
			json += "\"#";

			CRGB clr = state.currentPalette[i];
			String cmp;
			
			cmp = String(clr.r, HEX);
			if ( cmp.length() < 2 ) { json += "0"; }
			json += cmp;
			cmp = String(clr.g, HEX);
			if ( cmp.length() < 2 ) { json += "0"; }
			json += cmp;
			cmp = String(clr.b, HEX);
			if ( cmp.length() < 2 ) { json += "0"; }
			json += cmp;

			json += "\"";

			if ( i < 16-1 ) {
				json += ", ";
			}
		}
		json += "]}";
		
                server.send(200, "text/html", json);
		server.client().stop();
	});	
	server.on("/palettes/current", HTTP_PUT, [&](){
		int paletteId = server.arg("id").toInt();
		currentPaletteId = paletteId;
		state.currentPalette = emgr.getPalette(paletteId).palette;
		Serial.println(paletteId);
		
                server.send(200, "text/html", "true");
		server.client().stop();
	});
	server.on("/setup", [&](){
		server.setContentLength(CONTENT_LENGTH_UNKNOWN);
		server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
		server.sendHeader("Pragma", "no-cache");
		server.sendHeader("Expires", "-1");
		server.sendHeader("Content-Length", "-1");
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
			server.sendContent(String() + "\r\n<tr onclick=\"document.getElementById('ssidinput').value=this.firstChild.firstChild.innerHTML; setTimeout(function(){ document.getElementById('pskinput').focus(); }, 100);\"><td><a href=\"#setup-wifi\">" + WiFi.SSID(i) + "</a></td><td>" + String((WiFi.encryptionType(i) == ENC_TYPE_NONE)?"Open":"Encrypted") + "</td><td>" + WiFi.RSSI(i) + "dBm</td></tr>");
		}
		server.sendContent(String() + "\
			</table>\
			<h4>Connect to a network</h4>\
			<form method='POST' id='setup-wifi' action='/setup/wifi'>\
				<input type='text' id='ssidinput' placeholder='network' value='" + String(WiFi.SSID()) + "' name='n'>\
				<input type='password' id='pskinput' placeholder='password' value='" + String(WiFi.psk()) + "' name='p'>\
				<button type='submit'>Save and Connect</button>\
			</form>\
		");

		server.sendContent(String() + R"(
			<h4>Device setup</h4>
			<form method="POST" id="setup-device" action="/setup/device">
				<input name="name" placeholder="Device name" value=")" + spiffsRead("/name") + R"(">
				<button type="submit">Save</button>
			</form>
		)");

		EEPROM.begin(256);
		byte hardwareType = EEPROM.read(1);
		byte brightness = EEPROM.read(7);
		EEPROM.end(); //HACK HACK HACK?

		server.sendContent(String() + R"(
			<h4>LED setup</h4>
			<h5>Don't touch this stuff!</h5>
			<form method="POST" action="/setup/leds">
				<select name="hardware_type" id="hardware_type"><!-- )" + hardwareType + R"( -->
					<option value="0" )" + (hardwareType==0 ? "selected" : "") + R"(>Custom setup from config.h (set at build)</option>
					<option value="1" )" + (hardwareType==1 ? "selected" : "") + R"(>NeoPixels on GPIO4(D2) (Wemos D1 Mini with NeoPixel shield)</option>
					<option value="2" )" + (hardwareType==2 ? "selected" : "") + R"(>NeoPixels on GPIO5(D1) (Makers Local 256; Synapse Wireless)</option>
					<option value="3" )" + (hardwareType==3 ? "selected" : "") + R"(>DotStars on GPIO0(D3)/GPIO2(D4) (JC/JB Hoop)</option>
				</select>
				<input name="numpixels" placeholder="Number of LEDs" value=")" + numpixels + R"(">
				<input name="maxLoadMilliamps" placeholder="Maximum milliamps to draw" value=")" + maxLoadMilliamps + R"(">
				<input name="brightness" placeholder="Brightness (1-255) " value=")" + brightness + R"(">
				<button type="submit">Save</button>
			</form>
		)");
		server.client().stop();
	});
	server.on("/setup/wifi", HTTP_POST, handleSetupWifiPost);
	server.on("/setup/leds", HTTP_POST, [&](){
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

		int brightness = server.arg("brightness").toInt();
		FastLED.setBrightness(brightness);
		EEPROM.write(7, brightness);
	
		EEPROM.end(); //write it out on an ESP
		send302("/setup?saved");
		Serial.println("done. "); 
	});
	server.on("/setup/device", HTTP_POST, [&](){
		Serial.print("[httpd] Device settings post. ");
		spiffsWrite("/name", server.arg("name"));
		
		send302("/setup?saved");
		Serial.println("done.");
	});
	server.on("/debug", [&](){
		String content = header;
		content += ("<h1>About</h1><ul>");

		unsigned long uptime = millis();
		content += (String("<li>Version ") + VERSION + "</li>");
		content += (String("<li>Booted about ") + (uptime/60000) + " minutes ago (" + ESP.getResetReason() + ")</li>");
		content += (String("<li>Battery: ") + getAdjustedVcc() + "mV (Raw: " + ESP.getVcc() + ")</li>");
		content += (String("<li>Goal: ") + TARGET_FRAMERATE + "fps, Actual: " + actualFrameRate + "fps</li>");
		content += ("</ul>");
	
		content += (R"(
			<h2>Debugging buttons (don't touch)</h2>
			<form method="POST" action="/debug/lowpowermode">
				<button type="submit">Toggle low power mode now</button>
			</form>
			<form method='POST' action='/debug/reset'>
				<button type='submit'>Restart</button>
			</form>
			<form method="POST" action="/debug/sleepforever">
				<button type="submit">Simulate dead battery - go to sleep forever</button>
			</form>
			<form method='POST' action='/debug/disconnect'>
				<button type='submit'>Forget connection info</button>
			</form>
		)");
		server.send(200, "text/html", content);
	});
	server.on("/debug/lowpowermode", [&](){
		send302("/debug?done");
		state.lowPowerMode = !(state.lowPowerMode);
	});
	server.on("/debug/reset", [&](){
		send302("/debug?done");
		doRestartDevice = true;
	});
	server.on("/debug/sleepforever", [&](){
		send302("/debug?done");
		sleepForever();
	});
	server.on("/debug/disconnect", [&](){
		send302("/debug?done");
		delay(500);
		WiFi.disconnect();
	});
	server.on("/debug/test", [&](){
		server.setContentLength(CONTENT_LENGTH_UNKNOWN);
		server.send(200, "text/html", "Testing stuff... ");

		uint32_t flashChipSize = ESP.getFlashChipSize();
		String platform = "generic";
		if ( flashChipSize == 4194304 ) {
			platform = "nodemcu"; //HACK HACK HACK
		}
		String url = String("http://update.pixilic.com/hoopla.ino.") + platform + ".bin";
		String versionHeader = String("{\"flashChipSize\":") + String(flashChipSize) + ", \"version\":" + String(VERSION) + ", \"name\":\"" + String(devHostName) + "\"}";
		server.sendContent(url);
		server.sendContent(versionHeader);

		server.sendContent("done.");
		server.client().stop();
	});
	server.on("/version.json", [&](){
		server.send(200, "text/html", String(VERSION));
		server.client().stop();
	});
	server.onNotFound ( handleNotFound );
	server.begin(); // Web server start

	state.color = CRGB::Yellow; runLeds();
	Serial.println("[start] Setting up OTA");
	// Port defaults to 8266
	//ArduinoOTA.setPort(8266);
	// Hostname defaults to esp8266-[ChipID]
	ArduinoOTA.setHostname(devHostName);
	// No authentication by default
	//ArduinoOTA.setPassword((const char *)"123");
	ArduinoOTA.onStart([]() {
		currentEffectId = 1;
		state.color = CRGB::OrangeRed;
		Serial.println("Starting OTA update. Other functions will be suspended.");
	});
	ArduinoOTA.onEnd([]() {
		currentEffectId = 2;
		state.color = CRGB::Yellow;
		runLeds();
		
		EEPROM.begin(256);
		EEPROM.write(6,1);
		EEPROM.end(); //notify that we just installed an update OTA.
		
		Serial.println("\nOTA update complete. Reloading");
	});
	ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
		if ( leds[0] == CRGB(0,0,0) ) {
			state.color = CRGB::OrangeRed; 
	    } else {
	        state.color = CRGB::Black;
	    }
		runLeds();

		Serial.printf("OTA progress: %u%%\r", (progress / (total / 100)));
	});
	ArduinoOTA.onError([](ota_error_t error) {
		state.color = CRGB::Red;
		Serial.printf("OTA Error[%u]: ", error);
		if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
		else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
		else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
		else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
		else if (error == OTA_END_ERROR) Serial.println("End Failed");
	});

	Serial.println("[start] Starting HTTP Update Checker");
	ESPhttpUpdate.rebootOnUpdate(false);
	server.on("/update/check", [&](){
		int oldEffect = currentEffectId;
		
		currentEffectId = 2;
		runLeds();
		//WiFiUDP::stopAll();
		
		boolean result = phoneHome();
		if ( result && ESPhttpUpdate.getLastError() == 0 ) { //we updated and there wasn't an error
			state.intEffectState = numpixels-1;
			state.color = CRGB::Yellow;
			runLeds();
		} else {
			Serial.println("[check] Updater returned that an update wasn't performed");
			currentEffectId = oldEffect;
		}

		String resultStr = ESPhttpUpdate.getLastErrorString();
		if ( result == false && resultStr.length() < 1 ) {
			resultStr = "No updates were necessary.";
		}

                server.send(200, "text/html", resultStr);
		server.client().stop();

        });

	state.color = CRGB::Green; runLeds();
	currentEffectId = 6;

	Serial.println("[start] Checking battery");
	checkBattery(false); //don't actually shut down even if we are dead. We will do that later

	delay(1000);

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

		//time to do our every-second tasks

		#ifdef DEBUG
		actualFrameRate = (double)frameCount/((double)(millis()-timer1s)/1000);
		Serial.print("[Hbeat] FRAME RATE: "); Serial.print(actualFrameRate);
		uint32_t loadmw = calculate_unscaled_power_mW(leds,numpixels);
		Serial.print(" - LOAD: "); Serial.print(loadmw); Serial.print("mW ("); Serial.print(loadmw/5); Serial.print("mA) - ");
		Serial.print("Wi-Fi: "); Serial.print( (WiFi.status() == WL_CONNECTED) ? "Connected" : "Disconnected");
		Serial.println();
		#endif /*DEBUG*/

		timer1s = millis();
		frameCount = 0;

		/*
		if ( currentEffectId <= 2 && millis() < 10000 ) {
			//we are stuck in a status display
			currentEffectId = 6;
		}
		*/

	}
	EVERY_N_MILLISECONDS(5000) {

		if ( doRestartDevice ) {
			ESP.restart();
		}

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
			if ( WiFi.status() != WL_CONNECTED && !isAP && allowApMode ) { //We have been trying to associate for far too long.
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

				Serial.println("[Wi-Fi] Starting DNS poisoning");
				dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

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

	EVERY_N_MILLISECONDS(10000) {
		checkBattery(true); //shutdown if we are dead

		if ( attractMode ) {
			do {
				currentEffectId = (currentEffectId+1) % emgrLoadedCount; //wrap around if we're over the loaded count
			} while ( ! emgr.getEffect(currentEffectId).useForAttractMode ); //Skip any effects that don't want to be seen in attract mode
		}
	}

	if ( rotateColorFromPalette ) {
		uint8_t beatA = beat8(30);
		state.color = ColorFromPalette(state.currentPalette, beatA);
	}

	runLeds();

}

void runLeds() {
	
	//WOW this function got way simpler than it used to be. I just want to talk about that.

	frameCount++; //for frame rate measurement
	emgr.getEffect(currentEffectId).run(&state); //Pass our state struct to the effect.
	show_at_max_brightness_for_power(); //FastLED.show();

}


//HTTP STUFF borrowed from https://github.com/esp8266/Arduino/blob/master/libraries/DNSServer/examples/CaptivePortalAdvanced/CaptivePortalAdvanced.ino

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

/** Handle the WLAN save form and redirect to WLAN config page again */
void handleSetupWifiPost() {
  Serial.print("[httpd]  wifi save. ");
  server.arg("n").toCharArray(ssidTemp, sizeof(ssidTemp) - 1);
  server.arg("p").toCharArray(passwordTemp, sizeof(passwordTemp) - 1);
  Serial.print("ssid: "); Serial.print(ssidTemp);
  Serial.print(" pass: "); Serial.println(passwordTemp);
  server.sendHeader("Location", "/setup?saved", true);
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");
  server.send ( 302, "text/plain", "");  // Empty content inhibits Content-length header so we have to close the socket ourselves.
  server.client().stop(); // Stop is needed because we sent no content length
  doConnect = true;

  //Commenting this out because the 'doConnect' process will do it.
  //WiFi.begin(ssidTemp,passwordTemp); //should also commit to nv
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

void spiffsWrite(String path, String contents) {
	File f = SPIFFS.open(path, "w");
	f.print(contents);
	f.close();
}
String spiffsRead(String path) {
	File f = SPIFFS.open(path, "r");
	String x = f.readStringUntil('\n');
	f.close();
	return x;
}

boolean phoneHome() {

	Serial.println("[chkup] checking for updates");

	//include the current version as the x-ESP8266-version header.
	uint32_t flashChipSize = ESP.getFlashChipSize();
	String platform = "generic";
	if ( flashChipSize == 4194304 ) {
		platform = "nodemcu"; //HACK HACK HACK
	}
	String url = String("http://update.pixilic.com/hoopla.ino.") + platform + ".bin";
	String versionHeader = String("{\"flashChipSize\":") + String(flashChipSize) + ", \"version\":" + String(VERSION) + ", \"name\":\"" + devHostName + "\"}";

	HTTPUpdateResult ret = ESPhttpUpdate.update(url, versionHeader);
	switch(ret) {
		case HTTP_UPDATE_OK:
			Serial.printf("[chkup] Update Success");
			EEPROM.begin(256);
			EEPROM.write(6,1);
			EEPROM.end(); //notify that we just installed an update OTA.
			doRestartDevice = true;
			return true;
			break;
		default:
			Serial.println("[chkup] Update failed, or there is no update.");
			return false;
			break;
	}
}

uint16_t getAdjustedVcc() {
	return ESP.getVcc()+500;
}

void send302(String dest) {
	server.sendHeader("Location", dest, true);
	server.send ( 302, "text/plain", "");
	server.client().stop();
}

void sleepForever() {
	Serial.println("[sleep] Sleeping forever.");

	ESP.deepSleep(0, WAKE_RF_DEFAULT); //this will sleep forever
}

void checkBattery(boolean shutdownIfDead) {
	uint16_t voltage = getAdjustedVcc();
	if ( voltage < BATTERY_DEAD_MV ) {
		Serial.println("[Hbeat] Battery is dead!!!");
		state.color = CRGB::Red;
		state.intEffectState = 0;
		currentEffectId = 2;
		runLeds();
	
		if ( shutdownIfDead ) {
			sleepForever();
		}
	} else if ( voltage < BATTERY_LOW_MV ) {
		Serial.println("[Hbeat] Battery is low...");
		if ( currentEffectId == 2 ) { //If we are showing a status indication
			state.color = CRGB::Yellow; //turn it yellow
		}
		state.lowPowerMode = true;
	}
}
