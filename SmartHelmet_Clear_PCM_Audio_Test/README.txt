SMART HELMET - CLEAR PCM AUDIO TEST

Use:
1. Keep MAX98357A wiring exactly:
   WS/LRC  -> XIAO ESP32-C6 D8 / GPIO19
   BCLK    -> D9 / GPIO20
   DIN     -> D10 / GPIO18
   GND     -> GND
   Speaker -> MAX98357A speaker output

2. Open SmartHelmet_PCM_Clear_Test.ino in Arduino IDE.
3. Keep audio_data_pcm.h in the same folder.
4. Select your XIAO ESP32-C6 board.
5. Upload.
6. Open Serial Monitor at 115200.
7. Press:
   1 = A01
   2 = A02
   4 = A04
   5 = A05
   6 = A06
   A = all

Audio source:
48 kHz, 16-bit PCM, mono. No ADPCM compression and no LittleFS.
