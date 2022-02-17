#include "motor.h"

#include <wiringPi.h>
void motor_turn_left()
{
int pin = LEFT_RIGHT;              
  if (wiringPiSetup () == -1)
    exit (1) ;               
 
 
  pinMode (pin, OUTPUT);

  int i;

  for ( i=0;i<2;i++) 
  {                      
      digitalWrite (pin, HIGH) ;         
      delay (0.5) ;        
      digitalWrite (pin, LOW) ;          
      delay (1) ;        
  }
}
void motor_turn_right()
{
int pin = LEFT_RIGHT;              
  if (wiringPiSetup () == -1)
    exit (1) ;               
 
 
  pinMode (pin, OUTPUT);
  int i;
  for (i=0;i<2;i++) 
  {                      
      digitalWrite (pin, HIGH) ;         
      delay (2) ;        
      digitalWrite (pin, LOW) ;          
      delay (1) ;        
  }
}
void motor_turn_up()
{
int pin = UP_DOWN;              
  if (wiringPiSetup () == -1)
    exit (1) ;               
 
 
  pinMode (pin, OUTPUT);
  int i;
  for (i=0;i<2;i++) 
  {                      
      digitalWrite (pin, HIGH) ;         
      delay (0.5) ;        
      digitalWrite (pin, LOW) ;          
      delay (1) ;        
  }
}
void motor_turn_down()
{
int pin = UP_DOWN;              
  if (wiringPiSetup () == -1)
    exit (1) ;               
 
 
  pinMode (pin, OUTPUT);
  int i;
  for (i=0;i<2;i++) 
  {                      
      digitalWrite (pin, HIGH) ;         
      delay (2) ;        
      digitalWrite (pin, LOW) ;          
      delay (1) ;        
  }
}
