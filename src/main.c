#include "stm32f0xx_conf.h"
#include "3595-LCD-Driver.h"
#include "main.h"
#include "STM32-Debounce.h"
#include "font5x8.h"
#include "98x67-snake.h"
#include <stdlib.h>

#define BACKGROUND 0xFF
#define FOREGROUND 0x00
#define GAME_OVER_FONT_COLOR  red
#define GAME_OVER_BD_COLOR  BACKGROUND
#define FRUIT_COLOR red

#define SNAKE_GIRTH   2
#define MAX_NODES     400
#define GAMEBOARD_X   (PAGE_SIZE+1)/SNAKE_GIRTH
#define GAMEBOARD_Y   (ROW_SIZE+1)/SNAKE_GIRTH

uint8_t game_running = 0;
uint8_t font_cursor_x = 15;
uint8_t font_cursor_y = 40;
uint16_t tail;
uint16_t head;
uint16_t snake_length_limit;
uint16_t snake_length_current;

typedef struct {
  uint8_t x;
  uint8_t y;
} point;

point corners[MAX_NODES];
int8_t dirY;
int8_t dirX;
point fruit;
uint8_t change_dir;

//Variables
static __IO uint32_t TimingDelay;
volatile uint8_t move_tick = 0;

//Prototypes
uint16_t get_next_node(uint16_t thisNode);
uint16_t get_previous_node(uint16_t thisNode);
uint16_t get_node_list_length(uint16_t node1, uint16_t node2);
uint8_t collision(point target);

void Write_Char(unsigned char letter, unsigned char fgcolor, unsigned char bgcolor)		//Function that writes one character to display
{
  //TODO: Prevent non-valid characters from crashing program
  
  //Setup display to write one char:
  LCD_Out(0x2A, 1);
  LCD_Out(font_cursor_x, 0);
  LCD_Out(font_cursor_x+5, 0);
  LCD_Out(0x2B, 1);
  LCD_Out(font_cursor_y, 0);
  LCD_Out(font_cursor_y+7, 0);
  LCD_Out(0x2C, 1);
  
  //letters come from font5x8[] in progmem (font5x8.h)
  letter -= 32;						//Adjust char value to match our font array indicies
  
  unsigned char temp[5];
  for (unsigned char i=0; i<5; i++)				//Read one column of char at a time
  {
    temp[i] = font5x8[(5 * letter) + i];	//Get column from progmem
  }
  
  for (unsigned char j=0; j<8; j++)						//Cycle through each bit in column
  {
    LCD_Out(bgcolor, 0);
    for (unsigned char k=0; k<5; k++)
    {
	    if (temp[k] & 1<<j) LCD_Out(fgcolor, 0);
	    else LCD_Out(bgcolor, 0);
    }
  }
}

void Double_Char(unsigned char letter, unsigned char fgcolor, unsigned char bgcolor)		//Function that writes one character to display
{
  //TODO: Prevent non-valid characters from crashing program
  
  //Setup display to write one char:
  LCD_Out(0x2A, 1);
  LCD_Out(font_cursor_x, 0);
  LCD_Out(font_cursor_x+10, 0);
  LCD_Out(0x2B, 1);
  LCD_Out(font_cursor_y, 0);
  LCD_Out(font_cursor_y+15, 0);
  LCD_Out(0x2C, 1);
  
  //letters come from font5x8[] in progmem (font5x8.h)
  letter -= 32;						//Adjust char value to match our font array indicies
  
  unsigned char temp[5];
  for (unsigned char i=0; i<5; i++)				//Read one column of char at a time
  {
    temp[i] = font5x8[(5 * letter) + i];	//Get column from progmem
  }

  for (unsigned char j=0; j<8; j++)						//Cycle through each bit in column
  {
    LCD_Out(bgcolor, 0);

    for (unsigned char k=0; k<5; k++)
    {
      if (temp[k] & 1<<j) { LCD_Out(fgcolor, 0); LCD_Out(fgcolor, 0); }
	    else { LCD_Out(bgcolor, 0); LCD_Out(bgcolor, 0); }
    }
    LCD_Out(bgcolor, 0);    
    for (unsigned char k=0; k<5; k++)
    {
      if (temp[k] & 1<<j) { LCD_Out(fgcolor, 0); LCD_Out(fgcolor, 0); }
	    else { LCD_Out(bgcolor, 0); LCD_Out(bgcolor, 0); }
    }
  }
}

void Write_String(char* myString, unsigned char fgcolor, unsigned char bgcolor)
{
  while (*myString)
  {
    Write_Char(*myString,fgcolor,bgcolor);
    Double_Char(*myString,fgcolor,bgcolor);
    font_cursor_x += 12; //advance cursor
    ++myString;
  }
}


void _delay_ms(__IO uint32_t nTime)
{
  TimingDelay = nTime;

  while(TimingDelay != 0);
}

/**
  * @brief  Decrements the TimingDelay variable.
  * @param  None
  * @retval None
  */
void TimingDelay_Decrement(void)
{
  if (TimingDelay != 0x00)
  { 
    TimingDelay--;
  }
}


void SysTick_Handler(void) {
  static uint16_t tick = 0;
  static uint16_t ten_ms_tick = 0;
  
  if (game_running)
  {
    switch (tick++) {
      //TODO: Add speed element to game
      case 150:
        tick = 0;
        move_tick = 1;
        break;
    }
  }
  
  TimingDelay_Decrement();
  
  if (ten_ms_tick++ > 9) {
    ten_ms_tick = 0;
    debounce_interrupt_service();
  }
}

void change_direction(void)
{
  switch (change_dir) {
    case 1:
      dirX = -1;
      dirY = 0;
      break;
    case 2:
      dirX = 0;
      dirY = -1;
      break;
    case 3:
      dirX = 1;
      dirY = 0;
      break;
    case 4:
      dirX = 0;
      dirY = 1;
      break;
  }
  change_dir = 0;
}

uint8_t absolute_difference(uint8_t a, uint8_t b)
{
  int16_t unknown = (int16_t)a - b;
  return (uint8_t)(unknown<0?0-unknown:unknown); 
}

uint8_t neighbors(point node1, point node2)
{
  if ((absolute_difference(node1.x,node2.x) == 1) || (absolute_difference(node1.y,node2.y) == 1)) return 1;
  return 0;
}

void make_fruit(void) {
  while (1) {
    fruit.x = (uint8_t)(rand()%(GAMEBOARD_X));
    fruit.y = (uint8_t)(rand()%(GAMEBOARD_Y));
    if (collision(fruit) == 0) break;
  }
  Draw_Box(fruit.x*SNAKE_GIRTH,fruit.y*SNAKE_GIRTH,(fruit.x*SNAKE_GIRTH)+SNAKE_GIRTH-1,(fruit.y*SNAKE_GIRTH)+SNAKE_GIRTH-1,FRUIT_COLOR);
}

uint8_t ate_fruit(uint8_t x, uint8_t y)
{
  if ((fruit.x == x) && (fruit.y == y)) return 1;
  return 0;
}

void game_over(void)
{
  font_cursor_x = ((PAGE_SIZE+1)/2)-((12*4)/2);
  font_cursor_y = ((ROW_SIZE+1)/2)-17;
  Write_String("GAME",GAME_OVER_FONT_COLOR,GAME_OVER_BD_COLOR);
  font_cursor_x = ((PAGE_SIZE+1)/2)-((12*4)/2);
  font_cursor_y = ((ROW_SIZE+1)/2)+1;
  Write_String("OVER",GAME_OVER_FONT_COLOR,GAME_OVER_BD_COLOR);
  game_running = 0;
}

void move_head(uint8_t new_dir)
{
  if (new_dir)
  {
    //Copy head to new position
    head = get_next_node(head); //increment head
    corners[head].x = corners[get_previous_node(head)].x;
    corners[head].y = corners[get_previous_node(head)].y;
    change_direction();  //change direction
  }
  
  //Have we left the game board?
  if ((corners[head].x == 0) && (dirX == -1)) { game_over(); return; }
  if ((corners[head].y == 0) && (dirY == -1)) { game_over(); return; }
  if ((corners[head].x == GAMEBOARD_X-1) && (dirX == 1)) { game_over(); return; }
  if ((corners[head].y == GAMEBOARD_Y-1) && (dirY == 1)) { game_over(); return; }
  corners[head].x += dirX;
  corners[head].y += dirY;
  ++snake_length_current;
}

void follow_tail(void)
{
  --snake_length_current;
  //is tail a neighbor of next?
  if (neighbors(corners[tail],corners[get_next_node(tail)]))
  {
    tail = get_next_node(tail);
  }
  
  //find which axis tail and next have in common
  else
  {
    if (corners[tail].x == corners[get_next_node(tail)].x)
    {
      //These points have the same X, so make adjustment to the Y
      if ((corners[tail].y - corners[get_next_node(tail)].y) < 0) corners[tail].y += 1;
      else corners[tail].y -= 1; 
    }
    else
    {
      //These points have the same Y, so make adjustment to the X
      if ((corners[tail].x - corners[get_next_node(tail)].x) < 0) corners[tail].x += 1;
      else corners[tail].x -= 1; 
    }
  }
}

uint8_t collision(point target)
{
  uint16_t lower = 0;
  uint16_t upper = 0;
  uint16_t testpoint = 1;
  uint16_t i=tail;
  uint16_t nextNode = get_next_node(i);;

  //Check to see if we hit part of the snake
  //traverse all nodes from tail forward
  for (int16_t count=get_node_list_length(tail,head)-3; count>0; count--)
  //while (nextNode<head)
    //( check head-3 because you can't run into a segment any close than that to the head)
  { 
    //check to see if head's x or y are shared with the current point
    if ((target.x == corners[i].x) && (corners[i].x == corners[nextNode].x))
    {
      //which point is the higher  number?
      if (corners[i].y < corners[nextNode].y) {lower = corners[i].y; upper = corners[nextNode].y;}
      else {lower = corners[nextNode].y; upper = corners[i].y;}
      testpoint = target.y;
    }
    else if ((target.y == corners[i].y) && (corners[i].y == corners[nextNode].y))
    {
      //which point is the higher  number?
      if (corners[i].x < corners[nextNode].x) {lower = corners[i].x; upper = corners[nextNode].x;}
      else {lower = corners[nextNode].x; upper = corners[i].x;}
      testpoint = target.x;
    }
    
    //Now check to see if head is a point between this node and the next
    if ((lower<=testpoint) && (testpoint<= upper)) return 1;
    
    i = nextNode;
    nextNode = get_next_node(i);
  }
  return 0;
}

void draw_graphic(void) {

  LCD_Out(0x2A, 1);
  LCD_Out(0, 0);
  LCD_Out(97, 0);
  LCD_Out(0x2B, 1);
  LCD_Out(0, 0);
  LCD_Out(66, 0);
  LCD_Out(0x2C, 1);
  
  for (uint16_t i=0; i<6566; i++)
  {
    LCD_Out(snake_graphic_cmap[snake_graphic[i]],0);
    //LCD_Out(white,0);
  }
}

void snake_init(void)
{
  tail = 0;
  head = 1;
  dirY = 0;
  dirX = 1;
  change_dir = 0;
  snake_length_limit = 22;    //Change this to alter starting length
  snake_length_current = snake_length_limit;
  corners[head].x = 4+snake_length_current;
  corners[head].y = 4;
  corners[tail].x = 4;
  corners[tail].y = 4;
  Draw_Box(0,0,PAGE_SIZE,ROW_SIZE,black);
  Draw_Box(0,0,PAGE_SIZE-((PAGE_SIZE+1)%SNAKE_GIRTH),ROW_SIZE-((ROW_SIZE+1)%SNAKE_GIRTH),BACKGROUND);
  Draw_Box(corners[tail].x*SNAKE_GIRTH,corners[tail].y*SNAKE_GIRTH,(corners[head].x*SNAKE_GIRTH)+SNAKE_GIRTH-1,(corners[head].y*SNAKE_GIRTH)+SNAKE_GIRTH-1,FOREGROUND);
  srand((uint16_t)SysTick->VAL);
  make_fruit();
}

/*--------------------------------------------------------------------------
  FUNC: 7/11/12 - Gets index of next node in a ring buffer array
  PARAMS: Current index
  RETURNS: Next index (will go 'around the bend' if necessary)
  NOTE: Depends on the constant MAX_NODES which defines size of array
--------------------------------------------------------------------------*/
uint16_t get_next_node(uint16_t thisNode) {
  uint16_t nextNode = thisNode + 1;
  if (nextNode >= MAX_NODES) nextNode = 0;
  return nextNode;  
}

/*--------------------------------------------------------------------------
  FUNC: 7/11/12 - Gets index of previous node in a ring buffer array
  PARAMS: Current index
  RETURNS: Previous index (will go 'around the bend' if necessary)
  NOTE: Depends on the constant MAX_NODES which defines size of array
--------------------------------------------------------------------------*/
uint16_t get_previous_node(uint16_t thisNode) {
  if (thisNode) return thisNode-1;  // thisNode is not zero so just decrement
  return MAX_NODES-1; //thisNode is zero so go around the bend  
}

/*--------------------------------------------------------------------------
  FUNC: 7/11/12 - Finds length in ring buffer from one node to the next
  PARAMS: Index of first node, Index of second node
  RETURNS: Total number of nodes (inclusive)
  NOTE: Depends on the constant MAX_NODES which defines size of array
        will go 'around the bend' if necessary
--------------------------------------------------------------------------*/
uint16_t get_node_list_length(uint16_t node1, uint16_t node2) {
  if (node1 == node2) return 1;
  if (node1 < node2) return (node2-node1)+1;  //Adding 1 to adjust for 0 index
  else return node2+(MAX_NODES-node1)+1;    //Adding 1 to adjust for 0 index
}

int main(void)
{ 
  SPI_Config();

  //Setup output for blinking blue LED  
  RCC->AHBENR |= RCC_AHBENR_GPIOCEN;   // enable the clock to GPIOC
  //GPIOC->MODER |= (1<<16); //Deprecated, used to make blue LED blink
  GPIOC->PUPDR |= (1<<10) | (1<<8) | (1<<4) | (1<<2) | (1<<0); //Pull-up resistor
  
  SysTick_Config(SystemCoreClock/1000);
  
  LCD_init();
   
  draw_graphic();  
  
  clear_keys(REPEAT_MASK); //Clear false reading due to active high buttons
  while(1) {
    if (move_tick) {
           
      /*
      //Move
      if (corners[head].x == 0) dirX = 1;
      if (corners[head].x == PAGE_SIZE) dirX = -1;
      if (corners[head].y == 0) dirY = 1;
      if (corners[head].y == ROW_SIZE) dirY = -1;
      */
      //corners[head].x += dirX;
      //corners[head].y += dirY;

      move_head(change_dir);
      if (ate_fruit(corners[head].x,corners[head].y))
      {
        snake_length_limit += (snake_length_limit/10);
        make_fruit();
      }
      if (collision(corners[head])) game_over();
      else {
        Draw_Box(corners[head].x*SNAKE_GIRTH,corners[head].y*SNAKE_GIRTH,(corners[head].x*SNAKE_GIRTH)+SNAKE_GIRTH-1,(corners[head].y*SNAKE_GIRTH)+SNAKE_GIRTH-1,FOREGROUND); //Redraw
        if (snake_length_current > snake_length_limit)
        {
          
          Draw_Box(corners[tail].x*SNAKE_GIRTH,corners[tail].y*SNAKE_GIRTH,(corners[tail].x*SNAKE_GIRTH)+SNAKE_GIRTH-1,(corners[tail].y*SNAKE_GIRTH)+SNAKE_GIRTH-1,BACKGROUND); //Erase
          follow_tail();
        }
      }    
      move_tick = 0;
    }
    if (get_key_press(1<<KEY0) && (dirX == 0)) change_dir = 1; // Left { dirX = -1; dirY = 0; }
    if (get_key_press(1<<KEY1) && (dirY == 0)) change_dir = 2; // Up { dirX = 0; dirY = -1; }
    if (get_key_press(1<<KEY2) && (dirX == 0)) change_dir = 3; // Right { dirX = 1; dirY = 0; }
    if (get_key_press(1<<KEY3) && (dirY == 0)) change_dir = 4; // Down { dirX = 0; dirY = 1; }
    if (get_key_press(1<<KEY4) && (game_running==0)) 
    {
      snake_init(); 
      game_running = 1;
    }
  }
}
