/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Copyright 2018 John Maloney, Bernat Romagosa, and Jens Mönig

// linux.c - Microblocks for Linux
// An adaptation of raspberryPi.c by John Maloney to
// work on a GNU/Linux TTY as a serial device

// John Maloney, December 2017
// Bernat Romagosa, February 2018

#define _XOPEN_SOURCE 600
#define __USE_MISC

#include <termios.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h> // still needed?
#include <sys/ioctl.h>
#include <sys/time.h> // still needed?
#include <unistd.h>

#include "mem.h"
#include "interp.h"
#include "persist.h"

// Timing Functions

static int startSecs = 0;

static void initTimers() {
	struct timeval now;
	gettimeofday(&now, NULL);
	startSecs = now.tv_sec;
}

uint32 microsecs() {
	struct timeval now;
	gettimeofday(&now, NULL);

	return (1000000 * (now.tv_sec - startSecs)) + now.tv_usec;
}

uint32 millisecs() {
	struct timeval now;
	gettimeofday(&now, NULL);

	return (1000 * (now.tv_sec - startSecs)) + (now.tv_usec / 1000);
}

void delay(int ms) {
    clock_t start = millisecs();
    while (millisecs() < start + ms);
}

// Communication/System Functions

static int pty; // pseudo terminal used for communication with the IDE
//char termReady = 0;

static void openPseudoTerminal() {
	pty = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
	if (-1 == pty) {
		perror("Error opening pseudo terminal\n");
		exit(-1);
	}

	struct termios settings;
	tcgetattr(pty, &settings);
	cfmakeraw(&settings);
	tcsetattr(pty, TCSANOW, &settings);

 	grantpt(pty);
 	unlockpt(pty);
}

int recvBytes(uint8 *buf, int count) {
	int readCount = read(pty, buf, count);
	if (readCount < 0) readCount = 0;
//	termReady = 1;
	return readCount;
}

int canReadByte() {
	int bytesAvailable;
	ioctl(pty, FIONREAD, &bytesAvailable);
	return (bytesAvailable > 0);
}

int sendByte(char aByte) {
//	if (termReady) {
		return write(pty, &aByte, 1);
//	} else {
//		return 1;
//	}
}

// System Functions

const char * boardType() { return "Linux"; }
void systemReset() { } // noop on Linux

// Stubs for IO primitives

OBJ primAnalogPins(OBJ *args) { return int2obj(0); }
OBJ primDigitalPins(OBJ *args) { return int2obj(0); }
OBJ primAnalogRead(int argCount, OBJ *args) { return int2obj(0); }
void primAnalogWrite(OBJ *args) { }
OBJ primDigitalRead(int argCount, OBJ *args) { return int2obj(0); }
void primDigitalWrite(OBJ *args) { }
void primDigitalSet(int pinNum, int flag) { };
OBJ primButtonA(OBJ *args) { return falseObj; }
OBJ primButtonB(OBJ *args) { return falseObj; }
void primSetUserLED(OBJ *args) { }
void stopServos() { }

OBJ primI2cGet(OBJ *args) { return int2obj(0); }
OBJ primI2cSet(OBJ *args) { return int2obj(0); }
OBJ primSPISend(OBJ *args) { return int2obj(0); }
OBJ primSPIRecv(OBJ *args) { return int2obj(0); }
OBJ primI2cRead(OBJ *args) { return int2obj(0); }
OBJ primI2cWrite(OBJ *args) { return int2obj(0); }

// Bogus micro:bit primitives

OBJ primMBDisplay(int argCount, OBJ *args) { return falseObj; }
OBJ primMBDisplayOff(int argCount, OBJ *args) { return falseObj; }
OBJ primMBPlot(int argCount, OBJ *args) { return falseObj; }
OBJ primMBUnplot(int argCount, OBJ *args) { return falseObj; }
OBJ primMBTiltX(int argCount, OBJ *args) { return int2obj(0); }
OBJ primMBTiltY(int argCount, OBJ *args) { return int2obj(0); }
OBJ primMBTiltZ(int argCount, OBJ *args) { return int2obj(0); }
OBJ primMBTemp(int argCount, OBJ *args) { return int2obj(0); }

OBJ primNeoPixelSend(int argCount, OBJ *args) { return falseObj; }
OBJ primNeoPixelSetPin(int argCount, OBJ *args) { return falseObj; }
OBJ primMBDrawShape(int argCount, OBJ *args) { return falseObj; }
OBJ primMBShapeForLetter(int argCount, OBJ *args) { return int2obj(0); }

// Other bogus primitives

void resetServos() {}
void stopTone() {}
void turnOffInternalNeoPixels() {}
void turnOffPins() {}
void addDisplayPrims() {}
void addSensorPrims() {}
void addTFTPrims() {}
void addIOPrims() {}
void stopPWM() {}
void processFileMessage(int msgType, int dataSize, char *data) { }

// Persistence support

FILE *codeFile;

void initCodeFile(uint8 *flash, int flashByteCount) {
	codeFile = fopen("ublockscode", "ab+");
	fseek(codeFile, 0 , SEEK_END);
	long fileSize = ftell(codeFile);

	// read code file into simulated Flash:
	fseek(codeFile, 0L, SEEK_SET);
	long bytesRead = fread((char*) flash, 1, flashByteCount, codeFile);
	if (bytesRead != fileSize) {
		outputString("initCodeFile did not read entire file");
	}
}

void writeCodeFile(uint8 *code, int byteCount) {
	fwrite(code, 1, byteCount, codeFile);
	fflush(codeFile);
}

void writeCodeFileWord(int word) {
	fwrite(&word, 1, 4, codeFile);
	fflush(codeFile);
}

void clearCodeFile(int ignore) {
	fclose(codeFile);
	remove("ublockscode");
	codeFile = fopen("ublockscode", "ab+");
	uint32 cycleCount = ('S' << 24) | 1; // Header record, version 1
	fwrite((uint8 *) &cycleCount, 1, 4, codeFile);
}

// Linux Main

int main() {
	openPseudoTerminal();
	printf("Starting Linux MicroBlocks... Connect on %s\n", (char*) ptsname(pty));
	initTimers();
	memInit(10000); // 10k words = 40k bytes
	primsInit();
	outputString("Welcome to uBlocks for Linux!");
	restoreScripts();
	startAll();
	vmLoop();
	return 0;
}
