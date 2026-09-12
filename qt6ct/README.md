# Qt6ct Terminal Transparent Theme

Custom Qt6 theme files for `qt6ct`. This folder is intended to be symlinked to `~/.config/qt6ct`.

## Files

- `terminal-transparent.qss`: transparent graphite terminal-style Qt stylesheet.
- `colors/terminal-transparent.conf`: matching Qt palette.
- `qt6ct.conf`: sample `qt6ct` config using FiraCode Nerd Font and the stylesheet above.

## Enable

Install `qt6ct` if needed, then point Qt apps at it:

```sh
export QT_QPA_PLATFORMTHEME=qt6ct
```

This repo folder is currently intended to be linked as:

```sh
ln -s ~/Projects/Dotfiles/qt6ct ~/.config/qt6ct
```

Transparency depends on the Qt app and compositor support. Apps that force their own theme or opaque windows may only use the colors and font.
