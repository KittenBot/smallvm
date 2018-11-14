/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Copyright 2018 John Maloney, Bernat Romagosa, and Jens Mönig

// netPrims.cpp - MicroBlocks network primitives
// Bernat Romagosa, August 2018
// Revised by John Maloney, November 2018

#include <stdio.h>
#include <string.h>
#include "mem.h"
#include "tinyJSON.h"

#if defined(ESP8266)
	#include <ESP8266WiFi.h>
#elif defined(ARDUINO_ARCH_ESP32)
	#include <WiFi.h>
	#include <esp_wifi.h>
#endif

#include "interp.h" // must be included *after* ESP8266WiFi.h

#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP32)

// Buffer for HTTP requests
#define REQUEST_SIZE 1024
static char request[REQUEST_SIZE];

#define JSON_HEADER \
"HTTP/1.1 200 OK\r\n" \
"Content-Type: application/json\r\n\r\n"

#define NOT_FOUND_RESPONSE \
"HTTP/1.1 404 Not Found\r\n" \
"Content-Type: application/json\r\n\r\n" \
"{ \"error\":\"Resource not found\" }"

// Primitives to build a Thing description (interim, until we have string concatenation)

// Hack: Simulate a MicroBlocks string object with a C struct. Since this is not a
// a dynamically allocated object, it could confuse the garbage collector. But
// we'll replace this interim mechanism once we do have a garbage collector.

#define DESCRIPTION_SIZE 1024

struct {
	uint32 header;
	char body[DESCRIPTION_SIZE];
} descriptionObj;

static char connecting = false;
static uint32 initTime;

int serverStarted = false;
WiFiServer server(80);
WiFiClient client;

// Web Server for Mozilla IoT Things

static void initWebServer() {
	server.stop();
	server.begin();
	serverStarted = true;
}

static int hasPrefix(char *s, char *requestPrefix, char *arg, int argSize) {
	// Return true if the HTTP request string s begins with the given prefix.
	// If arg is not NULL, the request string from the end of the prefix up to
	// the next space will be copied into arg.

	if (arg) arg[0] = '\0'; // clear arg result, if provided
	if ((strstr(s, requestPrefix) != s)) return false; // request does not match prefix

	if (arg) {
		// extract last part of URL into arg
		char *start = s + strlen(requestPrefix);
		char *end = strchr(start, ' '); // find the next space
		if (!end) strchr(start, '\r'); // if no space, use the end of the line
		if (!end) return true; // no space or line end found
		int count = end - start;
		if (count > argSize) count = argSize;
		if (count > 0) {
			strncpy(arg, start, count);
			arg[count] = '\0'; // null terminate
		}
	}
	return true;
}

static char * valueJSON(char *response, char *varName, int varID) {
	// Write a object with the name and value of the given variable into response.

	OBJ value = vars[varID];
	if (isInt(value)) {
		sprintf(response, "{ \"%s\": %d }\r\n", varName, obj2int(value));
	} else if ((trueObj == value) || (falseObj == value)) {
		sprintf(response, "{ \"%s\": %s }\r\n", varName, (trueObj == value) ? "true" : "false");
	} else if (IS_CLASS(value, StringClass)) {
		sprintf(response, "{ \"%s\": \"%s\" }\r\n", varName, obj2str(value));
	} else {
		sprintf(response, "{ \"%s\": \"<Unknown type>\" }\r\n", varName);
	}
	return response;
}

static void setVariableValue(char *varName, int varID, char *jsonData) {
	// Set the value of the given variable from the given JSON data.

	char *p = tjr_atPath(jsonData, varName);
	int type = tjr_type(p);
	if (tjr_Number == type) {
		vars[varID] = int2obj(tjr_readInteger(p));
	} else if (tjr_String == type) {
		char s[100];
		tjr_readStringInto(p, s, sizeof(s));
		vars[varID] = newStringFromBytes((uint8 *) s, strlen(s));
	} else if (tjr_True == type) {
		vars[varID] = trueObj;
	} else if (tjr_False == type) {
		vars[varID] = falseObj;
	}
}

void webServerLoop() {
	if (!client) client = server.available(); // attempt to accept a client connection
	if (!client) return; // no client connection

	// read an HTTP request
	int bytesAvailable = client.available();
	if (!bytesAvailable) return;
	client.readBytes(request, bytesAvailable);
	request[bytesAvailable] = 0; // null terminate

	// the body starts after the first blank line (or at end of request, if no blank lines)
	char *body = strstr(request, "\r\n\r\n");
	body = body ? (body + 4) : request + strlen(request);

	char response[1000];
	char varName[100];
	int varID = -1;

	// HTTP request format: "[GET/PUT] /some/url HTTP/1.1"
	if (hasPrefix(request, "GET / ", NULL, 0)) {
		// Get the Thing description
		client.print(JSON_HEADER);
		client.print(descriptionObj.body);
	} else if (hasPrefix(request, "GET /properties/", varName, sizeof(varName))) {
		// Get variable value
		varID = indexOfVarNamed(varName);
		if (varID < 0) {
			client.print(NOT_FOUND_RESPONSE);
		} else {
			client.print(JSON_HEADER);
			client.print(valueJSON(response, varName, varID));
		}
	} else if (hasPrefix(request, "PUT /properties/", varName, sizeof(varName))) {
		// Set variable value
		varID = indexOfVarNamed(varName);
		if (varID < 0) {
			client.print(NOT_FOUND_RESPONSE);
		} else {
			setVariableValue(varName, varID, body);
			client.print(JSON_HEADER);
			client.print(valueJSON(response, varName, varID));
		}
	} else {
		client.print(NOT_FOUND_RESPONSE);
	}
	client.flush();
	client.stop();
}

// WiFi Connection

// Macro for creating MicroBlocks string object constants
#define STRING_OBJ_CONST(s) \
	struct { uint32 header = HEADER(StringClass, ((sizeof(s) + 4) / 4)); char body[sizeof(s)] = s; }

// Status strings that can be returned by WiFiStatus primitive

STRING_OBJ_CONST("Not connected") statusNotConnected;
STRING_OBJ_CONST("Trying...") statusTrying;
STRING_OBJ_CONST("Connected") statusConnected;
STRING_OBJ_CONST("Failed; bad password?") statusFailed;
STRING_OBJ_CONST("Unknown network") statusUnknownNetwork;

int firstTime = true;

static OBJ primHasWiFi(int argCount, OBJ *args) { return trueObj; }

static OBJ primStartWiFi(int argCount, OBJ *args) {
	// Start a WiFi connection attempt. The client should call wifiStatus until either
	// the connection is established or the attempt fails.

	if (argCount < 2) return fail(notEnoughArguments);

	char *networkName = obj2str(args[0]);
	char *password = obj2str(args[1]);
	int createHotSpot = (argCount > 2) && (trueObj == args[2]);

	WiFi.persistent(false); // don't save network info to Flash
	WiFi.mode(WIFI_OFF); // Kill the current connection, if any

	if (createHotSpot) {
		WiFi.mode(WIFI_AP_STA); // access point & station mode
		WiFi.softAP(networkName, password);
	} else {
		WiFi.mode(WIFI_STA);
		WiFi.begin(networkName, password);
	}

	#ifdef ESP8266
		// workaround for an apparent ESP8266 WiFi startup bug: calling WiFi.status() during
		// the first few seconds after starting WiFi for the first time results in strange
		// behavior (task just stops without either error or completion; memory corruption?)
		if (firstTime) {
			delay(3000); // 3000 works, 2500 does not
			firstTime = false;
		}
	#endif
	connecting = true;
}

static OBJ primStopWiFi(int argCount, OBJ *args) {
	server.stop();
	WiFi.mode(WIFI_OFF);
	connecting = false;
	return falseObj;
}

static OBJ primWiFiStatus(int argCount, OBJ *args) {
	int status = WiFi.status();

	if (WL_NO_SHIELD == status) return (OBJ) &statusNotConnected; // reported on ESP32
	if (WL_NO_SSID_AVAIL == status) return (OBJ) &statusUnknownNetwork; // reported only on ESP8266
	if (WL_CONNECT_FAILED == status) return (OBJ) &statusFailed; // reported only on ESP8266

	if (WL_DISCONNECTED == status) {
		return connecting ? (OBJ) &statusTrying : (OBJ) &statusNotConnected;
	}
	if (WL_CONNECTION_LOST == status) {
		primStopWiFi(0, NULL);
		return (OBJ) &statusNotConnected;
	}
	if (WL_IDLE_STATUS == status) {
		if (WIFI_AP_STA == WiFi.getMode()) {
			status = WL_CONNECTED; // acting as a hotspot
		} else {
			return connecting ? (OBJ) &statusTrying : (OBJ) &statusNotConnected;
		}
	}
	if (WL_CONNECTED == status) {
		if (!serverStarted) {
			// start the server when a connection is first established
			initWebServer();
		}
		return (OBJ) &statusConnected;
	}
	return int2obj(status); // should not happen
}

struct {
	uint32 header;
	char body[16];
} ipStringObject;

static OBJ primGetIP(int argCount, OBJ *args) {
	IPAddress ip = (WIFI_AP_STA == WiFi.getMode()) ? WiFi.softAPIP() : WiFi.localIP();
	sprintf(ipStringObject.body, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
	ipStringObject.header = HEADER(StringClass, (strlen(ipStringObject.body) + 4) / 4);
	return (OBJ) &ipStringObject;
}

// Thing Description

static OBJ primThingDescription(int argCount, OBJ *args) {
	int wordCount = (strlen(descriptionObj.body) + 4) / 4;
	descriptionObj.header = HEADER(StringClass, wordCount);
	return (OBJ) &descriptionObj;
}

static OBJ primClearThingDescription(int argCount, OBJ *args) {
	descriptionObj.body[0] = 0;
}

static void appendObjToDescription(OBJ obj) {
	// Append a printed representation of the given object to the descriptionObj.body.
	// Do nothing if obj is not a string, integer, or boolean.

	int currentSize = strlen(descriptionObj.body);
	char *dst = &descriptionObj.body[currentSize];
	int n = (DESCRIPTION_SIZE - currentSize) - 1;

	if (objClass(obj) == StringClass) snprintf(dst, n, "%s", obj2str(obj));
	else if (isInt(obj)) snprintf(dst, n, "%d", obj2int(obj));
	else if (obj == trueObj) snprintf(dst, n, "true");
	else if (obj == falseObj) snprintf(dst, n, "false");
}

static OBJ primAppendToThingDescription(int argCount, OBJ *args) {
	for (int i = 0; i < argCount; i++) {
		appendObjToDescription(args[i]);
	}
	int currentSize = strlen(descriptionObj.body);
	if (currentSize < (DESCRIPTION_SIZE - 1)) {
		// add a newline, if there is room
		descriptionObj.body[currentSize] = '\n';
		descriptionObj.body[currentSize + 1] = 0;
	}
}

#else // not ESP8266 or ESP32

static OBJ primHasWiFi(int argCount, OBJ *args) { return falseObj; }
static OBJ primStartWiFi(int argCount, OBJ *args) { return fail(noWiFi); }
static OBJ primStopWiFi(int argCount, OBJ *args) { return fail(noWiFi); }
static OBJ primWiFiStatus(int argCount, OBJ *args) { return fail(noWiFi); }
static OBJ primGetIP(int argCount, OBJ *args) { return fail(noWiFi); }

static OBJ primThingDescription(int argCount, OBJ *args) { return fail(noWiFi); }
static OBJ primClearThingDescription(int argCount, OBJ *args) { fail(noWiFi); }
static OBJ primAppendToThingDescription(int argCount, OBJ *args) { fail(noWiFi); }

#endif

static PrimEntry entries[] = {
	"hasWiFi", primHasWiFi,
	"startWiFi", primStartWiFi,
	"stopWiFi", primStopWiFi,
	"wifiStatus", primWiFiStatus,
	"myIPAddress", primGetIP,
	"thingDescription", primThingDescription,
	"clearThingDescription", primClearThingDescription,
	"appendToThingDescription", primAppendToThingDescription,
};

void addNetPrims() {
	addPrimitiveSet("net", sizeof(entries) / sizeof(PrimEntry), entries);
}
