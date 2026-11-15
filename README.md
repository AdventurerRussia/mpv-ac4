# mpv AC-4 Edition (Windows x64)

[![Release](https://img.shields.io/github/v/release/AdventurerRussia/mpv-ac4?include_prereleases&color=blue&style=flat-square)](https://github.com/AdventurerRussia/mpv-ac4/releases/latest)
[![Platform](https://img.shields.io/badge/platform-Windows%20x64-informational?style=flat-square)](https://github.com/AdventurerRussia/mpv-ac4/releases/latest)
[![Codec](https://img.shields.io/badge/audio-Dolby%20AC--4%20(ETSI%20TS%20103%20190)-success?style=flat-square)](https://www.etsi.org/deliver/etsi_ts/103100_103199/10319001/01.03.01_60/ts_10319001v010301p.pdf)
[![License](https://img.shields.io/badge/license-GPLv3-green?style=flat-square)](https://www.gnu.org/licenses/gpl-3.0.html)

Специальная сборка медиаплеера **[mpv](https://github.com/mpv-player/mpv)** с нативной поддержкой декодирования аудиопотоков **Dolby AC-4** ([ETSI TS 103 190-1](https://www.etsi.org/deliver/etsi_ts/103100_103199/10319001/01.03.01_60/ts_10319001v010301p.pdf)), включая эфирные ТВ-записи **ATSC 3.0 / DVB-T2**.

---

## ⚡ О проекте

В официальном [FFmpeg](https://github.com/FFmpeg/FFmpeg) и стандартных сборках mpv (включая популярные сборки от [shinchiro](https://github.com/shinchiro/mpv-winbuild-cmake) и [zhongfly](https://github.com/zhongfly/mpv-winbuild)) декодер AC-4 отсутствует — в коде зарегистрирован лишь демультиплексор элементарного потока. При попытке воспроизвести файл с дорожкой AC-4 плеер не может декодировать звук.

В данном проекте нативный декодер AC-4, написанный разработчиком **[Paul B Mahol (richardpl)](https://github.com/richardpl)** в рамках форка **[librempeg](https://github.com/librempeg/librempeg)**, был полностью портирован и адаптирован для актуальной стабильной ветки **[FFmpeg 9.0.2 ("Lei")](https://github.com/FFmpeg/FFmpeg/tree/release/9.0)** (а также доступен патч для **[FFmpeg 7.1](https://github.com/FFmpeg/FFmpeg/tree/release/7.1)**) с сохранением подсистемы передискретизации `libswresample`. Плеер mpv скомпилирован с этим кастомным FFmpeg 9.0.2 (`libavcodec 63`) и декодирует аудиодорожки AC-4 напрямую без сторонних фильтров.

---

## 📥 Скачать готовую сборку (Portable)

Готовая портативная сборка для Windows x64 доступна во вкладке **Releases**:
* **[Скачать MPV-AC4-Windows-x64.zip](https://github.com/AdventurerRussia/mpv-ac4/releases/latest)**

### Структура каталога (схема [shinchiro](https://github.com/shinchiro/mpv-winbuild-cmake) / [rossy](https://github.com/rossy/mpv-install)):
```text
MPV PLAYER AC-4/
├── fonts/               # Пользовательские шрифты интерфейса и субтитров
├── installer/           # Скрипты интеграции в Windows (rossy/mpv-install)
│   ├── mpv-install.bat  # Регистрация плеера в системе и ассоциация файлов (ПКМ -> Запуск от имени администратора)
│   └── mpv-uninstall.bat# Удаление ассоциаций и записей реестра
├── mpv/                 # Каталог настроек:
│   ├── fonts.conf       # Конфигурация подсистемы шрифтов
│   ├── input.conf       # Горячие клавиши
│   └── mpv.conf         # Настройки видео, аудио и аппаратного ускорения
├── script-opts/         # Конфигурации скриптов
├── scripts/             # Lua / JS скрипты
├── mpv-document.ico     # Иконка ассоциированных видеофайлов
├── mpv-icon.ico         # Иконка плеера
├── mpv.com              # Консольная версия с выводом логов
└── mpv.exe              # Основной исполняемый файл
```

---

## 🎯 Поддерживаемые возможности

| Формат / Функция | Статус | Примечания |
|---|:---:|---|
| **AC-4 Stereo (2.0)** | ✅ Проверено | 48000 Hz, чистый звук без рассинхрона (`believer.mp4`, `Lady Gaga Bloody Mary`) |
| **AC-4 Multichannel (5.1)** | ✅ Проверено | 6 каналов, проверено на реальном ТВ-эфире ATSC 3.0 (`KDAF.ts`) |
| **Контейнеры MP4 / MOV** | ✅ Поддерживается | Стандартные медиафайлы с дорожками AC-4 |
| **Контейнеры MPEG-TS** | ✅ Поддерживается | Эфирные записи цифрового телевидения ATSC 3.0 и DVB-T2 |
| **Мультиязычные дорожки** | ✅ Поддерживается | Переключение дорожек и презентаций через клавишу `#` (`--aid=1`, `--aid=2`) |
| **Частота 46034 Гц (NTSC pull-down)** | ✅ Поддерживается | Специфичный для 59.94 fps ATSC 3.0 контента клок таймингов (`ratio 25025:24000`) |
| **AC-4 Atmos (Part 2 / A-JOC)** | ❌ Не поддерживается | Динамические 3D-объекты (заглушка в декодере `AVERROR_PATCHWELCOME`). В C-коде FFmpeg открытой реализации Atmos на данный момент не существует (экспериментальные наработки ведутся на других языках, например Rust-проект [MacinDecode-Spatial-Player](https://github.com/SakuzyPeng/MacinDecode-Spatial-Player)). |

---

## 🛠 Технические подробности порта ([FFmpeg 9.0.2](https://github.com/FFmpeg/FFmpeg/tree/release/9.0) / [7.1](https://github.com/FFmpeg/FFmpeg/tree/release/7.1))

1. **`libavcodec/kbdwin.h`**:
   Максимальный размер окна Кайзера-Бесселя (`FF_KBD_WINDOW_MAX`) увеличен с `1024` до `2048` точек, так как AC-4 применяет окно длиной 2048 точек (без этого плеер падал по `av_assert0`).
2. **`libavcodec/ac4dec.c`**:
   Реализована статическая функция векторного умножения `ac4_vector_fmul_reverse_c` взамен скрытого приватного символа. Для FFmpeg 9.0 декодер зарегистрирован через внутреннюю структуру `FFCodec ff_ac4_decoder`.
3. **`libavcodec/ac4_parser.c`**:
   В FFmpeg 9.0 парсер переведён на внутренний интерфейс `parser_internal.h` со структурой `FFCodecParser` и макросом `PARSER_CODEC_LIST(AV_CODEC_ID_AC4)`. (Для FFmpeg 7.1 используется открытый интерфейс `AVCodecParser`).
4. **Регистрация в кодеках**:
   `ff_ac4_decoder` и `ff_ac4_parser` зарегистрированы в `allcodecs.c`, `parsers.c` и `libavcodec/Makefile`.

---

## 🔨 Сборка из исходников (Windows UCRT64 / [MSYS2](https://www.msys2.org/))

### 1. Подготовка окружения
Установите [MSYS2](https://www.msys2.org/) и тулчейн UCRT64:
```bash
pacman -Syu --noconfirm
pacman -S --needed --noconfirm git base-devel mingw-w64-ucrt-x86_64-toolchain \
    mingw-w64-ucrt-x86_64-nasm mingw-w64-ucrt-x86_64-pkgconf \
    mingw-w64-ucrt-x86_64-meson mingw-w64-ucrt-x86_64-ninja \
    mingw-w64-ucrt-x86_64-python mingw-w64-ucrt-x86_64-libass \
    mingw-w64-ucrt-x86_64-libplacebo
```

### 2. Клонирование и наложение патча FFmpeg (9.0.2)
```bash
git clone --depth 1 -b n9.0.2 https://github.com/FFmpeg/FFmpeg.git ffmpeg
cd ffmpeg
git apply ../patches/ffmpeg-9.0.2-ac4.patch

./configure \
    --prefix=/install \
    --enable-shared \
    --disable-static \
    --disable-doc \
    --disable-debug \
    --enable-gpl \
    --enable-version3

make -j$(nproc)
make install
cd ..
```

### 3. Сборка mpv
```bash
git clone --depth 1 https://github.com/mpv-player/mpv.git mpv
export PKG_CONFIG_PATH="/install/lib/pkgconfig:$PKG_CONFIG_PATH"

meson setup mpv-build mpv \
    --prefix=/install_prefix \
    --pkg-config-path=/install/lib/pkgconfig \
    -Dtests=false \
    -Dmanpage-build=disabled

ninja -C mpv-build
ninja -C mpv-build install
```

---

## 📜 Авторы и лицензии

- **[mpv](https://github.com/mpv-player/mpv)**: GPLv2+ / LGPLv2.1+
- **[FFmpeg](https://github.com/FFmpeg/FFmpeg)**: GPLv3 (включает код декодера AC-4)
- Автор оригинального декодера AC-4: **[Paul B Mahol (richardpl)](https://github.com/richardpl)** ([Librempeg project](https://github.com/librempeg/librempeg))
- Скрипты интеграции Windows: **[rossy/mpv-install](https://github.com/rossy/mpv-install)**
- Референсные сборочные скрипты: **[shinchiro/mpv-winbuild-cmake](https://github.com/shinchiro/mpv-winbuild-cmake)**
