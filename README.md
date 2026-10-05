# <img src="docs/icon.png" width="32" height="32" alt=""> R Painter

A layer-based image editor. It works the same on the web, macOS, Linux and RenkuOS (HaikuOS).
The interface follows your system language (English, French, Italian, Japanese or Korean; anything else gets English).
There is no text tool: you cannot add text to an image.

English · [Français](README.fr.md) · [Italiano](README.it.md) · [日本語](README.ja.md) · [한국어](README.ko.md)

## Supported platforms

| System | Architecture | How to install |
|---|---|---|
| Web | any current browser | Nothing to install. Open it in a browser |
| macOS 26 or later | Apple Silicon (arm64) | Download the app |
| Linux: Debian, Ubuntu, Linux Mint | x86_64, arm64 | Build it with one command, or use the prebuilt files |
| RenkuOS (HaikuOS) | x86_64, arm64, 32-bit x86 (x86_gcc2) | `pkgman`, from pkgman.rainygirl.com |

## Install

### Web

![R Painter running in a browser](docs/screenshots/web.png)

Open https://painter.coroke.net.

### macOS

![R Painter running on macOS](docs/screenshots/mac.png)

1. Download [`r-painter-1.0.0-mac-arm64.dmg`](dist/r-painter-1.0.0-mac-arm64.dmg).
2. Open it and drag **R Painter** to the **Applications** folder.
3. Open R Painter from Applications.

The app is not signed with an Apple Developer ID, so macOS blocks it the first time. You only have to allow it once.

- After the warning, go to **System Settings > Privacy & Security**, find *"R Painter" was blocked* and click **Open Anyway**.
- Or in Terminal (this also fixes *"is damaged and can't be opened"*):

  ```sh
  /usr/bin/xattr -dr com.apple.quarantine "/Applications/R Painter.app"
  ```

### Linux

![R Painter running on Linux](docs/screenshots/linux.png)

1. Download this project as a ZIP and unpack it, or get it with `git`.
2. Open a terminal in that folder and run:

   ```sh
   sudo apt install build-essential cmake qtbase5-dev qt5-image-formats-plugins
   ./linux/build.sh
   sudo cmake --install linux/build
   ```

   On a distribution that only has Qt 6, install `qt6-base-dev qt6-image-formats-plugins` instead of `qtbase5-dev qt5-image-formats-plugins`.
3. Open **R Painter** from the application menu, or run `RPainter` in a terminal.

There are also prebuilt files for Ubuntu 24.04:
[x86_64](dist/r-painter-1.0.0-linux-x86_64.tar.gz), [arm64](dist/r-painter-1.0.0-linux-aarch64.tar.gz).
They need the Qt 6 runtime (`libqt6widgets6`).

### RenkuOS (HaikuOS)

![R Painter running on RenkuOS (HaikuOS)](docs/screenshots/haiku.png)

1. Open Terminal and run:

   ```sh
   pkgman add-repo https://pkgman.rainygirl.com/$(getarch -p)
   pkgman install rpainter
   ```

   Packages are published for x86_64, arm64 and 32-bit x86 (x86_gcc2); `$(getarch -p)` picks the right repository. On the 32-bit image the package is named `rpainter_x86`, so run `pkgman install rpainter_x86` there. The repository only has to be added once.
2. Open **R Painter** from the **Applications** menu in Deskbar.

To build it yourself:

```sh
pkgman install gcc binutils make cmake haiku_devel
./renku/build.sh
```

## License

MIT. See [LICENSE](LICENSE). The RenkuOS (HaikuOS) build bundles libwebp (BSD) and stb (public domain); their licenses are in `renku/third_party`.

## AI disclosure

Claude was used in the development of this program.
