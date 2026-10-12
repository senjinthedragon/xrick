Optional desktop integration for the Linux build
==================================================

A raw Linux binary can't carry an embedded icon the way Windows .exe files or
macOS .app bundles can -- this is how Linux shows one anyway, via the
freedesktop.org menu system.

1. Edit xrick.desktop: replace both /path/to/ placeholders with the actual
   full paths to the xrick binary and to xrickST.png, wherever you extracted
   this release.
2. Copy xrick.desktop to ~/.local/share/applications/ (or
   /usr/share/applications/ for all users).
3. Copy xrickST.png to ~/.local/share/icons/hicolor/32x32/apps/ (or
   /usr/share/icons/hicolor/32x32/apps/ for all users), then run
   `gtk-update-icon-cache ~/.local/share/icons/hicolor` (or the system
   equivalent) if the icon doesn't show up right away.

xrick should then appear in your desktop's application menu/launcher with
its icon, like any installed app.
