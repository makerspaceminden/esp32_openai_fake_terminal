# "Fake" Linux Terminal session using chatGPT, an ESP32 and an old monitor and keyboard

## Configuration

Copy the `/include/config.dist.h` to `/include/config.h` and fill it out with your data.

Open the `/platformio.ini` and fill it out with your data.

The program currently generates a MDA video signal, with "VIDEO" on the Green color channel and "Intensity" on the Red color channel of the VGA library.
Pinouts and such are _mostly_ documented in main.cpp

## Resources

- [FabGL](https://github.com/fdivitto/FabGL) For the VGA and PS2 interaction code
- [C64-Esp8266-OpenAI](https://github.com/makerspaceminden/C64-Esp8266-OpenAI) for the api interaction code
- [Awesome ChatGPT Prompts](https://github.com/f/awesome-chatgpt-prompts)
- [OpenAI API Reference](https://platform.openai.com/docs/api-reference/chat/create)
