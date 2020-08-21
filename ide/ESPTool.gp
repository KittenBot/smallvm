// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// Copyright 2019 John Maloney, Bernat Romagosa, and Jens Mönig

// ESPTool.gp - Uploader for ESP8266 and ESP32
// John Maloney, September, 2019

defineClass ESPTool port recvBuf boardType

to newESPTool { return (initialize (new 'ESPTool')) }

method initialize ESPTool {
	boardType = ''
	port = nil
	recvBuf = (newBinaryData)
	return this
}

// Serial Port

method openPort ESPTool portName baudRate {
	if (isNil baudRate) { baudRate = 115200 }
	closePort this
	port = (safelyRun (action 'openSerialPort' portName baudRate))
	if (not (isClass port 'Integer')) { port = nil } // failed
	if ('Browser' == (platform)) { waitMSecs 100 } // let browser callback complete
}

method setPort ESPTool portID {
	initialize this
	port = portID
}

method closePort ESPTool {
	if (notNil port) { closeSerialPort port }
	initialize this
}

// Chip Control

method enterBootMode ESPTool {
	// Use the RTS/DTR lines to force the chip into bootloader mode.
	// Set DTR, then toggle RTS to reset the chip.
	// Note: RTS and DTR are inverted by transistors on the board.

	if (isNil port) { return }
	setSerialPortDTR port false		// IO0 = high
	setSerialPortRTS port true		// EN = low (chip in reset)
	waitMSecs 100 // might need to increase to 220 msecs on some chips
	setSerialPortDTR port true		// IO0 = low
	setSerialPortRTS port false		// EN = high (exit reset)
	waitMSecs 50 // might need to increase to 450 msecs on some chips
	setSerialPortDTR port false		// IO0 = high
}

method exitBootMode ESPTool {
	// Reset the chip in normal mode so that it runs the MicroBlocks virtual machine.

	setSerialPortDTR port false		// IO0 = high for reset in normal (not boot) mode
	setSerialPortRTS port true		// EN = low (chip in reset)
	waitMSecs 100
	setSerialPortRTS port false		// EN = high (exit reset)
}

method chipType ESPTool {
	dateReg1 = (readRegister this '60000078')
	if ('0x00062000' == dateReg1) { return 'ESP8266' }
	if ('0x15122500' == dateReg1) { return 'ESP32' }
	if ('0x00000500' == dateReg1) {
		dateReg2 = (readRegister this '3f400074')
		if ('0x19031400' == dateReg2) { return 'ESP32-S2' }
	}
	return nil // unrecognized chip
}

// About SLIP:
// Each SLIP packet begins and ends with 0xC0 (192). Within the packet, all occurrences of
// 0xC0 and 0xDB are replaced with 0xDB 0xDC (219 220) and 0xDB 0xDD (219 221), respectively.

// SLIP Message Receiving

method waitForMsg ESPTool {
	timeout = 10000
	startTime = (msecsSinceStart)
	while (((msecsSinceStart) - startTime) < timeout) {
		msg = (nextSLIPMsg this)
		if (notNil msg) { return msg }
		waitMSecs 10
	}
	return nil
}

method nextSLIPMsg ESPTool {
	// Return the next complete SLIP message or nil. The returned message does not
	// include the start and end bytes and escaped byte pairs have been processed.

	if (isNil port) { return nil }
	data = (readSerialPort port true)
	if (notNil data) {
		recvBuf = (join recvBuf data)
	}
	startIndex = (nextFrameByte this 1)
	if (isNil startIndex) {
		recvBuf = (newBinaryData 0) // no message start found; discard entire buffer
		return nil
	}
	endIndex = (nextFrameByte this (startIndex + 1))
	if (isNil endIndex) {
		if (startIndex > 1) { recvBuf = (copyFromTo recvBuf startIndex) } // discard bytes before start
		return nil
	}

	// received a complete message
	result = (unescapeMsg this startIndex endIndex)
	recvBuf = (copyFromTo recvBuf (endIndex + 1))
	return result
}

method nextFrameByte ESPTool startIndex {
	// Return the index of the next SLIP frame byte after startIndex in recvBuf.
	// Return nil if there isn't one.

	end = (byteCount recvBuf)
	i = startIndex
	while (i <= end) {
		if (192 == (byteAt recvBuf i)) { return i } // SLIP frame mark byte (192 = 0xC0)
		i += 1
	}
	return nil
}

method unescapeMsg ESPTool startIndex endIndex {
	// Return a list of the bytes between the given indices in recvBuf
	// with escaped byte pairs replaced with their original byte values.

	result = (list)
	i = (startIndex + 1)
	while (i < endIndex) {
		b = (byteAt recvBuf i)
		if (219 == b) { // SLIP escape byte (219 = 0xDB)
			nextB = (byteAt recvBuf i)
			if (220 == nextB) { add result 192 } // escaped SLIP start byte (192 = 0xC0)
			if (221 == nextB) { add result 219 } // escaped SLIP escape byte (219 = 0xDB)
			i += 2
		} else {
			add result b
			i += 1
		}
	}
	return result
}

method clearReceiveBuffer ESPTool {
	// Discard any buffered data from the serial port and clear the receive buffer.

	data = true
	while (notNil data) {
		waitMSecs 10 // allow time for any leftover data to arrive
		data = (readSerialPort port true)
	}
	recvBuf = (newBinaryData)
}

// SLIP Message Sending

method sendCmd ESPTool cmd data checksum {
	// Send the given command to the board with the given data.
	// Both data and checksum are optional.

	if (isNil data) { data = (list) }
	if (isClass data 'BinaryData') { data = (toArray data) }

	byteCount = (count data)
	msg = (list 0 cmd (byteCount & 255) ((byteCount >> 8) & 255))
	if (notNil checksum) {
		add32Int this msg checksum
	} else {
		repeat 4 { add msg 0 }
	}
	addAll msg data
	sendSLIPMsg this msg
}

method computeChecksum ESPTool data {
	result = (hex 'EF')
	for n data { result = (result ^ n) }
	return result
}

method sendSLIPMsg ESPTool msg {
	escaped = (list)
	add escaped 192 // SLIP start byte
	for b msg {
		if (192 == b) { // escape SLIP end byte
			add escaped 219
			add escaped 220
		} (219 == b) { // escape SLIP escape byte
			add escaped 219
			add escaped 221
		} else {
			add escaped b
		}
	}
	add escaped 192 // SLIP end byte
	writeSerialPort port (toBinaryData (toArray escaped))
}

method add32Int ESPTool msg n {
	// Append the 32-bit integer n to the given message in little-endian byte order.

	repeat 4 {
		add msg (n & 255)
		n = (n >> 8)
	}
}

// Utilities

method attachSPI ESPTool {
	// Send an SPI_ATTACH message to an ESP32.
	// Neither needed nor supported on the ESP8266 but can be sent anyhow.

	clearReceiveBuffer this
	sendCmd this (hex '0d') (newArray 8 0)
	return (waitForMsg this)
}

method readRegister ESPTool hexAddr {
	// Read the given register (supplied as a hex string such as '3ff0005c').

	clearReceiveBuffer this
	sendCmd this 10 (hexToBytes this hexAddr)
	msg = (waitForMsg this)
	if (or (isNil msg) ((count msg) < 10)) { return -1 } // no response
	if (0 != (at msg 9)) { return (0 - (at msg 10)) } // return error code (negative)
	return (bytesAsHex this (reversed (copyFromTo msg 5 8)))
}

method hexToBytes ESPTool s {
	// Convert a hexadecimal value into a list of byte values in little-endian order.

	if (beginsWith s '0x') { s = (substring s 3) } // remove optional leading '0x'
	result = (list)
	i = ((count s) - 1)
	while (i >= 0) {
		if (0 == i) { // single character (odd-length string)
			add result (hex (substring s 1 1))
		} else {
			add result (hex (substring s i (i + 1)))
		}
		i += -2
	}
	return result
}

method bytesAsHex ESPTool bytes {
	out = (list '0x')
	for b (toArray bytes) {
		hex = (toLowerCase (toStringBase16 b))
		if ((count hex) < 2) { hex = (join '0' hex) }
		add out hex
	}
	return (joinStrings out '')
}

// Synchronization

method openAndConnect ESPTool portName baudRate {
	if (isNil baudRate) { baudRate = 115200 }

	print 'Connecting to' portName 'at' baudRate 'baud'
	openPort this portName baudRate
	if (isNil port) { return false }
	return (connect this)
}

method connect ESPTool {
	// Enter boot mode and connect to the ROM boot loader.

	repeat 20 {
		enterBootMode this
		waitMSecs 10
		recvBuf = (newBinaryData)
		repeat 3 {
			sendSyncMsg this
			waitMSecs 20
			msg = (nextSLIPMsg this)
			if (notNil msg) {
				clearReceiveBuffer this
				print 'Connected'
				return true
			}
		}
	}
	print 'Could not connect. Board did not respond.'
	return false
}

method sendSyncMsg ESPTool {
	// Send an ESPTool SYNC message to allow ESP board to detect the baud rate.

	data = (list 7 7 18 32) // four bytes: 0x07 0x07 0x12 0x20
	repeat 32 { add data (hex '55') } // 32 x 0x55 (to allow ESP to detect baud rate)
	sendCmd this 8 data
}

// Writing to Flash

method errorResponse ESPTool {
	// Return true if we got an error or no response.

	msg = (waitForMsg this)
	if (isNil msg) {
		print 'No response from board'
		return true
	}
	if ((count msg) < 10) {
		print 'Incomplete response from board'
		return true
	}
	if (0 != (at msg 9)) {
		print 'Board reported error:' (at msg 10)
		return true
	}
	return false // all good!
}

method uploadToFlash ESPTool startAddr flashData {
	// Upload the given binary data to Flash at the given address.

start = (msecsSinceStart)
	totalBytes = (byteCount flashData)
	packetSize = 512
	packetCount = (ceiling (totalBytes / packetSize))
	eraseSize = totalBytes

	if ('ESP8266' == (chipType this)) {
		eraseSize = (get_erase_size this startAddr totalBytes)
	}

	print ''
	args = (list)
	add32Int this args eraseSize
	add32Int this args packetCount
	add32Int this args packetSize
	add32Int this args startAddr
	sendCmd this 2 args
	if (errorResponse this) { return }

	sent = 0
	seqNum = 0
	while (sent < totalBytes) {
		bytesToSend = (min packetSize (totalBytes - sent))
		args = (list)
		add32Int this args bytesToSend
		add32Int this args seqNum
		repeat 8 { add args 0 }
		checksum = (hex 'EF')
		for i bytesToSend {
			byte = (byteAt flashData (sent + i))
			checksum = (checksum ^ byte)
			add args byte
		}
		sendCmd this 3 args checksum
		if (errorResponse this) { return }
		sent += bytesToSend
		percent = (round ((100 * sent) / totalBytes))
		print (join (string 27) '[1A' (string 27) '[K' sent ' (' percent '%)')
		seqNum += 1
	}
print ((msecsSinceStart) - start) 'msecs'
}

method uploadToRAM ESPTool hexStartAddr ramData {
	// Upload the given binary data to RAM at the given address.

	totalBytes = (byteCount ramData)
	packetSize = 800 // must be well under 1024 to allow for header and SLIP escaping
	packetCount = (ceiling (totalBytes / packetSize))

	args = (list)
	add32Int this args totalBytes
	add32Int this args packetCount
	add32Int this args packetSize
	addAll args (hexToBytes this hexStartAddr)
	sendCmd this 5 args
	if (errorResponse this) { return }

	sent = 0
	seqNum = 0
	while (sent < totalBytes) {
		bytesToSend = (min packetSize (totalBytes - sent))
		args = (list)
		add32Int this args bytesToSend
		add32Int this args seqNum
		repeat 8 { add args 0 }
		checksum = (hex 'EF')
		for i bytesToSend {
			byte = (byteAt ramData (sent + i))
			checksum = (checksum ^ byte)
			add args byte
		}
		sendCmd this 7 args checksum
		if (errorResponse this) { return }
		sent += bytesToSend
		seqNum += 1
	}
}

method get_erase_size ESPTool offset size {
	// Calculate an erase size given a specific size in bytes.
	// Workaround for the ESP ROM bootloader erase bug.
	// Follows get_erase_size function in esptool.py.

	sectors_per_block = 16
	sector_size = 4096 // minimum unit of erase

	num_sectors = (floor (((size + sector_size) - 1) / sector_size))
	start_sector = (floor (offset / sector_size))

	head_sectors = (sectors_per_block - (start_sector % sectors_per_block))
	if (num_sectors < head_sectors) { head_sectors = num_sectors }

	if (num_sectors < (2 * head_sectors)) {
		return (sector_size * (floor ((num_sectors + 1) / 2)))
	} else {
		return (sector_size * ((num_sectors - head_sectors)))
	}
}

// Tests

method registerTest ESPTool {
	// Read some registers. Assume connected and sync-ed...

	print '60000078' '->' (readRegister this '60000078') // differentiates ESP8266 vs ESP32*
	print '3f400074' '->' (readRegister this '3f400074') // differentiates ESP32-S2 vs others

	print '3ff0005c' '->' (readRegister this '3ff0005c')
	print '3ff00058' '->' (readRegister this '3ff00058')
	print '3ff00054' '->' (readRegister this '3ff00054')
	print '3ff00050' '->' (readRegister this '3ff00050')
}

method esp8266VM ESPTool {
	return (readEmbeddedFile (join 'precompiled/vm.ino.nodemcu.bin') true)
}

method atomVM ESPTool {
	return (readEmbeddedFile (join 'precompiled/vm.ino.m5atom.bin') true)
}

// esp = (newESPTool)
// openAndConnect esp '/dev/cu.SLAB_USBtoUART'
// openAndConnect esp '/dev/cu.usbserial-1420'
// openAndConnect esp '/dev/cu.usbserial-1956965AB4' (2 * 115200)
// openAndConnect esp '/dev/cu.SLAB_USBtoUART' (2 * 115200)
// openAndConnect esp '/dev/cu.usbserial-7152380AB6' (2 * 115200)

method writeFlashTest ESPTool {
	ok = (openAndConnect this '/dev/cu.SLAB_USBtoUART')
//	ok = (openAndConnect this '/dev/cu.usbserial-1420' (8 * 115200))
//	ok = (openAndConnect this '/dev/cu.usbserial-7152380AB6' (8 * 115200))

	if (not ok) { return }

	flashData = (newArray 2000)
	for i (count flashData) { atPut flashData i (i & 255) }
fillArray flashData 123
//	flashData = (esp8266VM this)

	uploadToFlash this 0 (toBinaryData flashData)
	exitBootMode this
}

method atomTest ESPTool {
//	ok = (openAndConnect this '/dev/cu.usbserial-1956965AB4' 115200)
	ok = (openAndConnect this '/dev/cu.SLAB_USBtoUART')
	if (not ok) { return }

	flashData = (atomVM this)
print (chipType this) 'vm size:' (byteCount flashData)
	uploadToFlash this (hex '10000') (toBinaryData flashData)
	exitBootMode this
}

method setFlashParameters ESPTool {
	// Assumes Flash if 4 MB for now. (Later, detect actual size.)
	// Other parameters are fixed.

	args = (list)
	add32Int this args 0 // Flash ID
	add32Int this args (4 * (1024 * 1024)) // total size
	add32Int this args (64 * 1024) // block size
	add32Int this args (4 * 1024) // sector size
	add32Int this args 256 // page size
	add32Int this args (hex 'ffff') // status mask
	sendCmd this (hex '0b') args
	return (not (errorResponse this))
}

method m5AtomTest ESPTool {
	ok = (openAndConnect this '/dev/cu.usbserial-7152380AB6')
	if (not ok) { return }

	ok = (uploadStub this)
	if (not ok) { return }

	ok = (setFlashParameters this)
	if (not ok) { return }

	data = (readEmbeddedFile (join 'esp32/bootloader_dio_40m.bin') true)
	uploadToFlash this (hex '1000') data
print 'bootloader_dio_40m.bin' (byteCount data)

	data = (readEmbeddedFile (join 'esp32/partitions.bin') true)
	uploadToFlash this (hex '8000') data
print 'partitions.bin' (byteCount data)

	data = (readEmbeddedFile (join 'esp32/boot_app0.bin') true)
	uploadToFlash this (hex 'e000') data
print 'boot_app0.bin' (byteCount data)

	data = (readEmbeddedFile (join 'precompiled/vm.ino.m5atom.bin') true)
	uploadToFlash this (hex '10000') data
print 'vm.ino.m5atom.bin' (byteCount data)

	exitBootMode this
	closePort this
}

method writeZeroFile ESPTool {
	flashData = (newArray 1024)
	fillArray flashData 0
	writeFile 'zeros.dat' (toBinaryData flashData)
}

method writeTestFile ESPTool {
	flashData = (newArray 1024)
	for i (count flashData) { atPut flashData i (i & 127) }
	writeFile 'test.dat' (toBinaryData flashData)
}

// Stubs

method startStub ESPTool hexStartAddr {
	print 'Starting stub...'
	args = (list 0 0 0 0)
	addAll args (hexToBytes this hexStartAddr)
	sendCmd this 6 args
	waitForMsg this // okay if this times out
	msg = (waitForMsg this)
	if (or (isNil msg) (msg != (list 79 72 65 73))) {
		print 'Stub did not repsond'
		return false
	}
	print 'Stub started'
	return true
}

method uploadStub ESPTool {
	type = (chipType this)
	print 'Chip type:' type

	if ('ESP8266' == type) {
		stub = (esp8266_stub this)
	} ('ESP32' == type) {
		stub = (esp32_stub this)
	} ('ESP32-S2' == type) {
		stub = (esp32_S2_stub this)
	} else {
		print 'unknown chip type'
		return
	}
	uploadToRAM this (at stub 'text_start') (at stub 'text')
	uploadToRAM this (at stub 'data_start') (at stub 'data')
	ok = (startStub this (at stub 'entry'))
	return ok
}

method esp8266_stub ESPTool {
	result = (dictionary)
	atPut result 'text' (base64Decode '
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
AAAAMqEMAg3wAA==')

	atPut result 'data' (base64Decode '
CIH+PwUFBAACAwcAAwMLAFHnEECH5xBAtecQQFToEEAF9xBAuugQQBDpEEBc6RBABfcQQCLqEECf
6hBAYOsQQAX3EEAF9xBA+OsQQAX3EEDX7hBAn+8QQNjvEEAF9xBABfcQQHXwEEAF9xBAW/EQQAHy
EEBA8xBA//MQQND0EEAF9xBABfcQQAX3EEAF9xBA/vUQQAX3EED09hBAL+0QQCfoEEBC9RBAS+oQ
QJjpEEAF9xBAiPYQQM/2EEAF9xBABfcQQAX3EEAF9xBABfcQQAX3EEAF9xBABfcQQMDpEED/6RBA
WvUQQAEAAAACAAAAAwAAAAQAAAAFAAAABwAAAAkAAAANAAAAEQAAABkAAAAhAAAAMQAAAEEAAABh
AAAAgQAAAMEAAAABAQAAgQEAAAECAAABAwAAAQQAAAEGAAABCAAAAQwAAAEQAAABGAAAASAAAAEw
AAABQAAAAWAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAEAAAABAAAAAgAAAAIAAAADAAAAAwAA
AAQAAAAEAAAABQAAAAUAAAAGAAAABgAAAAcAAAAHAAAACAAAAAgAAAAJAAAACQAAAAoAAAAKAAAA
CwAAAAsAAAAMAAAADAAAAA0AAAANAAAAAAAAAAAAAAADAAAABAAAAAUAAAAGAAAABwAAAAgAAAAJ
AAAACgAAAAsAAAANAAAADwAAABEAAAATAAAAFwAAABsAAAAfAAAAIwAAACsAAAAzAAAAOwAAAEMA
AABTAAAAYwAAAHMAAACDAAAAowAAAMMAAADjAAAAAgEAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAABAAAAAQAAAAEAAAABAAAAAgAAAAIAAAACAAAAAgAAAAMAAAADAAAA
AwAAAAMAAAAEAAAABAAAAAQAAAAEAAAABQAAAAUAAAAFAAAABQAAAAAAAAAAAAAAAAAAABAREgAI
BwkGCgULBAwDDQIOAQ8AAQEAAAEAAAAEAAAA')

	atPut result 'text_start' '0x4010e000'
	atPut result 'data_start' '0x3fffaca8'
	atPut result 'entry' '0x4010e004'
	return result
}

method esp32_stub ESPTool {
	result = (dictionary)
	atPut result 'text' (base64Decode '
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')

	atPut result 'data' (base64Decode 'CMD8Pw==')
	atPut result 'text_start' '0x400be000'
	atPut result 'data_start' '0x3ffdeba8'
	atPut result 'entry' '0x400be594'
	return result
}

method esp32_S2_stub ESPTool {
	result = (dictionary)
	atPut result 'text' (base64Decode '
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')

	atPut result 'data' (base64Decode 'CAD9Pw==')
	atPut result 'text_start' '0x40028000'
	atPut result 'data_start' '0x3ffe2bf4'
	atPut result 'entry' '0x4002872c'
	return result
}
