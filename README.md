# Custom Taskbar. FlyStack.

<img width="1000" height="481" alt="Анимация" src="https://github.com/user-attachments/assets/d82e0e50-4c4e-439d-8bd9-cf143105a9fd" />

---

> [!NOTE]
> ## FlyStack — не первый и не последний кастомизатор панели задач.
> 
> Почему из сотен таких-же можно выбрать именно **FlyStack**?
> 
> Гибкость настроек. Из коробки ты можешь менять почти все составляющие панели задач.
>
> Передовой дизайн. Минималистичная капсула притягивает взглядом. Не так-ли?
>
> Это просто крутой проект без рекламы.

## FlyStack — это про дизайн.

> [!IMPORTANT]
> Кастомный календарь
>
> <img width="1030" height="544" alt="image" src="https://github.com/user-attachments/assets/76d954c1-2699-45a2-81c7-fa67cd699b39" />


> [!IMPORTANT]
> Кастомная панель задач
>
> <img width="543" height="164" alt="image" src="https://github.com/user-attachments/assets/bef48441-6cff-459a-b385-726091d5e0f3" />


> [!IMPORTANT]
> Кастомный трей
>
> <img width="1040" height="158" alt="image" src="https://github.com/user-attachments/assets/cb4ffc7b-ccc1-4262-b978-6afcec9c353e" />

---

> [!WARNING]
> ## Об урезанных сборках Windows по типу Atlas, Revi и тому подобных.
>
> **Из-за урезанных компонентов этих сборок:**
> 
> Кастомный трей может работать **некорректно**.
> 
> Иногда может появляться **BSOD**.
>
> Я в целом не гарантирую стабильную работу многих функций на них.

---

> [!IMPORTANT]
> **Лечение пропавшей панели задач в Windows 10:**
>
> 1. Откройте диспетчер задач, найдите процесс FlyStackTaskbar.exe, остановите его
> 2. Хоткеем Win + R откройте cmd, введите команду: "reg.exe add "HKCU\Software\Classes\CLSID\{86ca1aa0-34aa-4e8b-a509-50c905bae2a2}\InprocServer32" /f /ve"
>
> **Лечение пропавшей панели задач в Windows 11:**
> 1. Откройте диспетчер задач, найдите процесс FlyStackTaskbar.exe, остановите его
> 2. Хоткеем Win + R откройте cmd, введите команду: "reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\StuckRects3" /f & taskkill /f /im explorer.exe & start explorer.exe"

## Установка и запуск

1. Перейдите в раздел **[Releases](../../releases)** справа.
2. Скачайте актуальную версию `FlyStack.exe`.
3. Запустите файл — установка не требуется, программа работает в portable-режиме.

> [!NOTE]
> Так как приложение глубоко взаимодействует с интерфейсом Windows, встроенный защитник Windows SmartScreen может показать предупреждение при первом запуске. Это нормально для неподписанных утилит с открытым исходным кодом.







