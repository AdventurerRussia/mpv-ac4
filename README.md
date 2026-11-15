# mpv AC-4 Edition (Windows x64)

[![Release](https://img.shields.io/github/v/release/AdventurerRussia/mpv-ac4?include_prereleases&color=blue&style=flat-square)](https://github.com/AdventurerRussia/mpv-ac4/releases/latest)
[![Platform](https://img.shields.io/badge/platform-Windows%20x64-informational?style=flat-square)](https://github.com/AdventurerRussia/mpv-ac4/releases/latest)
[![Codec](https://img.shields.io/badge/audio-Dolby%20AC--4%20(ETSI%20TS%20103%20190)-success?style=flat-square)](https://github.com/AdventurerRussia/mpv-ac4)

Специальная сборка медиаплеера **[mpv](https://mpv.io/)** с нативной поддержкой декодирования аудиопотоков **Dolby AC-4** (ETSI TS 103 190-1), включая эфирные записи **ATSC 3.0 / DVB-T2**.

---

## ⚡ О проекте

В официальном FFmpeg и стандартных сборках mpv (включая сборки от shinchiro) декодер AC-4 отсутствует (есть только демультиплексор заголовков). При попытке открыть видео с дорожкой AC-4 плеер не может воспроизвести звук.

В данном проекте нативный декодер AC-4 от Paul B Mahol (из форка `librempeg`) был полностью портирован и адаптирован для стабильной ветки **FFmpeg 7.1** с сохранением `libswresample`. Плеер mpv скомпилирован с этим кастомным FFmpeg и воспроизводит аудиодорожки AC-4 напрямую без сторонних фильтров и костылей.

---

## 📥 Скачать готовую сборку (Portable)

Готовая портативная сборка для Windows x64 доступна во вкладке **Releases**:
* **[Скачать MPV-AC4-Windows-x64.zip](https://github.com/AdventurerRussia/mpv-ac4/releases/latest)**

### Структура каталога (схема shinchiro / rossy):
```text
MPV PLAYER AC-4/
├── fonts/               # Пользовательские шрифты интерфейса и субтитров
├── installer/           # Официальные скрипты установки (rossy/mpv-install)
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
| **AC-4 Atmos (Part 2 / A-JOC)** | ❌ Не поддерживается | Динамические 3D-объекты (заглушка в декодере `AVERROR_PATCHWELCOME`). В C-коде FFmpeg открытой реализации Atmos на данный момент не существует. |

---

## 🛠 Технические подробности порта (FFmpeg 7.1)

1. **`libavcodec/kbdwin.h`**:
   Максимальный размер окна Кайзера-Бесселя (`FF_KBD_WINDOW_MAX`) увеличен с `1024` до `2048` точек, так как AC-4 применяет окно длиной 2048 точек (без этого плеер падал по `av_assert0`).
2. **`libavcodec/ac4dec.c`**:
   Реализована статическая функция векторного умножения `ac4_vector_fmul_reverse_c` взамен скрытого приватного символа FFmpeg 7.1.
3. **`libavcodec/ac4_parser.c`**:
   Парсер адаптирован под открытый API структуры `AVCodecParser` вместо упразднённого внутреннего `parser_internal.h`.
4. **Регистрация в кодеках**:
   `ff_ac4_decoder` и `ff_ac4_parser` зарегистрированы в `allcodecs.c`, `parsers.c` и `libavcodec/Makefile`.

---

## 🔨 Сборка из исходников (Windows UCRT64 / MSYS2)

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

### 2. Клонирование и наложение патча FFmpeg
```bash
git clone --depth 1 -b n7.1 https://github.com/FFmpeg/FFmpeg.git ffmpeg
cd ffmpeg
git apply ../patches/ffmpeg-7.1-ac4.patch

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

## 📜 Лицензия

- **mpv**: GPLv2+ / LGPLv2.1+
- **FFmpeg**: GPLv3 (включает код декодера AC-4)
- Автор оригинального декодера AC-4: **Paul B Mahol** (Librempeg project).
