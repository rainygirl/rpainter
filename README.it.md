# <img src="docs/icon.png" width="32" height="32" alt=""> R Painter

Un editor di immagini a livelli. Funziona allo stesso modo sul web, su macOS, Linux e RenkuOS (HaikuOS).
L'interfaccia segue la lingua del sistema (italiano, inglese, francese, giapponese o coreano; per le altre lingue, inglese).
Non c'è uno strumento testo: non si può aggiungere testo a un'immagine.

[English](README.md) · [Français](README.fr.md) · Italiano · [日本語](README.ja.md) · [한국어](README.ko.md)

## Piattaforme supportate

| Sistema | Architettura | Come si installa |
|---|---|---|
| Web | un browser recente | Niente da installare. Si apre nel browser |
| macOS 26 o successivo | Apple Silicon (arm64) | Scarica l'app |
| Linux: Debian, Ubuntu, Linux Mint | x86_64, arm64 | Si compila con un solo comando, oppure si usano i file già compilati |
| RenkuOS (HaikuOS) | x86_64, arm64 | `pkgman`, dal repository pkgman.rainygirl.com |

## Installazione

### Web

![R Painter nel browser](docs/screenshots/web.png)

Apri https://painter.coroke.net.

### macOS

![R Painter su macOS](docs/screenshots/mac.png)

1. Scarica [`r-painter-1.0.0-mac-arm64.dmg`](dist/r-painter-1.0.0-mac-arm64.dmg).
2. Aprilo e trascina **R Painter** nella cartella **Applicazioni**.
3. Apri R Painter da Applicazioni.

L'app non è firmata con un Apple Developer ID, quindi la prima volta macOS la blocca. Basta consentirla una volta.

- Dopo l'avviso, vai in **Impostazioni di Sistema > Privacy e sicurezza**, cerca *"R Painter" è stato bloccato* e fai clic su **Apri comunque**.
- Oppure dal Terminale (risolve anche il messaggio *"è danneggiata e non può essere aperta"*):

  ```sh
  /usr/bin/xattr -dr com.apple.quarantine "/Applications/R Painter.app"
  ```

### Linux

![R Painter su Linux](docs/screenshots/linux.png)

1. Scarica questo progetto come ZIP ed estrailo, oppure prendilo con `git`.
2. Apri un terminale in quella cartella ed esegui:

   ```sh
   sudo apt install build-essential cmake qtbase5-dev qt5-image-formats-plugins
   ./linux/build.sh
   sudo cmake --install linux/build
   ```

   Sulle distribuzioni che hanno solo Qt 6, installa `qt6-base-dev qt6-image-formats-plugins` al posto di `qtbase5-dev qt5-image-formats-plugins`.
3. Apri **R Painter** dal menu delle applicazioni, oppure esegui `RPainter` in un terminale.

Ci sono anche file già compilati per Ubuntu 24.04:
[x86_64](dist/r-painter-1.0.0-linux-x86_64.tar.gz), [arm64](dist/r-painter-1.0.0-linux-aarch64.tar.gz).
Richiedono il runtime Qt 6 (`libqt6widgets6`).

### RenkuOS (HaikuOS)

![R Painter su RenkuOS (HaikuOS)](docs/screenshots/haiku.png)

1. Apri Terminal ed esegui:

   ```sh
   pkgman add-repo https://pkgman.rainygirl.com/$(getarch -p)
   pkgman install rpainter
   ```

   I pacchetti sono pubblicati per x86_64 e arm64; `$(getarch -p)` sceglie quello giusto. RenkuOS (HaikuOS) a 32 bit (x86, x86_gcc2) non è supportato. Il repository va aggiunto una volta sola.
2. Apri **R Painter** dal menu **Applications** di Deskbar.

Per compilarlo da te:

```sh
pkgman install gcc binutils make cmake haiku_devel
./haiku/build.sh
```

## Licenza

MIT. Vedi [LICENSE](LICENSE). La versione per RenkuOS (HaikuOS) include libwebp (BSD) e stb (pubblico dominio); le licenze sono in `haiku/third_party`.

## Nota sull'uso dell'IA

Nello sviluppo di questo programma è stato utilizzato Claude.
