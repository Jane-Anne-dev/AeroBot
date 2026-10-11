# Starbie
<img width="957" height="485" alt="Screenshot 2026-10-10 114730" src="https://github.com/user-attachments/assets/90e21e72-1c1e-4491-bcd4-88fe2456b28c" />
<img width="727" height="367" alt="Screenshot 2026-10-10 114755" src="https://github.com/user-attachments/assets/2f64a2e5-e811-4bee-96f8-1296dc6400ed" />

Aerobot is a personal desk buddy. It can be any shape, the goal is to be creative. I t has a motion sensor which allows aerobot  to know if tis being moved around.
shake it and its face moves!
## What it is
It sits on your desk as a companion. You can assign different functions with the buttons and you can play with it if you're bored.

It is inspired by the starbie, here is the repository: https://github.com/SharKingStudios/Starbie/blob/main/README.md?plain=1
it is equiped with a guide which i followed.

The board includes an oled display, microcontroller and motion sensor

The firmware is in the main.cpp file

The aerobot has 21 expressions:
 Mood button cycle (starts in AUTO at boot, never returns to it):
//     Tap  1 Happy         Tap  2 Wink          Tap  3 Love
//     Tap  4 Sleepy        Tap  5 Cry           Tap  6 Distressed
//     Tap  7 Glare         Tap  8 Mad           Tap  9 Glasses
//     Tap 10 Sleep         Tap 11 Blink upper   Tap 12 Tired
//     Tap 13 Night         Tap 14 Upper lids    Tap 15 Lower lids
//     Tap 16 Blank         Tap 17 "HELLO"       Tap 18 "BYE"
//     Tap 19 "What?"       Tap 20 Scuba Cat (sways side to side)
//     Tap 21 = back to Happy ... and so on forever

It also signals water me when it senses dry soil
The 3rd button turns the screen off
The 4th button recaliberates the mpu6050