# <img src="docs/icon.png" width="32" height="32" alt=""> R Painter

레이어 기반 이미지 편집기입니다. 웹, macOS, Linux, RenkuOS (HaikuOS)에서 같은 기능으로 동작합니다.
화면 언어는 시스템 언어를 따릅니다 (한국어, 영어, 일본어, 이탈리아어, 프랑스어. 그 외에는 영어).
글자 추가 기능은 없습니다.

[English](README.md) · [Français](README.fr.md) · [Italiano](README.it.md) · [日本語](README.ja.md) · 한국어

## 지원 플랫폼

| 환경 | 아키텍처 | 설치 방법 |
|---|---|---|
| 웹 | 최신 브라우저 | 설치 없음. 브라우저에서 엽니다 |
| macOS 26 이상 | Apple Silicon (arm64) | 앱을 내려받습니다 |
| Linux: Debian, Ubuntu, Linux Mint | x86_64, arm64 | 명령 한 번으로 빌드하거나 미리 빌드한 파일을 씁니다 |
| RenkuOS (HaikuOS) | x86_64, arm64, 32비트 x86 (x86_gcc2) | pkgman.rainygirl.com 저장소에서 `pkgman`으로 설치합니다 |

## 설치

### 웹

![웹에서 실행한 R Painter](docs/screenshots/web.png)

https://painter.coroke.net 을 엽니다.

### macOS

![macOS에서 실행한 R Painter](docs/screenshots/mac.png)

1. [`r-painter-1.0.0-mac-arm64.dmg`](dist/r-painter-1.0.0-mac-arm64.dmg)를 내려받습니다.
2. 열어서 **R Painter**를 **응용 프로그램** 폴더로 끌어다 놓습니다.
3. 응용 프로그램에서 R Painter를 엽니다.

Apple Developer ID로 서명한 앱이 아니어서 처음 열 때 macOS가 막습니다. 한 번만 허용하면 됩니다.

- 경고가 뜬 뒤 **시스템 설정 > 개인정보 보호 및 보안**에서 *"R Painter"이(가) 차단됨* 항목의 **그래도 열기**를 누릅니다.
- 또는 터미널에서 (*"손상되었기 때문에 열 수 없습니다"* 가 나올 때도 이 방법을 씁니다):

  ```sh
  /usr/bin/xattr -dr com.apple.quarantine "/Applications/R Painter.app"
  ```

### Linux

![Linux에서 실행한 R Painter](docs/screenshots/linux.png)

1. 이 프로젝트를 ZIP으로 내려받아 풀거나 `git`으로 받습니다.
2. 받은 폴더에서 터미널을 열고 실행합니다.

   ```sh
   sudo apt install build-essential cmake qtbase5-dev qt5-image-formats-plugins
   ./linux/build.sh
   sudo cmake --install linux/build
   ```

   Qt 6만 있는 배포판에서는 `qtbase5-dev qt5-image-formats-plugins` 대신 `qt6-base-dev qt6-image-formats-plugins`를 설치합니다.
3. 프로그램 메뉴에서 **R Painter**를 열거나 터미널에서 `RPainter`를 실행합니다.

Ubuntu 24.04용으로 미리 빌드한 파일도 있습니다:
[x86_64](dist/r-painter-1.0.0-linux-x86_64.tar.gz), [arm64](dist/r-painter-1.0.0-linux-aarch64.tar.gz).
실행하려면 Qt 6 런타임(`libqt6widgets6`)이 필요합니다.

### RenkuOS (HaikuOS)

![RenkuOS (HaikuOS)에서 실행한 R Painter](docs/screenshots/haiku.png)

1. 터미널을 열고 실행합니다.

   ```sh
   pkgman add-repo https://pkgman.rainygirl.com/$(getarch -p)
   pkgman install rpainter
   ```

   패키지는 x86_64, arm64, 32비트 x86(x86_gcc2)용으로 배포하며, `$(getarch -p)`가 맞는 저장소를 고릅니다. 32비트 이미지에서는 패키지 이름이 `rpainter_x86`이므로 `pkgman install rpainter_x86`으로 설치합니다. 저장소 추가는 한 번만 하면 됩니다.
2. Deskbar의 **Applications** 메뉴에서 **R Painter**를 엽니다.

직접 빌드하려면:

```sh
pkgman install gcc binutils make cmake haiku_devel
./renku/build.sh
```

## 라이선스

MIT. [LICENSE](LICENSE)를 참고하세요. RenkuOS (HaikuOS) 빌드에는 libwebp(BSD)와 stb(퍼블릭 도메인)가 들어 있으며, 각 라이선스는 `renku/third_party`에 있습니다.

## AI 사용 고지

이 프로그램 개발에는 Claude가 활용되었습니다.
