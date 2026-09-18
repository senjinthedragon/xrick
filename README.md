# xrick
Remember Rick Dangerous?

Way before Lara Croft, back in the 1980's and early 1990's, Rick Dangerous was the Indiana Jones of computer games,
running away from rolling rocks, avoiding traps, from South America to a futuristic missile base via Egypt and the
Schwarzendumpf castle.

**xrick** is a clone of Rick Dangerous, produced by carefully reverse-engineering the PC and Atari versions of the
game, and re-coding in C. It has been ported to Windows, Linux, but also BeOs, Amiga, QNX, and all sorts
of gaming console.

You can read more about Rick Dangerous straight from his creator, [Simon Phipps](https://www.simonphipps.com/games/rickdangerous/),
and more about xrick at the original [xrick page](http://www.bigorno.net/xrick). The code for xrick was only available 
as Zip files on that page: the goal of this repository is to release it in a more convenient way.

So far, it contains:
* The "Dec 12th, 2002" release (#021212) which is the last version I published in 2002
* The "May, 2005" release (#050500) which was never released, and is a bit cleaner (?)
* Ported from SDL to SDL2
* With adjustments so it can build with [emscripten](https://emscripten.org/)

This is all work-in-progress and will be updated.

## About this fork

This fork takes the #050500 codebase and ports it from SDL2 to **SDL3**, replacing the old
`SDL_Renderer` backend with a custom `SDL_GPU` pipeline. Emscripten support has been removed -
this fork targets a native, self-contained desktop build only.

What's new here:
* **SDL3 + SDL_GPU renderer**, laying the groundwork for real-time upscale/CRT shaders.
* **Upscaling**: AMD FSR1 (EASU + RCAS).
* **CRT emulation**: crt-easymode, and a condensed multi-pass port of crt-royale (scanlines,
  phosphor mask, bloom, spherical curvature), both ported from the real RetroArch `slang-shaders`.
* **Monitor bezel** compositing (Commodore 1084S), with screen curvature tied to it.
* **Self-contained binary**: game data, shaders, and bezel/mask art are embedded directly into
  the executable (via C23 `#embed`) - no loose data files needed at runtime.
* **Audio** converted from 8-bit PCM WAV to Ogg Vorbis, decoded via `libvorbisfile`.
* A number of real, pre-existing bugs found and fixed along the way (see commit history for
  specifics - none were introduced by the port itself).

### Building

```sh
make install
```

Produces `build/xrick`. Requires SDL3 and libvorbisfile.

### Runtime options

```
-fullscreen           start in fullscreen
-zoom <n>             window scale factor
-upscale <none|fsr1>
-crt <none|easymode|royale>
-bezel <none|1084s>
-royale-mask <slot|grille|shadow>
-controls <classic|modern>
-vol <n> / -nosound
-speed <n>            game speed
```

Upscale/CRT/bezel modes can also be cycled live with **F10**/**F11**/**F12**. F1 toggles
fullscreen, F2/F3 zoom, F4-F6 sound mute/volume, F7-F9 cheats.

`-controls classic` (the default) is the original game: hold **fire** and press **up**/**down**
to shoot/drop a bomb. `-controls modern` adds dedicated shoot/bomb keys (**A**/**S** by default)
on top of that, without removing the classic combo. Either way, you need to be standing still
to shoot or drop a bomb - moving or crouching first, same as the original game.

### Credits

Beyond bigorno's original xrick decompilation, this fork's shader/bezel pipeline builds on work
by others:
* **crt-royale** CRT shader by [TroggleMonkey](https://github.com/libretro/slang-shaders/blob/master/crt/shaders/crt-royale/README.TXT) (GPL), via [libretro/slang-shaders](https://github.com/libretro/slang-shaders).
* **crt-easymode** CRT shader by EasyMode (GPL), via [libretro/slang-shaders](https://github.com/libretro/slang-shaders).
* **FSR1** (FidelityFX Super Resolution 1.0) by AMD (MIT), via the RetroArch/libretro slang port (Unlicense).
* **Commodore 1084S bezel** image by [Duimon](https://github.com/Duimon/Duimon-Mega-Bezel) (CC BY-NC-ND 4.0).
* **stb_image.h** by [Sean Barrett](https://github.com/nothings/stb) (public domain).
* **unzip.c** (minizip) by Gilles Vollant (zlib-style license).
