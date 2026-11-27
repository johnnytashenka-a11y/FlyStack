<div align="right">
  🇷🇺 <b>Русский</b> | <a href="README.en.md">🇺🇸 English</a>
</div>

<div align="center">

# FlyStack — Custom Taskbar

**Легковесный и быстрый кастомизатор панели задач для Windows 10/11 x64, написанный на чистом C++ и Win32 API.**

<img width="1031" height="537" alt="Preview" src="https://github.com/user-attachments/assets/49a4d8b6-4cbd-4140-bba7-aa72dbea134c" />

</div>

---

> [!NOTE]
> ### Почему именно FlyStack?
>
> - **Нативная скорость.** C++ и Win32 API, минимальные зависимости и мгновенный отклик.
> - **Реальный системный трей.** FlyStack не эмулирует иконки, а берет нативный `TrayNotifyWnd` и оставляет логику уведомлений Windows нетронутой.
> - **Гибкость из коробки.** Настраиваемые отступы, скругления и поведение элементов панели.
> - **Минималистичный дизайн.** Аккуратная плавающая капсула, которая не перекрывает рабочий стол.
> - **Ничего лишнего.** Открытый исходный код, без рекламы и без установщика.

## Интерфейс и возможности

### Плавающая панель и системный трей

Компактная капсула с индикаторами активных окон, системным поиском и нативным треем в стилизованном CSS-островке:

<img width="1044" height="510" alt="image" src="https://github.com/user-attachments/assets/c018376a-f924-440d-881e-92f603fdd10b" />

## Установка и запуск

1. Перейдите в раздел **[Releases](../../releases)**.
2. Скачайте `FlyStackTaskbar_MVP_win_x64.zip` и распакуйте архив.
3. Запустите `FlyStackTaskbar.exe`. Программа работает в **portable-режиме** и не требует установки.

> [!IMPORTANT]
> `WebView2Loader.dll` должен лежать рядом с `FlyStackTaskbar.exe`. Также необходим [Microsoft WebView2 Runtime](https://developer.microsoft.com/microsoft-edge/webview2/) — обычно он уже установлен в системе.

> [!TIP]
> При первом запуске Windows SmartScreen может показать предупреждение: это стандартная реакция на неподписанные open-source утилиты, работающие с окнами Проводника.

Для выхода завершите процесс обычным способом (например, Alt+F4 или через Dispatcher задач): стандартная панель `Shell_TrayWnd` будет снова показана.

## Сборка из исходников

Нужны Visual Studio 2022 или Build Tools с компонентом **Desktop development with C++** (набор инструментов v143, x64).

Из корня репозитория:

```bat
source\build.bat
```

Скрипт сам находит Visual Studio через `vswhere.exe`, собирает `FlyStackTaskbar.exe` в корне и копирует рядом `WebView2Loader.dll`.

## Устранение неполадок

<details>
<summary><b>⚠️ Урезанные сборки Windows (AtlasOS, ReviOS и др.)</b></summary>

В кастомных сборках часто вырезаны компоненты Проводника и UWP-службы. Из-за этого кастомный трей может работать некорректно, а в редких случаях возможен сбой при обращении к отсутствующим API. Используйте оригинальные образы Windows.

</details>

<details>
<summary><b>🛠 Как вернуть стандартную панель задач, если она пропала</b></summary>

**Для Windows 11:**

1. Откройте Диспетчер задач (`Ctrl + Shift + Esc`), завершите `FlyStackTaskbar.exe`.
2. Нажмите `Win + R`, введите `cmd` и выполните:

```cmd
reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\StuckRects3" /f & taskkill /f /im explorer.exe & start explorer.exe
```

**Для Windows 10:**

1. Завершите `FlyStackTaskbar.exe` через Диспетчер задач.
2. Нажмите `Win + R`, введите `cmd` и выполните:

```cmd
reg.exe add "HKCU\Software\Classes\CLSID\{86ca1aa0-34aa-4e8b-a509-50c905bae2a2}\InprocServer32" /f /ve
```

</details>

## История изменений

См. [CHANGELOG.md](CHANGELOG.md).
