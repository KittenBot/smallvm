// interp.h - Simple interpreter based on 32-bit opcodes
// John Maloney, April 2017

#ifdef __cplusplus
extern "C" {
#endif

// Opcodes

#define halt 0
#define noop 1
#define pushImmediate 2 // true, false, and ints that fit in 24 bits
#define pushBigImmediate 3 // ints that do not fit in 24 bits (and later, floats)
#define pushLiteral 4 // string or array constant from literals frame
#define pushVar 5
#define popVar 6
#define incrementVar 7
#define pushArgCount 8
#define pushArg 9
#define pushLocal 10
#define popLocal 11
#define incrementLocal 12
#define pop 13
#define jmp 14
#define jmpTrue 15
#define jmpFalse 16
#define decrementAndJmp 17
#define callFunction 18
#define returnResult 19
#define waitMicrosOp 20
#define waitMillisOp 21
#define printIt 22
#define stopAll 23
#define add 24
#define subtract 25
#define multiply 26
#define divide 27
#define lessThan 28
#define newArray 29
#define newByteArray 30
#define fillArray 31
#define at 32
#define atPut 33
#define analogReadOp 34
#define analogWriteOp 35
#define digitalReadOp 36
#define digitalWriteOp 37
#define setLEDOp 38
#define microsOp 39
#define millisOp 40
#define peekOp 41
#define pokeOp 42
#define modulo 43
#define lessOrEq 44
#define equal 45
#define greaterOrEq 46
#define greaterThan 47
#define notOp 48
#define sayIt 49
#define analogPinsOp 50
#define digitalPinsOp 51
#define hexToInt 52
#define i2cGet 53
#define i2cSet 54

#define mbDisplay 55 // temporary micro:bit primitives for demos
#define mbDisplayOff 56
#define mbPlot 57
#define mbUnplot 58
#define mbTiltX 59
#define mbTiltY 60
#define mbTiltZ 61
#define mbTemp 62
#define mbButtonA 63
#define mbButtonB 64

#define random 65
#define spiSend 66
#define spiRecv 67

// Instruction Format

#define OP(opcode, arg) (((unsigned) arg << 8) | (opcode & 0xFF))
#define CMD(n) (n & 0xFF)
#define ARG(n) (n >> 8)

#define CALL(chunkIndex, argCount, localCount) \
  (((localCount & 0xFF) << 24) | ((argCount & 0xFF) << 16) | \
   ((chunkIndex & 0xFF) << 8)| callFunction)

// Global Variables

#define MAX_VARS 25
extern OBJ vars[MAX_VARS];

// Code Chunks

// The code chunk table is an array of CodeChunkRecords. A code chunk is referenced by its
// index in this table. This "chunk index" is used for several purposes:
//
//	1. to specify the called function in function calls
//	2. to specify the code to run when starting a task
//	3. to communicate with the client IDE about code chunks and tasks
//
// Referencing code chunks via their index in the code chunk table allows the actual code
// chunk objects, which may be located in either Flash or RAM memory, to be updated by user
// or edits. When code chunks are stored in Flash (which is write-once until it is erased),
// newer versions of a chunk are appended to the end of Flash. At startup time, Flash memory
// is scanned, and the chunks[] table is reconstructed with references to the latest version
// of each chunk.

typedef enum {
	unusedChunk = 0,
	command = 1,
	reporter = 2,
	functionHat = 3,
	startHat = 4,
	whenConditionHat = 5,
} ChunkType_t;

typedef struct {
	OBJ code;
	uint8 chunkType;
} CodeChunkRecord;

#define MAX_CHUNKS 256
extern CodeChunkRecord chunks[MAX_CHUNKS];

// Task List

// The task list is an array of taskCount Tasks. Each Task has a chunkIndex for
// the top-level block of the task, as well as for the current function chunk
// when inside a call to  user-defined function. It also holds the task status, processor
// state (instruction pointer (ip), stack pointer (sp), and frame pointer (fp)), and
// the wakeTime (used when a task is waiting on the millisecond or microsecond clock).
// In the current design, Tasks have a fixed-size stack built in. In the future,
// this will become a reference to a growable stack object in memory.
//
// "When <condition>" hats have their condition test compiled into them. They
// loop back and suspend themselves when the condition is false. When the condition
// becomes true, execution proceeds to the blocks under the hat.

typedef enum {
	unusedTask = 0, // task entry is available
	waiting_micros = 1, // waiting for microseconds to reach wakeTime
	waiting_millis = 2, // waiting for milliseconds to reach wakeTime
	running = 3,
} TaskStatus_t;

typedef struct {
	uint8 status; // TaskStatus_t, stored as a byte
	uint8 taskChunkIndex; // chunk index of the top-level stack for this task
	uint8 currentChunkIndex; // chunk index when inside a function
	uint32 wakeTime;
	OBJ code;
	int ip;
	int sp;
	int fp;
	OBJ stack[10];
} Task;

// Task list shared by interp.c and runtime.c

#define MAX_TASKS 16
extern Task tasks[MAX_TASKS];
extern int taskCount;

// Serial Protocol Messages: IDE -> Board

#define storeChunkMsg			1
#define deleteChunkMsg			2
#define startChunkMsg			3
#define stopChunkMsg			4
#define startAllMsg				5
#define stopAllMsg				6
#define getVarMsg				7
#define deleteVarMsg			10
#define deleteCommentMsg		11
#define getVersionMsg			12
#define getAllCodeMsg			13
#define deleteAllCodeMsg		14
#define systemResetMsg			15

// Serial Protocol Messages: Board -> IDE

#define taskStartedMsg			16
#define taskDoneMsg				17
#define taskReturnedValueMsg	18
#define taskErrorMsg			19
#define outputValueMsg			20
#define argValueMsg				21
#define versionMsg				22
#define chunkCodeMsg			23

// Serial Protocol Messages: Bidirectional

#define chunkPositionMsg		27
#define chunkAttributeMsg		28
#define varNameMsg				29
#define commentMsg				30
#define commentPositionMsg		31

// Error Codes (codes 1-9 are reserved for protocol errors; 10 and up are runtime errors)

#define noError					0
#define unspecifiedError		1
#define badChunkIndexError		2

#define insufficientMemoryError	10	// Insufficient memory to allocate object
#define needsArrayError			11	// Needs an Array or ByteArray
#define needsBooleanError		12	// Needs a boolean
#define needsIntegerError		13	// Needs an integer
#define needsStringError		14	// Needs a string
#define nonComparableError		15	// Those objects cannot be compared for equality
#define arraySizeError			16	// Array size must be a non-negative integer
#define needsIntegerIndexError	17	// Array index must be an integer
#define indexOutOfRangeError	18	// Array index out of range
#define byteArrayStoreError		19 	// A ByteArray can only store integer values between 0 and 255
#define hexRangeError			20	// Hexadecimal input must between between -1FFFFFFF and 1FFFFFFF
#define i2cDeviceIDOutOfRange	21	// I2C device ID must be between 0 and 127
#define i2cRegisterIDOutOfRange	22	// I2C register must be between 0 and 255
#define i2cValueOutOfRange		23	// I2C value must be between 0 and 255

// Runtime Operations

OBJ fail(uint8 errCode);
void initTasks(void);
void startAll();
void stopAllTasks(void);
void processMessage(void);
int hasOutputSpace(int byteCount);
void outputString(char *s);
void outputValue(OBJ value, int chunkIndex);
void sendTaskDone(uint8 chunkIndex);
void sendTaskError(uint8 chunkIndex, uint8 errorCode, int where);
void sendTaskReturnValue(uint8 chunkIndex, OBJ returnValue);
void vmLoop(void);
void vmPanic(char *s);

// Integer Evaluation

static inline int evalInt(OBJ obj) {
	if (isInt(obj)) return obj2int(obj);
	fail(needsIntegerError);
	return 0;
}

// Testing Support

void startTaskForChunk(uint8 chunkIndex);
void runTasksUntilDone(void);

void interpTests1(void);
void taskTest(void);

// Printf macro for Arduino (many thanks to Michael McElligott)

#ifdef ARDUINO
	void putSerial(char *s);
	extern char printfBuffer[100];

	#define printf(format, ...) \
		do { \
		snprintf(printfBuffer, sizeof(printfBuffer), format, ##__VA_ARGS__); \
		putSerial(printfBuffer); \
		} while(0)
#endif

// Platform Specific Operations

uint32 microsecs(void);
uint32 millisecs(void);

int canReadByte(void);
int readBytes(uint8 *buf, int count);
int canSendByte(void);
void sendByte(char aByte);

const char *boardType();
void hardwareInit(void);
void systemReset(void);

void updateMicrobitDisplay(); // temporary

// Primitives

OBJ primNewArray(OBJ *args);
OBJ primNewByteArray(OBJ *args);
OBJ primArrayFill(OBJ *args);
OBJ primArrayAt(OBJ *args);
OBJ primArrayAtPut(OBJ *args);

OBJ primHexToInt(OBJ *args);
OBJ primPeek(OBJ *args);
OBJ primPoke(OBJ *args);

OBJ primAnalogPins(OBJ *args);
OBJ primDigitalPins(OBJ *args);
OBJ primAnalogRead(OBJ *args);
OBJ primAnalogWrite(OBJ *args);
OBJ primDigitalRead(OBJ *args);
OBJ primDigitalWrite(OBJ *args);
OBJ primSetLED(OBJ *args);
OBJ primI2cGet(OBJ *args);
OBJ primI2cSet(OBJ *args);
OBJ primSPISend(OBJ *args);
OBJ primSPIRecv(OBJ *args);

OBJ primMBDisplay(OBJ *args);
OBJ primMBDisplayOff(OBJ *args);
OBJ primMBPlot(OBJ *args);
OBJ primMBUnplot(OBJ *args);
OBJ primMBTiltX(OBJ *args);
OBJ primMBTiltY(OBJ *args);
OBJ primMBTiltZ(OBJ *args);
OBJ primMBTemp(OBJ *args);
OBJ primMBButtonA(OBJ *args);
OBJ primMBButtonB(OBJ *args);

#ifdef __cplusplus
}
#endif
