#include <Arduino.h>
#include "fabgl.h"

#define clk_pin 33
#define data_pin 32

// FSM xt_keyb_states defining
#define START_BITS_START 0x00
#define START_BITS_END 0x01
#define PAYLOAD_RECEIVING 0x02

fabgl::Terminal  *xt_keyb_terminal;

uint8_t xt_keyb_val;
uint8_t xt_keyb_lastVal;
int xt_keyb_received_bits = 0;
uint8_t xt_keyb_state = START_BITS_START;

bool xt_keyb_isNumlock = false;
bool xt_keyb_isCapslock = false;
bool xt_keyb_isShift = false;
//size_t res = 0;  // Sending result (0 --> error; 1 --> OK)
//uint8_t sending_counter = 0;  // This variable prevents infinite sending loop

#define KEY_ESC 0xA0
#define KEY_BACKSPACE 0x08
#define KEY_TAB '\t'
#define KEY_RETURN '\n'
#define KEY_LEFT_CTRL 0xA2
#define KEY_LEFT_SHIFT 0xA3
#define KEY_LEFT_ALT 0xA4
#define KEY_LEFT_GUI 0xA5
#define KEY_SCROLL_LOCK 0xA6
#define KEY_RIGHT_SHIFT 0xA3
#define KEY_RIGHT_ALT 0xA4
#define KEY_PRTSCR 0xA7
#define KEY_CAPS_LOCK 0xA8
#define KEY_NUM_LOCK 0xA9
#define KEY_F1 0xB1
#define KEY_F2 0xB2
#define KEY_F3 0xB3
#define KEY_F4 0xB4
#define KEY_F5 0xB5
#define KEY_F6 0xB6
#define KEY_F7 0xB7
#define KEY_F8 0xB8
#define KEY_F9 0xB9
#define KEY_F10 0xBA

#define KEY_AE 0xC0
#define KEY_UE 0xC1
#define KEY_OE 0xC2
#define KEY_SS 0xC3


unsigned char translationTable[128] = {
  0,  // Not Used
  KEY_ESC,
  '1',
  '2',
  '3',
  '4',
  '5',
  '6',
  '7',
  '8',
  '9',
  '0',
  KEY_SS,  // ' and ? // ß
  0x60, // ´
  KEY_BACKSPACE,
  
  KEY_TAB,
  'q',
  'w',
  'e',
  'r',
  't',
  'z',
  'u',
  'i',
  'o',
  'p',
  KEY_UE, //ü
  '+', //+*
  KEY_RETURN,
  
  KEY_LEFT_CTRL,
  'a',
  's',
  'd',
  'f',
  'g',
  'h',
  'j',
  'k',
  'l',
  KEY_OE,// ö
  KEY_AE,// ä
  '#', //# '
  
  KEY_LEFT_SHIFT,
  '<', // >
  'y',
  'x',
  'c',
  'v',
  'b',
  'n',
  'm',
  ',', // :
  '.', // ;
  '-', // _
  KEY_RIGHT_SHIFT,
  KEY_PRTSCR, // PrtScrn
  
  KEY_LEFT_ALT,
  ' ',
  KEY_CAPS_LOCK,

  KEY_F1,
  KEY_F2,
  KEY_F3,
  KEY_F4,
  KEY_F5,
  KEY_F6,
  KEY_F7,
  KEY_F8,
  KEY_F9,
  KEY_F10,
  
  KEY_NUM_LOCK,  // Num Lock
  KEY_SCROLL_LOCK, // Scroll Lock
  '7', // 7
  '8', // 8
  '9', // 9
  '-', // -
  '4', // 4
  '5', // 5
  '6', // 6
  '+', // +
  '1', // 1
  '2', // 2
  '3', // 3
  '0', // 0
  '.' // .
};


void IRAM_ATTR clk_down() {
  switch (xt_keyb_state) {
    case START_BITS_START:
      if (!digitalRead(data_pin))
        xt_keyb_state = START_BITS_END;
      else
        xt_keyb_state = START_BITS_START;
      break;
    case START_BITS_END:
      if (digitalRead(data_pin))
        xt_keyb_state = PAYLOAD_RECEIVING;
      else
        xt_keyb_state = START_BITS_END;
      break;
    case PAYLOAD_RECEIVING:
      if (xt_keyb_received_bits < 7) {  // Receiving
        xt_keyb_val |= (digitalRead(data_pin) << xt_keyb_received_bits);
        xt_keyb_received_bits++;
      }
      else {  // Out Key
        xt_keyb_val |= (digitalRead(data_pin) << xt_keyb_received_bits);
        
        if (xt_keyb_val != xt_keyb_lastVal && (xt_keyb_val & 0x7f) <= 83) {
        pinMode(data_pin, OUTPUT);  // These instructions prevent Keyboard from sending data during time-consuming operations (BLE connection)

        digitalWrite(data_pin, LOW);
          int msb = xt_keyb_val & 0x80;  // Only the byte's MSB is on
          
          char buf[256];
          if (msb)
             sprintf(buf, "Released 0x%02x --> \"%c\"\n", xt_keyb_val & 0x7f, translationTable[xt_keyb_val & 0x7f]); // Debugging purpose
          else{
             sprintf(buf, "Pressed 0x%02x --> \"%c\"\n", xt_keyb_val & 0x7f, translationTable[xt_keyb_val & 0x7f]); // Debugging purpose
            Serial.print(buf);
          }
          
             
          if (xt_keyb_terminal->availableForWrite(true)) {
            unsigned char key = translationTable[xt_keyb_val & 0x7f];
            if (msb == 0) {  // msb == 0 --> press
              if (key >= KEY_F1 && key <= KEY_F10) {
                if(key == KEY_F10){
                  xt_keyb_terminal->localWrite("{F10}");
                } else {
                  xt_keyb_terminal->localWrite("{F");
                  xt_keyb_terminal->localWrite(key - 0xB0 + '0');
                  xt_keyb_terminal->localWrite("}");
                }
              }
              switch (key)
              {
              case KEY_ESC:
                xt_keyb_terminal->localWrite("{ESC}");
                break;
              case KEY_LEFT_ALT:
                xt_keyb_terminal->localWrite("{ALT}");
                break;
              case KEY_LEFT_CTRL:
                xt_keyb_terminal->localWrite("{CTRL}");
                break;

              case KEY_LEFT_SHIFT:
                xt_keyb_isShift = true;
                break;

              case KEY_CAPS_LOCK:
                xt_keyb_isCapslock = !xt_keyb_isCapslock;
                break;
              case KEY_NUM_LOCK:
                xt_keyb_isNumlock = !xt_keyb_isNumlock;
                break;              
              
              case KEY_AE:
                if (xt_keyb_isCapslock || xt_keyb_isShift) {
                  xt_keyb_terminal->localWrite("Ä");
                } else {
                  xt_keyb_terminal->localWrite("ä");
                }
                break;
              case KEY_OE:
                if (xt_keyb_isCapslock || xt_keyb_isShift) {
                  xt_keyb_terminal->localWrite("Ö");
                } else {
                  xt_keyb_terminal->localWrite("ö");
                }
                break;
              case KEY_UE:
                if (xt_keyb_isCapslock || xt_keyb_isShift) {
                  xt_keyb_terminal->localWrite("Ü");
                } else {
                  xt_keyb_terminal->localWrite("ü");
                }
                break;
              case KEY_SS: 
                if (xt_keyb_isCapslock || xt_keyb_isShift) {
                  xt_keyb_terminal->localWrite("?");
                } else {
                  xt_keyb_terminal->localWrite("ß");
                }
                break;

              default:
                if (xt_keyb_isCapslock || xt_keyb_isShift) {
                  // Upper Case
                  if(key >= 'a' && key <= 'z') {
                    xt_keyb_terminal->localWrite(key - 32);
                  } else if(key >= '0' && key <= '9') {
                    switch (key)
                    {
                    case '3':
                      xt_keyb_terminal->localWrite("§");
                      break;
                    case '7':
                      xt_keyb_terminal->localWrite('/');
                      break;
                    case '0':
                      xt_keyb_terminal->localWrite('=');
                      break;
                    
                    default:
                      xt_keyb_terminal->localWrite(key-('1'-'!'));
                    }
                  }
                  else if(key == '<')
                    xt_keyb_terminal->localWrite('>');
                  else if(key == ',')
                    xt_keyb_terminal->localWrite(';');
                  else if(key == '.')
                    xt_keyb_terminal->localWrite(':');
                  else if(key == '-')
                    xt_keyb_terminal->localWrite('_');
                  else if(key == '#')
                    xt_keyb_terminal->localWrite('\'');
                  else if(key == '+')
                    xt_keyb_terminal->localWrite('*');
                  else
                    if (key >= ' ' && key <= '~')
                      xt_keyb_terminal->localWrite(key);

                } else {
                    if (key >= ' ' && key <= '~')
                      xt_keyb_terminal->localWrite(key);
                }

                xt_keyb_isShift = false;
                break;
              }
            }
  
            xt_keyb_lastVal = xt_keyb_val;
            pinMode(data_pin, INPUT_PULLUP);  // Re-activate Keyboard sending data
        }
        
        xt_keyb_received_bits = 0;
        xt_keyb_val = 0x00;
        xt_keyb_state = START_BITS_START;
      }
      }
      break;
  }
}

void initXTKeyb(fabgl::Terminal  *term) {
  xt_keyb_terminal = term;
  /* SOFT RESET */
  pinMode(data_pin, INPUT);  // Data Line Hi
  digitalWrite(data_pin, HIGH);
  pinMode(clk_pin, INPUT);
  digitalWrite(clk_pin, HIGH);
  delay(5);
  digitalWrite(clk_pin, LOW);  // Falling Edge
  delay(21);  // Wait ~20ms
  digitalWrite(clk_pin, HIGH);  // Rising Edge

  /* RECEIVING PIN MODE */
  pinMode(clk_pin, INPUT_PULLUP);
  pinMode(data_pin, INPUT_PULLUP);

  /* WAIT */
  //delay(100);

  attachInterrupt(digitalPinToInterrupt(clk_pin), clk_down, FALLING);
}
