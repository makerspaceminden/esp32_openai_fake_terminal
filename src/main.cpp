/*
  Created by Fabrizio Di Vittorio (fdivitto2013@gmail.com) - <http://www.fabgl.com>
  Copyright (c) 2019-2022 Fabrizio Di Vittorio.
  All rights reserved.


* Please contact fdivitto2013@gmail.com if you need a commercial license.


* This library and related software is available under GPL v3.

  FabGL is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  FabGL is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with FabGL.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
//#include <wordwrap.h>

#include "config.h"
//#include "cert.h"

#include "fabgl.h"
//#include "xtKeyboard.hpp"

// timings->label, &freq, &hdisp, &hsyncstart, &hsyncend, &htotal, &vdisp, &vsyncstart, &vsyncend, &vtotal, &pos
#define MDA_600x320_50Hz "\"600x320@50Hz\" 13.824 600 615 705 752 320 328 330 352 +HSync -VSync"
#define MDA_640x320_50Hz "\"640x320@50Hz\" 13.824 640 650 735 752 320 328 330 352 +HSync -VSync"
//hfront hsync hback pixels vfront vsync vback lines divy pixelclock hpolaritynegative vpolaritynegative
  //Mode myMode(15, 90, 45, 600, 8, 2, 20, 320, 1, 13824000, 1, 1);

// PS2 Keyboard CLK: Pin 33, DATA: Pin 32

#define HSYNC GPIO_NUM_5
#define VSYNC GPIO_NUM_18
#define VIDEO GPIO_NUM_19
#define HI_INTENSITY GPIO_NUM_21
// HI_Intensity is on red channel, and VIDEO is on green channel
#define MDA_BLACK Color::Black
#define MDA_WHITE Color::BrightYellow
#define MDA_GRAY Color::BrightGreen
#define PAYLOAD_SIZE 6*1024


fabgl::VGA4Controller DisplayController;
fabgl::Terminal        Terminal;
//fabgl::Keyboard        Keyboard;
fabgl::LineEditor        LineEditor(&Terminal);
fabgl::PS2Controller     PS2Controller;


char const * AUTOEXEC = "##quickinfo\r";

enum class State { Prompt,
                   PromptInput,
                   UnknownCommand,
                   Help,
                   Info,
                   ChatGPT,
                   Reset,
                   Reboot,
                   ShowPrompt,
                   QuickInfo
                 };


State        state = State::Prompt;
char const * currentScript = nullptr;
bool         error = false;

//#define PROMPT "Du bist ein Comodore 64 Computer. Der Benutzer wird dir Fragen über den Comodore 64 stellen. Du sollst dem Benutzer helfen, einfache Programme in Basic zu schreiben, die im Comodore 64 laufen können, aber auch kurze Antworten zum Comodore 64 geben. Obwohl du ein alter computer bist du hast aktuelle informationen ueber die Welt."

#define PROMPT "I want you to act as a Linux terminal. I will type commands and you will reply with what the terminal should show." \
        "I want you to only reply with the terminal output, and nothing else. Do not use a Code Block. Do not write explanations." \
        "Do not type commands unless I instruct you to do so. When I need to tell you something in English I will do so by putting text" \
        " inside curly brackets {like this}. Output only ascii characters. Do not use special characters. Do not use emojis. Do not use" \
        " colors. Do not use escape codes. Keep the output short."\
        "Your System stats are: Dual processor system with 2 Xtensa LX6 Processors at 160 MHz, 520 KB of SRAM and 4 MB of Flash. "\
        "You are running \"UNIFOX\" Version 3.27.1, an UNIX variant made by the Company \"UNIFOX Inc.\" (c) 1978-1995 (Think like in an alternate reality game). "\
        "Your hostname is \"shadowFOX\" and the logged in user is \"root\". "\
        "You are connected to the internet. Your IP address is 10.42.1.10. "\
        "You have a black and white screen (VT-100 Terminal) with a screen size of 640x320 pixels / 80x40 Characters. "\
        "The shell is called the shadowFOX Universal Intelligent Data Transformer, with the welcome message being "\
        "\"You can interact with the using regular language or by calling programs just like on UNIX!\". "\
        "Assume that every entered command exists (and works) on the computer. If a user enters a command that you dont recognize, assume it exists and generate fitting output from context and its name."
        
#define CHAT_BUFFER_SZ 5
DynamicJsonDocument response(1024);
static JsonDocument gptPrompt;

// Increase stack size for loop task
SET_LOOP_TASK_STACK_SIZE(20*1024); // 16KB

String userBuf[CHAT_BUFFER_SZ];
String assistantBuf[CHAT_BUFFER_SZ];
int chatBufIndex = 0;

String serialInput;
WiFiClientSecure client;

void convertToCP437(String *inputString){
/* CP437
  { VK_UMLAUT_u,     0x81 },    // ü
  { VK_UMLAUT_o,     0x94 },    // ö
  { VK_UMLAUT_a,     0x84 },    // ä
  { VK_ESZETT,       0xe1 },    // ß
  { VK_UMLAUT_A,     0x8e },    // Ä
  { VK_UMLAUT_O,     0x99 },    // Ö
  { VK_UMLAUT_U,     0x9a },    // Ü
*/
  inputString->replace("ä", "\x84");
  inputString->replace("ö", "\x94");
  inputString->replace("ü", "\x81");
  inputString->replace("ß", "\xe1"); // 0xe1 CP437
  inputString->replace("Ä", "\x8e");
  inputString->replace("Ö", "\x99");
  inputString->replace("Ü", "\x9a");
}

void convertFromCP437(String *inputString){
  inputString->replace("\x84", "ä");
  inputString->replace("\x94", "ö");
  inputString->replace("\x81", "ü");
  inputString->replace("\xe1", "ß"); // 0xe1 CP437
  inputString->replace("\x8e", "Ä");
  inputString->replace("\x99", "Ö");
  inputString->replace("\x9a", "Ü");

  inputString->replace("\"", "\\\"");
}


void slowPrintf(const char * format, ...)
{
  va_list ap;
  va_start(ap, format);
  int size = vsnprintf(nullptr, 0, format, ap) + 1;
  if (size > 0) {
    va_end(ap);
    va_start(ap, format);
    char buf[size + 1];
    vsnprintf(buf, size, format, ap);
    for (int i = 0; i < size; ++i) {
      Terminal.write(buf[i]);
      delay(25);
    }
  }
  va_end(ap);
}

void slowPrintfTime(int delayTime, const char * format, ...)
{
  va_list ap;
  va_start(ap, format);
  int size = vsnprintf(nullptr, 0, format, ap) + 1;
  if (size > 0) {
    va_end(ap);
    va_start(ap, format);
    char buf[size + 1];
    vsnprintf(buf, size, format, ap);
    for (int i = 0; i < size; ++i) {
      Terminal.write(buf[i]);
      delay(delayTime);
    }
  }
  va_end(ap);
}

String getOpenAiAnswer(String *inputString)
{
  convertFromCP437(inputString);

  gptPrompt["model"] = "gpt-3.5-turbo";
  gptPrompt["temperature"] = 0.7;
  gptPrompt["messages"][0]["role"] = "system";
  gptPrompt["messages"][0]["content"] = PROMPT;
  int messageIndex = 0;

  for(int i = 0; i < CHAT_BUFFER_SZ; i++){
    int bufIdx = (chatBufIndex + i) % CHAT_BUFFER_SZ;
    // Lets hope those 2 do never get out of sync...
    if(userBuf[bufIdx].length() == 0){
      continue;
    }
    Serial.println("Found a message to send to GPT:");
    Serial.println(userBuf[bufIdx]);
    Serial.println(assistantBuf[bufIdx]);
    messageIndex++;
    gptPrompt["messages"][messageIndex]["role"] = "user";
    gptPrompt["messages"][messageIndex]["content"] = userBuf[bufIdx];
    messageIndex++;
    gptPrompt["messages"][messageIndex]["role"] = "assistant";
    gptPrompt["messages"][messageIndex]["content"] = assistantBuf[bufIdx];
  }
  messageIndex++;
  gptPrompt["messages"][messageIndex]["role"] = "user";
  gptPrompt["messages"][messageIndex]["content"] = *inputString;

  // Size determined by empirical testing, seems enough
  static char payloadC[PAYLOAD_SIZE];
  int payloadsz = serializeJson(gptPrompt, payloadC, PAYLOAD_SIZE);
  Serial.println(payloadC);
  String responseMessage;

  if (payloadsz == PAYLOAD_SIZE || payloadsz == 0) {
    // Clear the buffer if its too full
    for (int i = 0; i < CHAT_BUFFER_SZ; i++) {
      userBuf[i] = "";
      assistantBuf[i] = "";
    }
    chatBufIndex = 0;
    gptPrompt.clear();
    gptPrompt["model"] = "gpt-3.5-turbo";
    gptPrompt["temperature"] = 0.7;
    gptPrompt["messages"][0]["role"] = "system";
    gptPrompt["messages"][0]["content"] = PROMPT;
    int messageIndex = 0;
    messageIndex++;
    gptPrompt["messages"][messageIndex]["role"] = "user";
    gptPrompt["messages"][messageIndex]["content"] = *inputString;
  } 

  HTTPClient httpClient;
  httpClient.begin(client, OPENAI_URL);
  httpClient.setTimeout(60000);
  httpClient.setConnectTimeout(60000);
  httpClient.addHeader("Content-Type", "application/json");
  httpClient.addHeader("Authorization", OPENAI_TOKEN);
  
  int httpCode = httpClient.POST((uint8_t *)payloadC, payloadsz);
  if (httpCode > 0) {
    if (httpCode == HTTP_CODE_OK) {
      String responseStr = httpClient.getString();
      deserializeJson(response, responseStr);
      responseMessage = response["choices"][0]["message"]["content"].as<String>();

      // Copy the data to the buffers
      userBuf[chatBufIndex] = String(inputString->c_str());
      assistantBuf[chatBufIndex] = String(responseMessage.c_str());
      chatBufIndex = (chatBufIndex + 1) % CHAT_BUFFER_SZ;

      // CP437 conversion
      convertToCP437(&responseMessage);
      responseMessage.replace("\n", "\r\n");
    }
    else
    {
      responseMessage = "SEGMENTATION FAULT (core dumped) at 0x0"+String(httpCode);
    }
    //httpClient.end();
  } else {
      Serial.printf("HTTP failed: %d\n", httpCode);
      switch (httpCode)
      {
      case HTTPC_ERROR_READ_TIMEOUT:
        responseMessage = "Timeout while waiting for response. Try again in a couple minutes.";
        break;
      
      default:
        responseMessage = "Unhandled exception ("+String(httpCode*-1)+") while executing at: 0xFFFF (LOCK CMPXCHG8B r32)";
        break;
      }
  }
  httpClient.end();
  
  gptPrompt.clear();
	return responseMessage;
}

void exe_info()
{
  Terminal.write("\e[97mTerminal System Information:\r\n");
  Terminal.printf("\e[92mScreen Size        :\e[93m %d x %d\r\n", DisplayController.getViewPortWidth(), DisplayController.getViewPortHeight());
  Terminal.printf("\e[92mTerminal Size      :\e[93m %d x %d\r\n", Terminal.getColumns(), Terminal.getRows());
  Terminal.printf("\e[92mFree Heap Memory   :\e[93m %d kB\r\n", ESP.getFreeHeap()/1024);
  Terminal.printf("\e[92mFree DMA Memory    :\e[93m %d kB\r\n", heap_caps_get_free_size(MALLOC_CAP_DMA)/1024);
  Terminal.printf("\e[92mFree 32 bit Memory :\e[93m %d kB\r\n", heap_caps_get_free_size(MALLOC_CAP_32BIT)/1024);
  if (WiFi.status() == WL_CONNECTED) {
    Terminal.printf("\e[92mWiFi SSID          :\e[93m %s\r\n", WiFi.SSID().c_str());
    Terminal.printf("\e[92mCurrent IP         :\e[93m %s\r\n", WiFi.localIP().toString().c_str());
  }
  error = false;
  state = State::Prompt;
}

void exe_showPrompt()
{
  Terminal.write("\e[97mChatGPT Prompt:\r\n");
  String prompt = PROMPT;
  convertToCP437(&prompt);
  Terminal.write(prompt.c_str());
  Terminal.write("\e[97m\r\n");
  state = State::Prompt;
}


void exe_quickInfo()
{
  
  slowPrintf("\e[97mLogging into terminal interface as root");
  for(int i=0;i<7;i++){
		delay(100);
		Terminal.write(".");
	}
  Terminal.write("success!\r\n\n");
  slowPrintf("\e[92mWelcome to the shadowFOX Universal Intelligent Data Transformer!\r\n");
  slowPrintf("\e[92mYou can interact with the using regular language or by calling programs\r\n just like on UNIX!\r\n\n");
  state = State::Prompt;
}


void decode_command()
{
  auto inputLine = LineEditor.get();
  if (*inputLine == 0)
    state = State::Prompt;
  else if (strncmp(inputLine, "##info", 6) == 0)
    state = State::Info;
  else if (strncmp(inputLine, "##quickinfo", 8) == 0)
    state = State::QuickInfo;
  else if (strncmp(inputLine, "##prompt", 8) == 0)
    state = State::ShowPrompt;
  else if (strncmp(inputLine, "##reset", 7) == 0)
    state = State::Reset;
  else if (strncmp(inputLine, "##reboot", 8) == 0)
    state = State::Reboot;
  else if (inputLine[0] == '#')
    state = State::UnknownCommand;
  else
    state = State::ChatGPT;
}


void exe_prompt()
{
  if (currentScript) {
    // process commands from script
    if (*currentScript == 0 || error) {
      // end of script, return to prompt
      currentScript = nullptr;
      state = State::Prompt;
    } else {
      // execute current line and move to the next one
      int linelen = strchr(currentScript, '\r') - currentScript;
      LineEditor.setText(currentScript, linelen);
      currentScript += linelen + 1;
      decode_command();
    }
  } else {
    // process commands from keyboard
    Terminal.write("\e[97mroot@shadowFOX:~$ ");
    state = State::PromptInput;
  }
}


void exe_promptInput()
{
  LineEditor.setText("");
  LineEditor.edit();
  decode_command();
}

void exe_chatgpt(){
  String inputLine = LineEditor.get();
  Terminal.write("\e[92m");
  slowPrintf(getOpenAiAnswer(&inputLine).c_str());
  Terminal.write("\r\n");
  state = State::Prompt;
}
void exe_reset(){
  for (int i = 0; i < CHAT_BUFFER_SZ; i++) {
    userBuf[i] = "";
    assistantBuf[i] = "";
  }
  chatBufIndex = 0;

  Terminal.write("\r\n");
  state = State::Prompt;
}


void setup()
{
  Serial.begin(115200); delay(500); Serial.write("\n\n\n"); // DEBUG ONLY

  for (int i = 0; i < CHAT_BUFFER_SZ; i++) {
    userBuf[i] = "";
    assistantBuf[i] = "";
  }

  disableCore0WDT();
  delay(100);
  disableCore1WDT();

  // Reduces some defaults to save RAM...
  fabgl::VGAController::queueSize                    = 128;
  fabgl::Terminal::inputQueueSize                    = 32;
  fabgl::Terminal::inputConsumerTaskStackSize        = 1300;
  fabgl::Terminal::keyboardReaderTaskStackSize       = 800;
  fabgl::Keyboard::scancodeToVirtualKeyTaskStackSize = 1500;

  PS2Controller.begin(PS2Preset::KeyboardPort0);
  DisplayController.begin(HI_INTENSITY, VIDEO, GPIO_NUM_17, HSYNC, VSYNC);
  DisplayController.setResolution(MDA_640x320_50Hz);//(MDA_600x320_50Hz);
  DisplayController.moveScreen(10,5);


  DisplayController.setPaletteItem(0, RGB888(0, 0, 0));       // 0: black
  DisplayController.setPaletteItem(1, RGB888(255, 0, 0));     // 1: red
  DisplayController.setPaletteItem(2, RGB888(0, 255, 0));     // 2: green
  DisplayController.setPaletteItem(3, RGB888(255, 255, 255)); // 3: white

  //Keyboard.begin(GPIO_NUM_2, GPIO_NUM_4); // CLK, DATA for actual PS2 Keyboard

  //Terminal.begin(&DisplayController, -1,-1, &Keyboard);
  Terminal.begin(&DisplayController, 80, -1);
  Terminal.keyboard()->setLayout(&fabgl::GermanLayout);
  Terminal.connectLocally();
  //initXTKeyb(&Terminal);
  //Terminal.setLogStream(Serial);  // DEBUG ONLY

  Terminal.enableCursor(true);

  // For style
  slowPrintf("\e[97m * * U N I V E R S A L  C O M P U T E R  T E R M I N A L * *\r\n");
  slowPrintf("\e[97mBooting \e[92mUNIFOX System\e[97m V3.27.1\r\n");
  slowPrintf("\e[97m(C) 1978-1995 \e[92mUNIFOX Inc.\e[97m. All rights reserved.\r\n");
  delay(250);
  slowPrintf("\e[97mTotal RAM: \e[92m%d\e[97m bytes free\r\n", ESP.getFreeHeap());
  slowPrintf("\e[97mStarting INIT");
  for(int i=0;i<5;i++){
		delay(200);
		Terminal.write(".");
	}
  Terminal.write("done\r\n\n\n");
  slowPrintf("\e[97mBi-Processor System detected:\r\n");
  slowPrintfTime(5,"\t\e[97m- Processor 0 is Xtensa LX6 @ \e[92m160\e[97m MHz\r\n");
  slowPrintfTime(5,"\t\e[97m- Processor 1 is Xtensa LX6 @ \e[92m160\e[97m MHz\r\n");

  slowPrintf("\e[97mStarting Network Interface:\r\n");
  slowPrintf("\e[97mConnecting to 10BaseW Network \"\e[92m%s\e[97m\"", WIFI_SSID);
	WiFi.begin(WIFI_SSID, WIFI_PASS);
	while (WiFi.status() != WL_CONNECTED)
	{
		delay(500);
		Terminal.write(".");
	}
  slowPrintf("connected!\r\n");
	client.setInsecure();

  currentScript = AUTOEXEC;
}

void loop()
{
  
  switch (state) {

    case State::Prompt:
      exe_prompt();
      break;

    case State::PromptInput:
      exe_promptInput();
      break;

    case State::Info:
      exe_info();
      break;

    case State::ChatGPT:
      exe_chatgpt();
      break;

    case State::ShowPrompt:
      exe_showPrompt();
      break;
    case State::QuickInfo:
      exe_quickInfo();
      break;

    case State::Reset:
      exe_reset();
      break;

    case State::Reboot:
      ESP.restart();
      break;

    case State::UnknownCommand:
      Terminal.write("\r\nMistake\r\n");
      state = State::Prompt;
      break;

    default:
      Terminal.write("\r\nNot Implemeted\r\n");
      state = State::Prompt;
      break;

  }
}
