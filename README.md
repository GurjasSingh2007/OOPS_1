# Stellar App (C++ port of Pro79)

Console C++17 port of the React Native app. No libraries to install.

## Run in VS Code
1. Install a C++ compiler (g++ / MinGW on Windows, Xcode tools on Mac, `build-essential` on Linux) and the **C/C++** extension.
2. File > Open Folder... and pick this folder.
3. Press **Ctrl+Shift+B** -> builds and runs the app in the terminal panel.
   (Or build with `g++ -std=c++17 src/main.cpp -o stellar` and run `./stellar` / `.\stellar.exe`.)
   Press **F5** to run under the debugger.

## Controls
Type the number/letter shown and press Enter. `b` = back, `q` = quit.

## Features
- **Star Map**: enter longitude and latitude, then `o` opens the live sky map in your browser (the original used an in-app WebView).
- **Daily Pictures**: fetches NASA's Astronomy Picture of the Day (needs internet + `curl`, which is built into Windows 10+, macOS and most Linux). `o` opens the picture/video.
- **Spacecrafts**: placeholder, same as the original.

## NASA API key
The key in the original repo was public on GitHub. Get your own free key at https://api.nasa.gov and replace `NASA_API_KEY` near the top of `src/main.cpp`.
