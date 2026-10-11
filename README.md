# Remember Rick Dangerous?

Way before Lara Croft, back in the 1980's and early 1990's, Rick Dangerous was the Indiana Jones of computer games, running away from rolling rocks, avoiding traps, from South America to a futuristic missile base via Egypt and the Schwarzendumpf castle.

**xrick** is an exact clone of both games in the series:

* Rick Dangerous, produced by carefully cracking and then reverse-engineering the original IBM PC (x86 CPU) and Atari ST (68k CPU) versions of the game, and then porting to C ;
* Rick Dangerous II, produced by having AI reverse-engineer the Atari ST (68k CPU) version of the game, and then porting to C, with manual control in the way.

Both games run on a real emulated Atari ST 68000 + YM2149, playing back the original sound-driver code directly for music and effects -- not a reimplementation.

**This repository contains the source code for xrick.**

## This Fork

This is [Senjin the Dragon](https://github.com/senjinthedragon)'s fork, built on top of [zpqrtbnk/xrick](https://github.com/zpqrtbnk/xrick)'s decompilation, RD2 port, real audio engine, AI solver / attract-mode demos, and web player. On top of that foundation, this fork adds:

* an SDL_GPU renderer with CRT shaders (EasyMode, Lottes, Royale), FSR1 upscaling, and CRT-monitor bezels ;
* a settings menu covering video, audio, controls and per-game cheats, with full gamepad remapping ;
* a diskette carousel for picking between the two games in place of a splash screen, with each game's own real title music, a floor reflection, and a death-scream sting on confirm ;
* modern, optional control schemes (dedicated shoot/bomb keys) alongside the original layouts.

## See Also

* The project's home page at [rick-dangerous.org](https://www.rick-dangerous.org/) ;
* Play online at [rick-dangerous.org/player](https://www.rick-dangerous.org/player) ;
* The complete Rick Dangerous code knowledge base in its [own repository](https://github.com/zpqrtbnk/xrick-kb) ;
* This fork, at [senjinthedragon/xrick](https://github.com/senjinthedragon/xrick).

## Credits

* **Rick Dangerous I & II**, originally created by Core Design, published by Firebird ;
* reverse-engineered and ported by Stephane Gay (bigorno / ZpqrtBnk) ;
* this fork -- SDL_GPU renderer, settings menu, gamepad remapping, game-select carousel -- by Senjin the Dragon ;
* Atari audio emulation by Arnaud Carre (Leonard / Oxygene) ;
* 68000 CPU emulation: Musashi, by Karl Stenerud ;
* CRT shaders: Timothy Lottes, EasyMode, TroggleMonkey, Themaister ;
* upscaling: AMD FidelityFX (FSR1) ;
* bezels: Duimon, the Mega Bezel project ;
* diskette art: sheikh_tuhin, Openclipart (public domain) ;
* SDL3 ; Emscripten.

## License

**xrick**'s own code is published under the MIT License. As for the artwork, and the gameplay itself, the situation is murky. The original author has discussed it with Simon Phipps in the past, and his Rick Dangerous page now has a paragraph about "who owns the rights to Rick now" and the answer is... it is hard to tell.

The bundled third-party components listed under Credits above -- the audio and CPU emulation, the CRT shaders, the upscaler, the bezels, and the diskette art -- are each under their own license from their respective projects ; see those projects for full terms.
