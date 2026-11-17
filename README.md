# XIII: Mongoose Fix
<img width="1280" alt="XIII Mongoose Fix" src="https://github.com/user-attachments/assets/7ac9050d-7654-48ea-9f21-1a66f37d4412" />

## Installation
The latest version of [XIII: Mongoose Fix](https://github.com/TGP482/XIII-Mongoose-Fix/releases) can be found in the Releases page.

### Game Setup
- After downloading XIII: Mongoose Fix, extract the contents to your XIII directory and overwrite all existing files when prompted.
- You can adjust the mod settings inside the in-game options menu whilst the game is running or alternatively inside `XIIIMongooseFix.ini` located in the `system\Plugins` folder.
<img width="3840" height="2160" alt="203196~1" src="https://github.com/user-attachments/assets/612ab810-cfb0-4f6a-91a7-d2f814a2bb96" />


> [!NOTE]
> Linux / Steam Deck will need the following command to launch the game:
```
WINEDLLOVERRIDES="dinput8=n,b" %command%
```

## Features

### Display
- **Display Mode** - Allows you to change between Fullscreen, Borderless and Windowed modes.
- **Window Resolution** - Sets the game's window resolution, with 0 using your desktop resolution.
- **Internal Resolution** - Added the option to change the internal resolution to a custom value, to allow for supersampling and downscaling.
- **Scaling Filter** - Sets the internal resolution scaling filter.
- **V-Sync** - Restores V-Sync in windowed and borderless, which the game only ever applied in fullscreen.
- **Max Frame Rate** - Caps the game's frame rate, either at your monitor's refresh rate automatically or at a value you pick, bypassing the game's 60FPS lock.

### Graphics
- **DirectX Version** - Runs the game's renderer through DirectX 9 via d3d8to9 or the stock DirectX 8 path which the game ships with.
- **Anisotropic Filtering** - Forces the selected anisotropic filtering level on all textures instead of using the game's defaults.
- **MSAA** - Enables multisample antialiasing at 2, 4 or 8 samples.
- **CRT Gamma** - Restores the vibrance and contrast a CRT gave the game's colours. (0) off, (1) on. DirectX 9 only.
- **Shader** - Optional post-process look over the finished frame: Sharpen, Vivid, Comic Ink, Bloom, Cinematic, Noir or Remaster. Switches live. DirectX 9 only.
- **Texture Pack** - Uses the packages in a `TexturesAI` folder next to `TexturesPC` (e.g. an upscaled texture pack) instead of the game's own, when that folder exists. The originals stay untouched. Applies at the next launch.
- **HD Textures** - With Texture Pack on, textures found in `TexturesAI\HD` (named by a hash of the original texture's content) are swapped in at draw time. The game keeps the original size and texture coordinates, so HUD icons, fonts, sprites and decals get sharper without changing their size or position. DirectX 9 only.
- **Cutscenes** - Plays the movies in a `VideoAI` folder next to `Video` (e.g. upscaled cutscenes) when they exist, per file, falling back to the originals. Movies of any resolution play; the stock player was limited to 1024x512. Switches live.

### Gameplay
- **Mouse Look Sensitivity** - Adjusts mouse look sensitivity.
- **Mouse Smoothing** - Toggles mouse smoothing on and off.

### Field of View
- **Field of View** - Adjusts the base gameplay field of view.
- **HUD Scaling** - (1) scales the HUD and comic panels to the screen like the original 640x480 game, (0) keeps the stock pixel size.

### Controller
- **Restored Controller Support** - Full controller support has been restored.
- **Controller Look Sensitivity** - Adjusts controller look sensitivity.
- **Vibration** - Restores controller vibration support.
- **Layout** - Sets the starting pad layout out of the four the Xbox version shipped with.

### General
- **Skip Intro** - Skips the Ubisoft, alien and nvidia logo movies the exe plays before the menu.
- **Allow Cheats** - Allows cheats to be used through the console.

### Fixes
- Fixed mouse movement breaking with high polling rates and removed the deadzone.
- Fixed an issue where mouse sensitivity changed with the field of view.
- Fixed HUD elements, menus, blur effects and comic panels not scaling properly with resolution.
<div align="center">
  <table>
    <tr>
      <td width="50%"><img style="width:100%" src="https://github.com/user-attachments/assets/4e8984c0-b385-4b58-8639-b6228873c292""></td>
      <td width="50%"><img style="width:100%" src="https://github.com/user-attachments/assets/96e14acc-ce94-448b-a049-a2e92488b3ff"></td>
    </tr>
    <tr>
      <td align="center">Before</td>
      <td align="center">After</td>
    </tr>
  </table>
</div>

- Fixed the cutscene camera never turning onto its focus target above 162 fps, leaving the shot pointed at whatever angle it started on.
- Fixed getting stuck when squeezing through narrow gaps at high frame rates.
- Fixed an issue with FMVs playing silently.
- Fixed DirectX 9 failing with "Failed to create vertex shader" (error X5430 on `min oFog`) under Proton/DXVK and on some AMD drivers: the d3d8to9 shader translator dropped the `.x` swizzle on fog sources.
- Fixed low frame rates on DirectX 9 (and DX8 under Wine): the engine locked the back buffer for reading every frame without using the result, which forced a full GPU readback.
- Fixed the sixth sense "Tap Tap Tap" markers being drawn off screen with HUD scaling on.

### Credits
- [PowerAtlas](https://github.com/PowerAtlas) - Proton/DX9 fixes, Shader, Texture Pack, HD Textures, Cutscenes, HUD Scaling toggle; developed with Claude Fable 5.2 and Claude Opus 5.5 (Anthropic).
- [ThirteenAG](https://github.com/ThirteenAG) - [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader).
- [crosire](https://github.com/crosire) - [d3d8to9](https://github.com/crosire/d3d8to9).
- [CeeJayDK](https://github.com/CeeJayDK) - [SweetFX](https://github.com/CeeJayDK/SweetFX), CRT Gamma.
