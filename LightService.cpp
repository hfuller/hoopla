#include "LightService.h"
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiUdp.h>
#include "SSDP.h"
#include <aJSON.h> // Replace avm/pgmspace.h with pgmspace.h there and set #define PRINT_BUFFER_LEN 4096 ################# IMPORTANT
#include <NeoPixelBus.h> // NeoPixelAnimator branch

String macString;
String bridgeIDString;
String ipString;
String netmaskString;
String gatewayString;
// Determines whether a client is already authorized
bool isAuthorized = false;
// The username of the client (currently we authorize all clients simulating a pressed button on the bridge)
String client;

ESP8266WebServer * httpServer;

LightServiceClass LightService;

LightHandler *lightHandlers[MAX_LIGHT_HANDLERS]; // interfaces exposed to the outside world

bool LightServiceClass::setLightHandler(int index, LightHandler *handler) {
  if (index >= currentNumLights || index < 0) return false;
  lightHandlers[index] = handler;
  return true;
}

bool LightServiceClass::setLightsAvailable(int lights) {
  if (lights <= MAX_LIGHT_HANDLERS) {
    currentNumLights = lights;
    return true;
  }
  return false;
}

int LightServiceClass::getLightsAvailable() {
  return currentNumLights;
}

String StringIPaddress(IPAddress myaddr)
{
  String LocalIP = "";
  for (int i = 0; i < 4; i++)
  {
    LocalIP += String(myaddr[i]);
    if (i < 3) LocalIP += ".";
  }
  return LocalIP;
}

LightHandler *LightServiceClass::getLightHandler(int numberOfTheLight) {
  if (numberOfTheLight >= currentNumLights || numberOfTheLight < 0) {
    return nullptr;
  }

  if (!lightHandlers[numberOfTheLight]) {
    return new LightHandler();
  }

  return lightHandlers[numberOfTheLight];
}

void handleAllOthers();

static const char* _ssdp_response_template =
  "HTTP/1.1 200 OK\r\n"
  "EXT:\r\n"
  "CACHE-CONTROL: max-age=%u\r\n" // SSDP_INTERVAL
  "LOCATION: http://%u.%u.%u.%u:%u/%s\r\n" // WiFi.localIP(), _port, _schemaURL
  "SERVER: Arduino/1.0 UPNP/1.1 %s/%s\r\n" // _modelName, _modelNumber
  "hue-bridgeid: %s\r\n"
  "ST: %s\r\n"  // _deviceType
  "USN: uuid:%s\r\n" // _uuid
  "\r\n";

static const char* _ssdp_notify_template =
  "NOTIFY * HTTP/1.1\r\n"
  "HOST: 239.255.255.250:1900\r\n"
  "NTS: ssdp:alive\r\n"
  "CACHE-CONTROL: max-age=%u\r\n" // SSDP_INTERVAL
  "LOCATION: http://%u.%u.%u.%u:%u/%s\r\n" // WiFi.localIP(), _port, _schemaURL
  "SERVER: Arduino/1.0 UPNP/1.1 %s/%s\r\n" // _modelName, _modelNumber
  "hue-bridgeid: %s\r\n"
  "NT: %s\r\n"  // _deviceType
  "USN: uuid:%s\r\n" // _uuid
  "\r\n";

int ssdpMsgFormatCallback(SSDPClass *ssdp, char *buffer, int buff_len,
                          bool isNotify, int interval, char *modelName,
                          char *modelNumber, char *uuid, char *deviceType,
                          uint32_t ip, uint16_t port, char *schemaURL) {
  if (isNotify) {
    return snprintf(buffer, buff_len,
      _ssdp_notify_template,
      interval,
      IP2STR(&ip), port, schemaURL,
      modelName, modelNumber,
      bridgeIDString.c_str(),
      deviceType,
      uuid);
  } else {
    return snprintf(buffer, buff_len,
      _ssdp_response_template,
      interval,
      IP2STR(&ip), port, schemaURL,
      modelName, modelNumber,
      "001788FFFE142F92",
      deviceType,
      uuid);
  }
}

void LightServiceClass::begin(ESP8266WebServer * svr) {
  macString = String(WiFi.macAddress());
  bridgeIDString = macString;
  bridgeIDString.replace(":", "");
  bridgeIDString = bridgeIDString.substring(0, 6) + "FFFE" + bridgeIDString.substring(6);
  ipString = StringIPaddress(WiFi.localIP());
  netmaskString = StringIPaddress(WiFi.subnetMask());
  gatewayString = StringIPaddress(WiFi.gatewayIP());

  Serial.println("Setting up HTTP");

  httpServer = svr;

  httpServer->onNotFound(handleAllOthers);

  Serial.println("Starting SSDP...");
  SSDP.begin();
  SSDP.setSchemaURL((char*)"description.xml");
  SSDP.setHTTPPort(80);
  SSDP.setName((char*)"Philips hue clone");
  SSDP.setSerialNumber((char*)"001788102201");
  SSDP.setURL((char*)"index.html");
  SSDP.setModelName((char*)"IpBridge");
  SSDP.setModelNumber((char*)"0.1");
  SSDP.setModelURL((char*)"http://www.meethue.com");
  SSDP.setManufacturer((char*)"Royal Philips Electronics");
  SSDP.setManufacturerURL((char*)"http://www.philips.com");
  SSDP.setDeviceType((char*)"upnp:rootdevice");
  SSDP.setMessageFormatCallback(ssdpMsgFormatCallback);
  Serial.println("SSDP Started");
}

void LightServiceClass::update() {
  httpServer->handleClient();
}

void sendJson(aJsonObject *root)
{
  // Take aJsonObject and print it to Serial and to WiFi
  // From https://github.com/pubnub/msp430f5529/blob/master/msp430f5529.ino
  char *msgStr = aJson.print(root);
  aJson.deleteItem(root);
  Serial.println(millis());
  Serial.println(msgStr);
  httpServer->send(200, "text/plain", msgStr);
  free(msgStr);
}

// ==============================================================================================================
// Color Conversion
// ==============================================================================================================
// TODO: Consider switching to something along the lines of
// https://github.com/patdie421/mea-edomus/blob/master/src/philipshue_color.c
// and/ or https://github.com/kayno/arduinolifx/blob/master/color.h
// for color coversions instead
// ==============================================================================================================

// Based on http://stackoverflow.com/questions/22564187/rgb-to-philips-hue-hsb
// The code is based on this brilliant note: https://github.com/PhilipsHue/PhilipsHueSDK-iOS-OSX/commit/f41091cf671e13fe8c32fcced12604cd31cceaf3

RgbColor getXYtoRGB(float x, float y, int brightness_raw) {
  float brightness = ((float)brightness_raw) / 255.0f;
  float bright_y = brightness / y;
  float X = x * bright_y;
  float Z = (1 - x - y) * bright_y;

  // convert to RGB (0.0-1.0) color space
  float R = X * 1.4628067 - brightness * 0.1840623 - Z * 0.2743606;
  float G = -X * 0.5217933 + brightness * 1.4472381 + Z * 0.0677227;
  float B = X * 0.0349342 - brightness * 0.0968930 + Z * 1.2884099;

  // apply inverse 2.2 gamma
  float inv_gamma = 1.0 / 2.4;
  float linear_delta = 0.055;
  float linear_interval = 1 + linear_delta;
  float r = R <= 0.0031308 ? 12.92 * R : (linear_interval) * pow(R, inv_gamma) - linear_delta;
  float g = G <= 0.0031308 ? 12.92 * G : (linear_interval) * pow(G, inv_gamma) - linear_delta;
  float b = B <= 0.0031308 ? 12.92 * B : (linear_interval) * pow(B, inv_gamma) - linear_delta;

  return RgbColor(r * COLOR_SATURATION,
                  g * COLOR_SATURATION,
                  b * COLOR_SATURATION);
}

int getHue(HsbColor hsb) {
  return hsb.H * 360 * 182.04;
}

int getSaturation(HsbColor hsb) {
  return hsb.S * COLOR_SATURATION;
}

RgbColor getMirektoRGB(int mirek) {
  int hectemp = 10000 / mirek;
  int r, g, b;
  if (hectemp <= 66) {
    r = COLOR_SATURATION;
    g = 99.4708025861 * log(hectemp) - 161.1195681661;
    b = hectemp <= 19 ? 0 : (138.5177312231 * log(hectemp - 10) - 305.0447927307);
  } else {
    r = 329.698727446 * pow(hectemp - 60, -0.1332047592);
    g = 288.1221695283 * pow(hectemp - 60, -0.0755148492);
    b = COLOR_SATURATION;
  }
  r = r > COLOR_SATURATION ? COLOR_SATURATION : r;
  g = g > COLOR_SATURATION ? COLOR_SATURATION : g;
  b = b > COLOR_SATURATION ? COLOR_SATURATION : b;
  return RgbColor(r, g, b);
}

void sendError(int type, String path, String description) {
  aJsonObject *root = aJson.createArray();
  aJsonObject *errorContainer = aJson.createObject();
  aJsonObject *errorObject = aJson.createObject();
  aJson.addItemToObject(errorObject, "type", aJson.createItem(type));
  aJson.addStringToObject(errorObject, "address", path.c_str());
  aJson.addStringToObject(errorObject, "description", description.c_str());
  aJson.addItemToObject(errorContainer, "error", errorObject);
  aJson.addItemToArray(root, errorContainer);
  sendJson(root);
}

void sendSuccess(String id, String value) {
  aJsonObject *search = aJson.createArray();
  aJsonObject *container = aJson.createObject();
  aJson.addItemToArray(search, container);
  aJsonObject *succeed = aJson.createObject();
  aJson.addItemToObject(container, "success", succeed);
  aJson.addStringToObject(succeed, id.c_str(), value.c_str());
  sendJson(search);
}

void sendUpdated() {
  httpServer->send(200, "text/plain", "Updated.");
}

bool parseHueLightInfo(HueLightInfo currentInfo, aJsonObject *parsedRoot, HueLightInfo *newInfo) {
  *newInfo = currentInfo;
  aJsonObject* onState = aJson.getObjectItem(parsedRoot, "on");
  if (onState) {
    newInfo->on = onState->valuebool;
  }

  // pull brightness
  aJsonObject* briState = aJson.getObjectItem(parsedRoot, "bri");
  if (briState) {
    newInfo->brightness = briState->valueint;
  }

  // pull effect
  aJsonObject* effectState = aJson.getObjectItem(parsedRoot, "effect");
  if (effectState) {
    const char *effect = effectState->valuestring;
    if (!strcmp(effect, "colorloop")) {
      newInfo->effect = EFFECT_COLORLOOP;
    } else {
      newInfo->effect = EFFECT_NONE;
    }
  }
  // pull alert
  aJsonObject* alertState = aJson.getObjectItem(parsedRoot, "alert");
  if (alertState) {
    const char *alert = alertState->valuestring;
    if (!strcmp(alert, "select")) {
      newInfo->alert = ALERT_SELECT;
    } else if (!strcmp(alert, "lselect")) {
      newInfo->alert = ALERT_LSELECT;
    } else {
      newInfo->alert = ALERT_NONE;
    }
  }

  aJsonObject* hueState = aJson.getObjectItem(parsedRoot, "hue");
  aJsonObject* satState = aJson.getObjectItem(parsedRoot, "sat");
  aJsonObject* ctState = aJson.getObjectItem(parsedRoot, "ct");
  aJsonObject* xyState = aJson.getObjectItem(parsedRoot, "xy");
  if (xyState) {
    aJsonObject* elem0 = aJson.getArrayItem(xyState, 0);
    aJsonObject* elem1 = aJson.getArrayItem(xyState, 1);
    if (!elem0 || !elem1) {
      sendError(5, "/api/api/lights/?/state", "xy color coordinates incomplete");
      return false;
    }
    HsbColor hsb = getXYtoRGB(elem0->valuefloat, elem1->valuefloat, newInfo->brightness);
    newInfo->hue = getHue(hsb);
    newInfo->saturation = getSaturation(hsb);
  } else if (ctState) {
    int mirek = ctState->valueint;
    if (mirek > 500 || mirek < 153) {
      sendError(7, "/api/api/lights/?/state", "Invalid vaule for color temperature");
      return false;
    }

    HsbColor hsb = getMirektoRGB(mirek);
    newInfo->hue = getHue(hsb);
    newInfo->saturation = getSaturation(hsb);
  } else if (hueState || satState) {
    if (hueState) newInfo->hue = hueState->valueint;
    if (satState) newInfo->saturation = satState->valueint;
  }
  return true;
}

void addLightJson(aJsonObject* root, int numberOfTheLight, LightHandler *lightHandler) {
  if (!lightHandler) return;
  String lightName = "" + (String) (numberOfTheLight + 1);
  aJsonObject *light;
  aJson.addItemToObject(root, lightName.c_str(), light = aJson.createObject());
  aJson.addStringToObject(light, "type", "Extended color light"); // type of lamp (all "Extended colour light" for now)
  aJson.addStringToObject(light, "name",  ("Hue LightStrips " + (String) (numberOfTheLight + 1)).c_str()); // // the name as set through the web UI or app
  aJson.addStringToObject(light, "modelid", "LST001"); // the model number
  aJsonObject *state;
  aJson.addItemToObject(light, "state", state = aJson.createObject());
  HueLightInfo info = lightHandler->getInfo(numberOfTheLight);
  aJson.addBooleanToObject(state, "on", info.on);
  aJson.addNumberToObject(state, "hue", info.hue); // hs mode: the hue (expressed in ~deg*182.04)
  aJson.addNumberToObject(state, "bri", info.brightness); // brightness between 0-254 (NB 0 is not off!)
  aJson.addNumberToObject(state, "sat", info.saturation); // hs mode: saturation between 0-254
  double numbers[2] = {0.0, 0.0};
  aJson.addItemToObject(state, "xy", aJson.createFloatArray(numbers, 2)); // xy mode: CIE 1931 color co-ordinates
  aJson.addNumberToObject(state, "ct", 500); // ct mode: color temp (expressed in mireds range 154-500)
  aJson.addStringToObject(state, "alert", "none"); // 'select' flash the lamp once, 'lselect' repeat flash for 30s
  aJson.addStringToObject(state, "effect", info.effect == EFFECT_COLORLOOP ? "colorloop" : "none");
  aJson.addStringToObject(state, "colormode", "hs"); // the current color mode
  aJson.addBooleanToObject(state, "reachable", true); // lamp can be seen by the hub
}

void addLightsJson(aJsonObject *lights) {
  for (int i = 0; i < LightService.getLightsAvailable(); i++) {
    addLightJson(lights, i, LightService.getLightHandler(i));
  }
}

void addConfigJson(aJsonObject *root)
{
  aJson.addStringToObject(root, "name", "hue emulator");
  aJson.addStringToObject(root, "swversion", "81012917");
  aJson.addStringToObject(root, "bridgeid", bridgeIDString.c_str());
  aJson.addBooleanToObject(root, "portalservices", false);
  aJson.addBooleanToObject(root, "linkbutton", true);
  aJson.addStringToObject(root, "mac", macString.c_str());
  aJson.addBooleanToObject(root, "dhcp", true);
  aJson.addStringToObject(root, "ipaddress", ipString.c_str());
  aJson.addStringToObject(root, "netmask", netmaskString.c_str());
  aJson.addStringToObject(root, "gateway", gatewayString.c_str());
  aJson.addStringToObject(root, "apiversion", "1.3.0");
  aJsonObject *whitelist;
  aJson.addItemToObject(root, "whitelist", whitelist = aJson.createObject());
  aJsonObject *whitelistFirstEntry;
  aJson.addItemToObject(whitelist, client.c_str(), whitelistFirstEntry = aJson.createObject());
  aJson.addStringToObject(whitelistFirstEntry, "name", "clientname#devicename");
  aJsonObject *swupdate;
  aJson.addItemToObject(root, "swupdate", swupdate = aJson.createObject());
  aJson.addStringToObject(swupdate, "text", "");
  aJson.addBooleanToObject(swupdate, "notify", false); // Otherwise client app shows update notice
  aJson.addNumberToObject(swupdate, "updatestate", 0);
  aJson.addStringToObject(swupdate, "url", "");
}

String trimSlash(String uri) {
  if (uri.startsWith("/")) {
    uri.remove(0, 1);
  }
  return uri;
}

void configHandler(String user, String uri) {
  aJsonObject *root;
  root = aJson.createObject();
  addConfigJson(root);
  sendJson(root);
}

void authHandler(String user, String uri) {
  // On the real bridge, the link button on the bridge must have been recently pressed for the command to execute successfully.
  // We try to execute successfully regardless of a button for now.
  isAuthorized = true; // FIXME: Instead, we should persist (save) the username and put it on the whitelist
  String username = "api";
  Serial.println("CLIENT: ");
  Serial.println(username);

  sendSuccess("username", username);
}

void lightsHandler(String user, String uri) {
  uri = trimSlash(uri.substring(6));
  if (uri == "") {
    switch (httpServer->method()) {
      case HTTP_GET: {
          // dump existing lights
          aJsonObject *lights = aJson.createObject();
          addLightsJson(lights);
          sendJson(lights);
          break;
        }
      case HTTP_POST: {
          // "start" a "search" for "new" lights
          sendSuccess("/lights", "Searching for new devices");
          break;
        }
    }
    return;
  }
  if (uri == "new") {
    // dump empty object
    aJsonObject *lights = aJson.createObject();
    sendJson(lights);
    return;
  }

  // individual light state request
  if (uri.endsWith("state")) {
    // remove "/state"
    String lightText = uri.substring(0, uri.length() - 6);
    int numberOfTheLight = atoi(lightText.c_str()) - 1;

    Serial.print("JSON Body:");
    Serial.println(httpServer->arg("plain"));
    aJsonObject* parsedRoot = aJson.parse(( char*) httpServer->arg("plain").c_str());
    LightHandler *handler = LightService.getLightHandler(numberOfTheLight);
    if (!handler) {
      sendError(3, uri, "Requested light not available");
      return;
    }
    HueLightInfo currentInfo = handler->getInfo(numberOfTheLight);
    if (parsedRoot) {
      HueLightInfo newInfo;
      if (!parseHueLightInfo(currentInfo, parsedRoot, &newInfo)) {
        aJson.deleteItem(parsedRoot);
        return;
      }
      handler->handleQuery(numberOfTheLight, newInfo, parsedRoot);
      aJson.deleteItem(parsedRoot);
    } else if (httpServer->arg("plain") != "") {
      // unparseable json
      sendError(2, "groups/0/action", "Bad JSON body in request");
      return;
    }

    aJsonObject *root;
    root = aJson.createObject();
    addLightJson(root, numberOfTheLight, handler);
    sendJson(root);
  }
}

aJsonObject *validateGroupCreateBody(String body) {
  aJsonObject *root = aJson.parse(( char*) body.c_str());
  aJsonObject* jName = aJson.getObjectItem(root, "name");
  aJsonObject* jLights = aJson.getObjectItem(root, "lights");
  if (!jName || !jLights) {
    return nullptr;
  }
  return root;
}

void applyConfigToLightMask(unsigned int lights) {
  Serial.print("JSON Body:");
  Serial.println(httpServer->arg("plain"));
  aJsonObject* parsedRoot = aJson.parse(( char*) httpServer->arg("plain").c_str());
  if (parsedRoot) {
    for (int i = 0; i < LightService.getLightsAvailable(); i++) {
      LightHandler *handler = LightService.getLightHandler(i);
      HueLightInfo currentInfo = handler->getInfo(i);
      HueLightInfo newInfo;
      if (parseHueLightInfo(currentInfo, parsedRoot, &newInfo)) {
        handler->handleQuery(i, newInfo, parsedRoot);
      }
    }
    aJson.deleteItem(parsedRoot);

    // As per the spec, the response can be "Updated." for memory-constrained devices
    sendUpdated();
  } else if (httpServer->arg("plain") != "") {
    // unparseable json
    sendError(2, "groups/0/action", "Bad JSON body in request");
  }
}

class LightGroup {
  public:
    LightGroup(aJsonObject *root) {
      aJsonObject* jName = aJson.getObjectItem(root, "name");
      aJsonObject* jLights = aJson.getObjectItem(root, "lights");
      // jName and jLights guaranteed to exist
      name = jName->valuestring;
      for (int i = 0; i < aJson.getArraySize(jLights); i++) {
        aJsonObject* jLight = aJson.getArrayItem(jLights, i);
        // lights are 1-based and map to the 0-based bitfield
        int lightNum = atoi(jLight->valuestring);
        if (lightNum != 0) {
          lights |= (1 << (lightNum - 1));
        }
      }
    }
    aJsonObject *getJson() {
      aJsonObject *object = aJson.createObject();
      aJson.addStringToObject(object, "name", name.c_str());
      aJsonObject *lightsArray = aJson.createArray();
      aJson.addItemToObject(object, "lights", lightsArray);
      for (int i = 0; i < 16; i++) {
        if (!((1 << i) & lights)) {
          continue;
        }
        // add light to list
        String lightNum = "";
        lightNum += (i + 1);
        aJson.addItemToArray(lightsArray, aJson.createItem(lightNum.c_str()));
      }
      return object;
    }
    unsigned int getLightMask() {
      return lights;
    }
  private:
    String name;
    // use unsigned int to hold members of this group. 2 bytes -> supports up to 16 lights
    unsigned int lights = 0;
    // no need to hold the group type, only LightGroup is supported for API 1.4
};

LightGroup *lightGroups[16] = {nullptr, };

// returns true on failure
bool updateGroupSlot(int slot, String body) {
  aJsonObject *root;
  if (body != "") {
    Serial.print("JSON Body:");
    Serial.println(body);
    root = validateGroupCreateBody(body);
  }
  if (!root && body != "") {
    // throw error bad body
    sendError(2, "groups/" + (slot + 1), "Bad JSON body");
    return true;
  }
  if (lightGroups[slot]) {
    delete lightGroups[slot];
    lightGroups[slot] = nullptr;
  }
  if (body != "") {
    lightGroups[slot] = new LightGroup(root);
    aJson.deleteItem(root);
  }
  return false;
}

void groupCreationHandler() {
  // handle group creation
  // find first available group slot
  int availableSlot = -1;
  for (int i = 0; i < 16; i++) {
    if (!lightGroups[i]) {
      availableSlot = i;
      break;
    }
  }
  if (availableSlot == -1) {
    // throw error no new groups allowed
    sendError(301, "groups", "Groups table full");
    return;
  }
  if (!updateGroupSlot(availableSlot, httpServer->arg("plain"))) {
    String slot = "";
    slot += (availableSlot + 1);
    sendSuccess("id", slot);
  }
}

void groupListingHandler() {
  // iterate over groups and serialize
  aJsonObject *root = aJson.createObject();
  for (int i = 0; i < 16; i++) {
    if (lightGroups[i]) {
      String sIndex = "";
      sIndex += (i + 1);
      aJson.addItemToObject(root, sIndex.c_str(), lightGroups[i]->getJson());
    }
  }
  sendJson(root);
}

void groupsHandler(String user, String uri) {
  uri = trimSlash(uri.substring(6));
  if (uri == "") {
    switch (httpServer->method()) {
      case HTTP_GET:
        groupListingHandler();
        break;
      case HTTP_POST:
        groupCreationHandler();
        break;
      default:
        sendError(4, "/api/" + user + "/groups", "Group method not supported");
        break;
    }
    return;
  }

  int nextSlash = uri.indexOf("/");
  int groupNum = -1;
  String groupNumText;
  if (nextSlash == -1) {
    groupNumText = uri;
    uri = "";
  } else {
    groupNumText = uri.substring(0, nextSlash);
    uri = trimSlash(uri.substring(nextSlash));
  }
  groupNum = atoi(groupNumText.c_str()) - 1;
  if ((groupNum == -1 && groupNumText != "0") || groupNum >= 16 || (groupNum >= 0 && !lightGroups[groupNum])) {
    // error, invalid group number
    String resource = "/api/";
    resource += user;
    resource += "/groups/";
    resource += groupNumText;
    sendError(3, resource, "Invalid group number");
    return;
  }

  if (uri == "") {
    switch (httpServer->method()) {
      case HTTP_GET:
        if (groupNum != -1) {
          sendJson(lightGroups[groupNum]->getJson());
        } else {
          aJsonObject *object = aJson.createObject();
          aJson.addStringToObject(object, "name", "0");
          aJsonObject *lightsArray = aJson.createArray();
          aJson.addItemToObject(object, "lights", lightsArray);
          for (int i = 0; i < MAX_LIGHT_HANDLERS; i++) {
            if (!lightHandlers[i]) {
              continue;
            }
            // add light to list
            String lightNum = "";
            lightNum += (i + 1);
            aJson.addItemToArray(lightsArray, aJson.createItem(lightNum.c_str()));
          }
          sendJson(object);
        }
        break;
      case HTTP_PUT:
        // validate body, delete old group, create new group
        updateGroupSlot(groupNum, httpServer->arg("plain"));
        sendUpdated();
        break;
      case HTTP_DELETE:
        updateGroupSlot(groupNum, "");
        break;
      default:
        sendError(4, "/api/" + user + "/groups", "Group method not supported");
        break;
    }
    return;
  }

  if (uri == "action") {
    if (httpServer->method() != HTTP_PUT) {
      // error, only PUT allowed
      sendError(4, "/api/" + user + "/groups/" + groupNum + "/action", "Only PUT supported for groups/*/action");
      return;
    }
    // parse input as if for all lights
    unsigned int lightMask;
    if (groupNum == -1) {
      lightMask == 0xFFFF;
    } else {
      lightMask = lightGroups[groupNum]->getLightMask();
    }
    // apply to group
    applyConfigToLightMask(lightMask);
  }
}

void scenesHandler(String user, String uri) {
  uri = trimSlash(uri.substring(6));
  if (uri == "" && httpServer->method() == HTTP_GET) {
    httpServer->send(200, "text/plain", "{}");
    return;
  }
  // no part of /api/user/scenes is supported, so all methods are unsupported
  sendError(4, "/api/" + user + "/" + uri, "Method not supported for scenes");
}

void wholeConfigHandler(String user, String uri) {
  // Serial.println("Respond with complete json as in https://github.com/probonopd/ESP8266HueEmulator/wiki/Hue-API#get-all-information-about-the-bridge");
  aJsonObject *root;
  root = aJson.createObject();
  aJsonObject *groups;
  // the default group 0 is never listed
  aJson.addItemToObject(root, "groups", groups = aJson.createObject());
  aJsonObject *scenes;
  aJson.addItemToObject(root, "scenes", scenes = aJson.createObject());
  aJsonObject *config;
  aJson.addItemToObject(root, "config", config = aJson.createObject());
  addConfigJson(config);
  aJsonObject *lights;
  aJson.addItemToObject(root, "lights", lights = aJson.createObject());
  addLightsJson(lights);
  aJsonObject *schedules;
  aJson.addItemToObject(root, "schedules", schedules = aJson.createObject());
  sendJson(root);
}

void descriptionHandler(String user, String uri) {
  WiFiClient client = httpServer->client();
  String str = "<root><specVersion><major>1</major><minor>0</minor></specVersion><URLBase>http://" + ipString + ":80/</URLBase><device><deviceType>urn:schemas-upnp-org:device:Basic:1</deviceType><friendlyName>Philips hue (" + ipString + ")</friendlyName><manufacturer>Royal Philips Electronics</manufacturer><manufacturerURL>http://www.philips.com</manufacturerURL><modelDescription>Philips hue Personal Wireless Lighting</modelDescription><modelName>Philips hue bridge 2012</modelName><modelNumber>929000226503</modelNumber><modelURL>http://www.meethue.com</modelURL><serialNumber>00178817122c</serialNumber><UDN>uuid:2f402f80-da50-11e1-9b23-00178817122c</UDN><presentationURL>index.html</presentationURL><iconList><icon><mimetype>image/png</mimetype><height>48</height><width>48</width><depth>24</depth><url>hue_logo_0.png</url></icon><icon><mimetype>image/png</mimetype><height>120</height><width>120</width><depth>24</depth><url>hue_logo_3.png</url></icon></iconList></device></root>";
  httpServer->send(200, "text/plain", str);
  Serial.println(str);
}

String methodToString(int method) {
  switch (method) {
    case HTTP_POST: return "POST";
    case HTTP_GET: return "GET";
    case HTTP_PUT: return "PUT";
    case HTTP_PATCH: return "PATCH";
    case HTTP_DELETE: return "DELETE";
    case HTTP_OPTIONS: return "OPTIONS";
    default: return "unknown";
  }
}

void handleAllOthers() {
  Serial.print("=== ");
  Serial.println(millis());
  Serial.print("Method: ");
  Serial.println(methodToString(httpServer->method()));
  String fullUri = trimSlash(httpServer->uri());
  Serial.print("requestedUri: ");
  Serial.println(fullUri);

  if (fullUri == "description.xml") {
    descriptionHandler("", fullUri);
    Serial.println(millis());
    return;
  }

  // make sure /api is there, rip it off along with trailing slash if present
  if (!fullUri.startsWith("api")) {
    // bail, unimplemented
    httpServer->send(200, "text/plain", "{}");
    Serial.println("FIXME: To be implemented");
    return;
  }
  fullUri.remove(0, 3);
  fullUri = trimSlash(fullUri);
  // get user
  String user;
  int userEnd = fullUri.indexOf('/');
  if (userEnd == -1) {
    user = fullUri;
    fullUri = "";
  } else {
    user = fullUri.substring(0, userEnd);
    fullUri.remove(0, userEnd);
  }
  String requestedUri = trimSlash(fullUri);
  if (requestedUri.endsWith("/")) {
    requestedUri = requestedUri.substring(0, requestedUri.length() - 1);
  }
  // requestedUri is remaining path, no leading or trailing slash
  Serial.print("URI: ");
  Serial.println(requestedUri);

  if ( requestedUri == "config" || (requestedUri == "" && user == "config") ) {
    configHandler(user, requestedUri);
  } else if (requestedUri == "") {
    if (user == "") {
      authHandler(user, requestedUri);
    } else {
      wholeConfigHandler(user, requestedUri);
    }
  } else if (requestedUri.startsWith("lights")) {
    lightsHandler(user, requestedUri);
  } else if (requestedUri.startsWith("groups")) {
    groupsHandler(user, requestedUri);
  } else if (requestedUri.startsWith("scenes")) {
    scenesHandler(user, requestedUri);
  } else {
    httpServer->send(200, "text/plain", "()");
    Serial.println("FIXME: To be implemented");

    // Print what the client has POSTed
    for (uint8_t i = 0; i < httpServer->args(); i++) Serial.printf("ARG[%u]: %s=%s\n", i, httpServer->argName(i).c_str(), httpServer->arg(i).c_str());
  }
  Serial.println(millis());
}

