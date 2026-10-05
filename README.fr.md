# <img src="docs/icon.png" width="32" height="32" alt=""> R Painter

Un éditeur d'images à calques. Il fonctionne de la même façon sur le web, macOS, Linux et RenkuOS (HaikuOS).
L'interface suit la langue du système (français, anglais, italien, japonais ou coréen ; anglais pour les autres langues).
Il n'y a pas d'outil texte : on ne peut pas ajouter de texte à une image.

[English](README.md) · Français · [Italiano](README.it.md) · [日本語](README.ja.md) · [한국어](README.ko.md)

## Plateformes prises en charge

| Système | Architecture | Comment l'installer |
|---|---|---|
| Web | un navigateur récent | Rien à installer. Il s'ouvre dans le navigateur |
| macOS 26 ou ultérieur | Apple Silicon (arm64) | Téléchargez l'application |
| Linux : Debian, Ubuntu, Linux Mint | x86_64, arm64 | Se compile en une commande, ou utilisez les fichiers déjà compilés |
| RenkuOS (HaikuOS) | x86_64, arm64 | `pkgman`, depuis le dépôt pkgman.rainygirl.com |

## Installation

### Web

![R Painter dans un navigateur](docs/screenshots/web.png)

Ouvrez https://painter.coroke.net.

### macOS

![R Painter sous macOS](docs/screenshots/mac.png)

1. Téléchargez [`r-painter-1.0.0-mac-arm64.dmg`](dist/r-painter-1.0.0-mac-arm64.dmg).
2. Ouvrez-le et faites glisser **R Painter** dans le dossier **Applications**.
3. Ouvrez R Painter depuis Applications.

L'application n'est pas signée avec un Apple Developer ID, donc macOS la bloque la première fois. Il suffit de l'autoriser une fois.

- Après l'avertissement, allez dans **Réglages Système > Confidentialité et sécurité**, repérez *« R Painter » a été bloqué* et cliquez sur **Ouvrir quand même**.
- Ou dans le Terminal (cela règle aussi le message *« est endommagé et ne peut pas être ouvert »*) :

  ```sh
  /usr/bin/xattr -dr com.apple.quarantine "/Applications/R Painter.app"
  ```

### Linux

![R Painter sous Linux](docs/screenshots/linux.png)

1. Téléchargez ce projet en ZIP et décompressez-le, ou récupérez-le avec `git`.
2. Ouvrez un terminal dans ce dossier et lancez :

   ```sh
   sudo apt install build-essential cmake qtbase5-dev qt5-image-formats-plugins
   ./linux/build.sh
   sudo cmake --install linux/build
   ```

   Sur une distribution qui n'a que Qt 6, installez `qt6-base-dev qt6-image-formats-plugins` à la place de `qtbase5-dev qt5-image-formats-plugins`.
3. Ouvrez **R Painter** depuis le menu des applications, ou lancez `RPainter` dans un terminal.

Il existe aussi des fichiers déjà compilés pour Ubuntu 24.04 :
[x86_64](dist/r-painter-1.0.0-linux-x86_64.tar.gz), [arm64](dist/r-painter-1.0.0-linux-aarch64.tar.gz).
Ils nécessitent le runtime Qt 6 (`libqt6widgets6`).

### RenkuOS (HaikuOS)

![R Painter sous RenkuOS (HaikuOS)](docs/screenshots/haiku.png)

1. Ouvrez Terminal et lancez :

   ```sh
   pkgman add-repo https://pkgman.rainygirl.com/$(getarch -p)
   pkgman install rpainter
   ```

   Les paquets sont publiés pour x86_64 et arm64 ; `$(getarch -p)` choisit le bon. RenkuOS (HaikuOS) 32 bits (x86, x86_gcc2) n'est pas pris en charge. Le dépôt ne s'ajoute qu'une seule fois.
2. Ouvrez **R Painter** depuis le menu **Applications** de la Deskbar.

Pour le compiler vous-même :

```sh
pkgman install gcc binutils make cmake haiku_devel
./renku/build.sh
```

## Licence

MIT. Voir [LICENSE](LICENSE). La version RenkuOS (HaikuOS) embarque libwebp (BSD) et stb (domaine public) ; leurs licences sont dans `renku/third_party`.

## Utilisation de l'IA

Claude a été utilisé dans le développement de ce programme.
