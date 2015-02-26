#include "types.h"
// Convenience definitions of Opcodes
#define MOV        0x20
#define MOV0       MOV
#define MOV1       (MOV | 1)
#define MOV2       (MOV | 2)
#define MOV3       (MOV | 3)
#define MOV4       (MOV | 4)
#define MOV5       (MOV | 5)
#define WAIT       0x40
#define START_LOOP 0x80
#define END_LOOP   0xA0
#define RECIPE_END 0

// structure to hold all servo related info/states
typedef struct{
  UINT8 looping;
  UINT8 loops;  //number of loops to do
  UINT8 curLoop; // current loop cycle
  UINT8 loopStartIndex;
  UINT8 wait;
  UINT8 curPos;
  UINT8 pause;
  UINT8 recipeIndex;
  UINT8 *recipe;
  UINT8 *reg;
  UINT8 err; 
} Servo;

UINT8 downcase(UINT8 character);
void setup(void);
void parseOpcode(UINT8 command, UINT8 servo);
Servo initServo(Servo s);
void newLine(void);
void unpause(UINT8 servo);
void pause(UINT8 servo);
void err(UINT8 code, UINT8 servo);
void clearErr(UINT8 servo);
void restart(UINT8 servo);
void nextOp(void);
UINT8 calcMove(UINT8 pos);
void wait(UINT8 cycles, UINT8 servo);
void move(UINT8 pos, UINT8 servo);
void setupPWM(void);
void cli(void);
UINT8 waitTime(UINT8 newPos, UINT8 oldPos);
void parseCommand(UINT8 command, UINT8 servo);

UINT8 standardRecipe[20] = {
  MOV0, 
  MOV5,
  MOV0, 
  MOV3, 
  START_LOOP, 
  MOV0, 
  MOV4,
  END_LOOP, 
  MOV0,
  MOV2,
  WAIT,
  MOV3,
  MOV2,
  MOV3,
  (WAIT | 31),
  (WAIT | 31),
  (WAIT | 31),
  MOV4,
  RECIPE_END
};

UINT8 looping[5] = {
  (START_LOOP | 2), 
  MOV0, 
  MOV5,
  END_LOOP
};

UINT8 nestedLoop[6] = {
  START_LOOP,
  MOV1,
  MOV4,
  START_LOOP,
  END_LOOP,
  RECIPE_END
};

UINT8 testAllPos[8] = {
  MOV0, 
  MOV1,
  MOV2, 
  MOV3, 
  MOV4, 
  MOV5, 
  RECIPE_END
};

UINT8 end[5] = {
  MOV0,
  MOV4,
  RECIPE_END,
  MOV2
};

UINT8 badOpcode[5] = {
  MOV0,
  MOV3,
  0xE0,
  MOV0
};

