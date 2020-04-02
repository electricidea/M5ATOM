/**************************************************************************
 * M5Stack ATOM Matrix Tilt Game
 * 
 * A simple game for the matrix version of the M5Stack ATOM. 
 * Red and green LEDs are shown on the display. 
 * If more red LEDs are shown, the device must be tilted to the right. 
 * If more green LEDs are shown, to the left.
 * There are twelve difficulty levels in which the number of red and green 
 * LEDs differs less and less and the display time becomes shorter and shorter. 
 * If a level is completed with more than 75% correct answers (>18 of 25), 
 * the next level is unlocked.
 * 
 * Hague Nusseck @ electricidea
 * v1.2 | 02.April.2020
 * https://github.com/electricidea/xxx
 * 
 * 
 * 
 * Distributed as-is; no warranty is given.
**************************************************************************/

#include <Arduino.h>

#include "M5Atom.h"
// install the library:
// pio lib install "M5Atom"

// You also need the FastLED library
// https://platformio.org/lib/show/126/FastLED
// FastLED is a library for programming addressable rgb led strips 
// (APA102/Dotstar, WS2812/Neopixel, LPD8806, and a dozen others) 
// acting both as a driver and as a library for color management and fast math.
// install the library:
// pio lib install "FastLED"

float accX_avg = 0, accY_avg = 0, accZ_avg = 0;
int n_average = 15;
bool IMU_ready = false;

// Display Buffer for the 5x5 RGB LED Matrix
// the first two elements are the size (5 rows and 5 coulums)
// then for every pixel three bytes for red, green and blue
uint8_t Display_Buffer[2 + 5 * 5 * 3];

// predefined display buffers to display some characters and numbers
#include "characters.c"

// Define own colors
// NOTE: The brightness of RGB LED is limited to 20 (global brightness scaling).
// DO NOT set it higher to avoid damaging the LED and acrylic screen.
#define cBLACK 0
#define cRED 1
#define cGREEN 2
#define cBLUE 3
#define cWHITE 4
#define cCYAN 5
#define cYELLOW 6
#define cMAGENTA 7
const uint8_t RGB_colors[8][3] = {{0x00, 0x00, 0x00},
                                  {0xF0, 0x00, 0x00},
                                  {0x00, 0xF0, 0x00},
                                  {0x00, 0x00, 0xF0},
                                  {0x70, 0x70, 0x70},
                                  {0x00, 0x70, 0x70},
                                  {0x70, 0x70, 0x00},
                                  {0x70, 0x00, 0x70}};
uint8_t display_color = cBLACK;
uint8_t goal_color = cBLACK;
uint8_t false_color = cBLACK;

// timer to switch the display to black
unsigned long display_off_millis;

// timer for the reaction time
unsigned long response_until_time;

// timer for wait a while
unsigned long wait_millis;

// game state machine states
#define s_START 1
#define s_WAIT_FLAT 2
#define s_SHOW_COLOR 3
#define s_WAIT_TILT 4
#define s_SHOW_RESULT 5
#define s_WAIT 6
#define s_SHOW_FINAL 7
#define s_GAME_FINISH 8
int game_state = s_START;

#define CENTER 0
#define LEFT 1
#define RIGHT 2
// if the device was tilted to the left or to the right
int tilt_move = CENTER;
// score reached within each level
int score = 0;
// game difficulty level
int game_level = 1;

// the difficulty of each level is coded in an array:
// 1 = correct color
// 2 = false color
// 3 = black pixel 
// 4 = white pixel
const int board_levels[13][25] = {{1,1,1,1,1, 1,1,1,1,1, 1,1,1,1,1, 2,2,2,2,2, 2,2,2,2,2}, // 0 (L1a)
                                  {2,2,2,2,2, 2,2,2,2,2, 1,1,1,1,1, 1,1,1,1,1, 1,1,1,1,1}, // 1 (L1b)
                                  {1,1,1,1,1, 1,1,1,1,1, 1,1,1,1,1, 1,1,2,2,2, 2,2,2,2,2}, // L2
                                  {1,1,1,1,1, 1,1,1,1,1, 1,1,1,1,1, 2,2,2,2,2, 2,2,2,2,2}, // L3
                                  {1,1,1,1,1, 1,1,1,1,1, 1,1,1,1,2, 2,2,2,2,2, 2,2,2,2,2}, // L4
                                  {1,1,1,1,1, 1,1,1,1,1, 1,1,1,2,2, 2,2,2,2,2, 2,2,2,2,2}, // L5
                                  {3,3,3,3,3, 1,1,1,1,1, 1,1,1,1,1, 2,2,2,2,2, 3,3,3,3,3}, // L6
                                  {3,3,3,3,3, 3,1,1,1,1, 1,1,1,2,2, 2,2,2,2,3, 3,3,3,3,3}, // L7
                                  {3,3,3,3,3, 3,3,3,1,1, 1,1,1,2,2, 2,2,3,3,3, 3,3,3,3,3}, // L8
                                  {3,3,3,3,3, 4,4,4,1,1, 1,1,1,2,2, 2,2,4,4,4, 3,3,3,3,3}, // L9
                                  {3,3,3,3,3, 4,4,4,4,1, 1,1,1,2,2, 2,4,4,4,4, 3,3,3,3,3}, // L10
                                  {4,4,4,4,4, 3,3,3,1,1, 1,1,1,2,2, 2,2,3,3,3, 4,4,4,4,4}, // L11
                                  {4,4,4,4,4, 4,4,4,4,1, 1,1,1,2,2, 2,4,4,4,4, 4,4,4,4,4}, // L12
                                 };
// the board for the actual move is filed with the data corresponding to the level
int board[25];
int board_index;
// For each level, the time, how long the board is visible (in ms)
int level_display_time[13] = {-1, 350, 300, 250, 200, 200, 150, 100, 50, 40, 30, 20, 10};
// For each level there is a separate reaction time requirement (in ms)
int level_reaction_time[13] = {-1, 1500, 1250, 1000, 900, 800, 700, 600, 500, 400, 350, 350, 350};

// A buffer to hold the result of each game
// withing one level (each level contains 25 turns)
uint8_t Result_Buffer[25];
int move_count = 0;

//==============================================================
// fill the display buffer with a solid color and diplay the buffer
// "color" is the index to the RGB_colors array
void fillScreen(uint8_t color){
    Display_Buffer[0] = 0x05;
    Display_Buffer[1] = 0x05;
    for (int i = 0; i < 25; i++){
        Display_Buffer[2 + i * 3 + 0] = RGB_colors[color][0];
        Display_Buffer[2 + i * 3 + 1] = RGB_colors[color][1];
        Display_Buffer[2 + i * 3 + 2] = RGB_colors[color][2];
    }
    M5.dis.displaybuff(Display_Buffer);
}

//==============================================================
// function to display a number on the LED Matrix screen
// the characters are defined in the fiel "characters.c"
void display_number(uint8_t number){
  // only single digit numbers between 0 and 9 are possible.
  if(number < 20){
    M5.dis.displaybuff((uint8_t *)image_numbers[number]);
  } else {
    M5.dis.displaybuff((uint8_t *)image_dot);
  }
}

//==============================================================
// function to display an animated star on the LED Matrix screen
void display_star(){
  for(int i=0; i<2; i++){
    for(int n=0; n<10; n++){
      M5.dis.displaybuff((uint8_t *)image_star[n]);
      delay(100);
    }
  }
}

//==============================================================
// function to display an animated line on the LED Matrix screen
void display_line(){
  for(int i=0; i<2; i++){
    M5.dis.displaybuff((uint8_t *)image_line);
    delay(500);
    M5.dis.displaybuff((uint8_t *)image_left);
    delay(500);
    M5.dis.displaybuff((uint8_t *)image_line);
    delay(500);
    M5.dis.displaybuff((uint8_t *)image_right);
    delay(500);
  }
  M5.dis.displaybuff((uint8_t *)image_line);
  delay(500);
}

void setup(){
    // start the ATOM device with serial and Display
    // begin(SerialEnable, I2CEnable, DisplayEnable)
    M5.begin(true, false, true);
    delay(50);
    Serial.println("M5ATOM Tilt-Game");
    Serial.println("v1.2 | 02.04.2020");
    // Show startup animation
    display_line();
    Serial.println("");
    Serial.println("INIT IMU");
    // check if IMU is ready
    if (M5.IMU.Init() == 0){
        IMU_ready = true;
        Serial.println("[OK] INIT ready");
        M5.dis.displaybuff((uint8_t *)image_CORRECT);
    } else {
        IMU_ready = false;
        Serial.println("[ERR] IMU INIT failed!");
        M5.dis.displaybuff((uint8_t *)image_WRONG);
    }
    delay(1000);
    display_off_millis = millis()+1000;
}

void loop(){
  // if display_off time is reached, switch the display to black
  if(millis() > display_off_millis){
    fillScreen(cBLACK);
  }
  // get the acceleration data
  // accX = right pointing vector
  //      tilt to the right: > 0
  //      tilt to the left:  < 0
  // accy = backward pointing vector
  //      tilt forward:  < 0
  //      tilt backward: > 0
  // accZ = upward pointing vector
  //      flat orientation: -1g
  float accX = 0, accY = 0, accZ = 0;
  M5.IMU.getAccelData(&accX, &accY, &accZ);
  // Average the acceleration data
  // simple "running average" method without storng the data in an array
  accX_avg = ((accX_avg * (n_average-1))+accX)/n_average;
  accY_avg = ((accY_avg * (n_average-1))+accY)/n_average;
  accZ_avg = ((accZ_avg * (n_average-1))+accZ)/n_average;

  // State machine for the game states
  switch (game_state){
    // Start a game level
    case s_START:
      // start after Button was pressed
      if (M5.Btn.wasPressed()){
        // reset the score for this level
        score = 0;
        move_count = 0;
        // display the actual level
        M5.dis.displaybuff((uint8_t *)image_L);
        delay(1000);
        display_number(game_level);
        delay(1000);
        fillScreen(cBLACK);
        delay(1000);
        // blink a dot 3 times as a count down
        for(int n=0; n<3; n++){
          M5.dis.displaybuff((uint8_t *)image_dot);
          delay(500);
          fillScreen(cBLACK);
          delay(500);
        }
        fillScreen(cBLACK);
        game_state = s_WAIT_FLAT;
      }
      break;

    // Wait until the device is held flat 
    case s_WAIT_FLAT:
      if(accZ_avg < 0.7 && abs(accX_avg) < 0.3){ // 0.3g = 27deg tilt angle
        wait_millis = millis()+1000;
        game_state = s_WAIT;
      }
      break;

    // just wait a while
    case s_WAIT:
      if(millis() > wait_millis){
        game_state = s_SHOW_COLOR;
      }
      break;

    // Show the level colors
    // randomly more red or more green pixel
    case s_SHOW_COLOR:
      // red or green?
      // 50/50 red or green
      if(random(1, 3) == 1){ // numbers between 1 and 2
        goal_color = cRED;
        false_color = cGREEN;
      } else {
        goal_color = cGREEN;
        false_color = cRED;
      }
      if(game_level == 1){
        // the first 10 moves = Simple full screen color
        // the first 4 moves always: RED, GREEN, RED, GREEN
        if(move_count < 10){
          if(move_count == 0 || move_count == 2){
            goal_color = cRED;
            false_color = cGREEN;
          }
          if(move_count == 1 || move_count == 3){
            goal_color = cGREEN;
            false_color = cRED;
          }
          fillScreen(goal_color);
        } else {
          // the next moves = = divied in top and bottom
          // fill the board with the level based information
          // 50/50 top or bottom
          if(random(1, 3) == 1){
            for(int i=0; i<25; i++){
              board[i] = board_levels[0][i];
            }
          } else {
            for(int i=0; i<25; i++){
              board[i] = board_levels[1][i];
            }
          }
          // fill the display buffer related to the board array
          Display_Buffer[0] = 0x05;
          Display_Buffer[1] = 0x05;
          for(int i=0; i<25; i++){
            if(board[i] == 1)
              display_color = goal_color;
            else
              display_color = false_color;
            Display_Buffer[2 + i * 3 + 0] = RGB_colors[display_color][0];
            Display_Buffer[2 + i * 3 + 1] = RGB_colors[display_color][1];
            Display_Buffer[2 + i * 3 + 2] = RGB_colors[display_color][2];
          }
          // show the resulting display buffer
          M5.dis.displaybuff(Display_Buffer);
        }
      } else {
        // level higher than 1
        // random Dots based on the level difficulty information
        // fill the board with the level based information
        for(int i=0; i<25; i++){
          board[i] = board_levels[game_level][i];
        }
        // fill the display buffer
        Display_Buffer[0] = 0x05;
        Display_Buffer[1] = 0x05;
        for(int i=0; i<25; i++){
          // select randomly a "non used" field out of the board array
          // "non used" field are the ones with numbers higher than 0
          do{
            board_index = random(0, 25);
          } while (board[board_index] == 0);
          // set the corresponding color
          display_color = false_color;
          if(board[board_index] == 1)
            display_color = goal_color;
          if(board[board_index] == 3)
            display_color = cBLACK;
          if(board[board_index] == 4)
            display_color = cWHITE;
          // mark the field as "used"
          board[board_index] = 0;
          // write the color in the display buffer
          Display_Buffer[2 + i * 3 + 0] = RGB_colors[display_color][0];
          Display_Buffer[2 + i * 3 + 1] = RGB_colors[display_color][1];
          Display_Buffer[2 + i * 3 + 2] = RGB_colors[display_color][2];
        }
        // show the resulting display buffer
        M5.dis.displaybuff(Display_Buffer);
      }
      // The time, how long the board is visible, depends on the level 
      display_off_millis = millis()+level_display_time[game_level];
      response_until_time = millis()+level_reaction_time[game_level];
      game_state = s_WAIT_TILT;
      break;

    // Wait until the device was tilted to the left or to the right
    case s_WAIT_TILT:
      // if response time limit is reached, switch to the RESULT state
      if(millis() > response_until_time){
        M5.dis.displaybuff((uint8_t *)image_O);
        delay(500);
        fillScreen(cBLACK);
        game_state = s_SHOW_RESULT;
      } else{
        // otherwise check if the device is tilted
        if(abs(accX_avg) > 0.5){ // 0.5g = 45deg tilt angle
          if(accX_avg < -0.5){
            tilt_move = LEFT;
          }
          if(accX_avg > 0.5){
            tilt_move = RIGHT;
          }
          if(tilt_move != CENTER){
            game_state = s_SHOW_RESULT;
            response_until_time = 0;
          }
        }   
      }
      break;

    // Analyse the response and show the result
    case s_SHOW_RESULT:
      response_until_time = 0;
      // correct response?
      if((goal_color == cRED && tilt_move == RIGHT) || (goal_color == cGREEN && tilt_move == LEFT)){
        M5.dis.displaybuff((uint8_t *)image_CORRECT);
        score++;
        Result_Buffer[move_count] = cGREEN;
      } else {
        // or tilted the wrong way?
        // reaction to slow?
        if(tilt_move != CENTER)
          M5.dis.displaybuff((uint8_t *)image_WRONG);
        Result_Buffer[move_count] = cRED;
      }
      display_off_millis = millis()+500;
      tilt_move = CENTER;
      move_count++;
      // each level contains 25 moves
      // if level is done, show the level result
      if(move_count == 25){
        delay(1000);
        game_state = s_SHOW_FINAL;
      } else {
        game_state = s_WAIT_FLAT;
      }
      break;

    // Show the result of the actual level
    case s_SHOW_FINAL:
      Serial.printf("Level finished. Score: %i/25\r\n", score);
      // if more than 75% correct answers, enable the next level
      // 18 out of 25 = 72%
      // 19 out of 25 = 76%
      if(score > 18){
        game_level++;
        Serial.println("[OK] Next level");
        display_star();
      } else {
        // next level not reached
        for(int n=0; n<4; n++){
          M5.dis.displaybuff((uint8_t *)image_sad);
          delay(500);
          fillScreen(cBLACK);
          delay(500);
        }
      }
      // fill the display buffer with the 25 answers
      Display_Buffer[0] = 0x05;
      Display_Buffer[1] = 0x05;
      for(int i=0; i<25; i++){
        Display_Buffer[2 + i * 3 + 0] = RGB_colors[Result_Buffer[i]][0];
        Display_Buffer[2 + i * 3 + 1] = RGB_colors[Result_Buffer[i]][1];
        Display_Buffer[2 + i * 3 + 2] = RGB_colors[Result_Buffer[i]][2];
      }
      // show the result for 5 seconds
      M5.dis.displaybuff(Display_Buffer);
      display_off_millis = millis()+2500;
      if(game_level < 13)
        game_state = s_START;
      else
        game_state = s_GAME_FINISH;
      break;

    // Game finished! All level passed!
    case s_GAME_FINISH:
      M5.dis.displaybuff((uint8_t *)image_CORRECT);
      delay(1000);
      fillScreen(cBLACK);
      delay(1000);
      break;

    // should never reached...
    default:
      break;
  }

  delay(5);
  M5.update();
}
