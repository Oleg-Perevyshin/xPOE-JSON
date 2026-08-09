# 📄 xPOE-JSON

## 📋 Обзор

`poe_json` — лёгкий парсер и генератор JSON (RFC 8259) для встраиваемых систем с ограниченной памятью.
Использует **арена-аллокатор** (bump allocator) для детерминированного управления памятью без `malloc`.
Поддерживает полный DOM (Document Object Model) с двусвязными списками.

Кроссплатформенная независимая библиотека без внешних зависимостей — только стандартная библиотека C
(`stdbool.h`, `stddef.h`, `stdint.h`, `ctype.h`, `math.h`, `string.h`). Не требует RTOS, вендорского SDK
или какого-либо конкретного фреймворка — компилируется как на встраиваемых MCU (ARM Cortex-M, ESP32...),
так и на хосте. Разбор и печать чисел не используют `strtod`/`sprintf` — свои реализации, не зависящие от
locale и не требующие `stdio.h`/`stdlib.h` (см. «Особенности»).

## 🏗️ Архитектура

```
┌───────────────────────────────────────────────────────────────────────────┐
│                             POE JSON Library                              │
├───────────────────────────────────────────────────────────────────────────┤
│  ┌─────────────────────────────────────────────────────────────────────┐  │
│  │                        Arena Allocator (Bump)                       │  │
│  │  ┌─────────────────────────────────────────────────────────────┐    │  │
│  │  │            JSON_Context { buffer, size, used, root }        │    │  │
│  │  └─────────────────────────────────────────────────────────────┘    │  │
│  └─────────────────────────────────────────────────────────────────────┘  │
│                                    │                                      │
│                                    ▼                                      │
│  ┌─────────────────────────────────────────────────────────────────────┐  │
│  │                             JSON DOM                                │  │
│  │   ┌─────────────────────────────────────────────────────────────┐   │  │
│  │   │  JSON { next, prev, child, type, key_name, value_str, num } │   │  │
│  │   └─────────────────────────────────────────────────────────────┘   │  │
│  └─────────────────────────────────────────────────────────────────────┘  │
│                                                                           │
│  ┌─────────────────────────────┐  ┌────────────────────────────────────┐  │
│  │           Parser            │  │             Generator              │  │
│  │  JSON_Parse(ctx, string)    │  │  JSON_Print(ctx, item, buf, size)  │  │
│  └─────────────────────────────┘  └────────────────────────────────────┘  │
└───────────────────────────────────────────────────────────────────────────┘
```

## 🧱 Типы JSON

### 📌 Типы узлов

| Тип | Значение | Описание |
|-----|----------|----------|
| `JSON_NULL` | `1 << 0` | null значение |
| `JSON_False` | `1 << 1` | false |
| `JSON_True` | `1 << 2` | true |
| `JSON_Number` | `1 << 3` | число (double) |
| `JSON_String` | `1 << 4` | строка |
| `JSON_Array` | `1 << 5` | массив |
| `JSON_Object` | `1 << 6` | объект |

### 📌 Структура узла

```c
typedef struct JSON {
  struct JSON* next;      /* Следующий элемент в списке */
  struct JSON* prev;      /* Предыдущий элемент в списке */
  struct JSON* child;     /* Первый дочерний элемент */
  int          type;      /* Тип узла (битовая маска) */
  char*        key_name;  /* Имя ключа (только для объектов) */
  char*        value_str; /* Строковое значение */
  double       value_num; /* Числовое значение */
} JSON;
```

### 🧠 Контекст арена-аллокатора

```c
typedef struct {
  uint8_t* buffer;   /* Указатель на буфер */
  size_t   size;     /* Размер буфера */
  size_t   used;     /* Использовано байт */
  JSON*    root;     /* Корневой узел */
  bool     init;     /* Флаг инициализации */
} JSON_Context;
```

## 📖 API Reference

Все функции, кроме `JSON_Print`/`JSON_AddItemToArray`/`JSON_AddItemToObject`, возвращают указатель
(`NULL` при ошибке) или `void`. Три перечисленные — `bool` (`true` = успех).

### 🚀 Инициализация контекста

```c
JSON_Context* JSON_InitContext(void* buffer, size_t buffer_size);
```

Инициализирует контекст без создания корневого узла.

```c
JSON_Context* JSON_BeginObject(void* buffer, size_t buffer_size);
```

Инициализирует контекст и создаёт корневой объект.

**Очистка:**
```c
void JSON_ClearContext(JSON_Context* ctx);
```

Просто сбрасывает счётчик `used` (быстрое освобождение всей памяти).

### 🔍 Парсинг

```c
JSON* JSON_Parse(JSON_Context* ctx, const char* value);
```

Разбирает JSON строку в DOM. При ошибке откатывает контекст.

### ✏️ Сериализация

```c
bool JSON_Print(JSON_Context* ctx, const JSON* item, char* output,
                 size_t out_len, size_t* written_len);
```

Преобразует DOM в JSON строку. Возвращает `true` при успехе.

### 📌 Доступ к данным

```c
int JSON_GetArraySize(const JSON* array);
JSON* JSON_GetArrayItem(const JSON* array, int index);
JSON* JSON_GetObjectItem(const JSON* object, const char* string);
bool JSON_Type(const JSON* item, JSON_check_type check);
```

**Итераторы:**
```c
JSON_ArrayForEach(element, array) { /* ... */ }
JSON_ObjectForEach(element, object) { /* ... */ }
```

### 📌 Создание узлов

```c
JSON* JSON_CreateArray(JSON_Context* ctx);
JSON* JSON_CreateObject(JSON_Context* ctx);
JSON* JSON_CreateNull(JSON_Context* ctx);
JSON* JSON_CreateBool(JSON_Context* ctx, bool boolean);
JSON* JSON_CreateNumber(JSON_Context* ctx, double num);
JSON* JSON_CreateString(JSON_Context* ctx, const char* string);
```

### 📌 Добавление в контейнер

```c
/* Добавление примитивов в объект */
JSON* JSON_AddNullToObject(JSON_Context* ctx, JSON* obj, const char* key);
JSON* JSON_AddBoolToObject(JSON_Context* ctx, JSON* obj, const char* key, bool v);
JSON* JSON_AddNumberToObject(JSON_Context* ctx, JSON* obj, const char* key, double v);
JSON* JSON_AddStringToObject(JSON_Context* ctx, JSON* obj, const char* key, const char* v);

/* Добавление контейнеров */
JSON* JSON_AddObjectToObject(JSON_Context* ctx, JSON* obj, const char* key);
JSON* JSON_AddArrayToObject(JSON_Context* ctx, JSON* obj, const char* key);
JSON* JSON_AddObjectToArray(JSON_Context* ctx, JSON* array);

/* Универсальные */
bool JSON_AddItemToObject(JSON_Context* ctx, JSON* obj, const char* key, JSON* item);
bool JSON_AddItemToArray(JSON_Context* ctx, JSON* array, JSON* item);
```

### 🛠️ Утилиты

```c
JSON* JSON_Duplicate(JSON_Context* ctx, const JSON* item);
void JSON_SortObject(JSON_Context* ctx, JSON* obj);
```

`printf` в примерах ниже — только для читаемости примера; сама библиотека `stdio.h` не требует, на
встраиваемой цели замените на свой логгер (`SC_LOG_*` и т.п.).

## 💡 Пример использования

### 📌 Создание JSON

```c
uint8_t buffer[4096];
JSON_Context* ctx = JSON_BeginObject(buffer, sizeof(buffer));

JSON* root = ctx->root;
JSON_AddStringToObject(ctx, root, "name", "Device");
JSON_AddNumberToObject(ctx, root, "value", 42);

JSON* arr = JSON_AddArrayToObject(ctx, root, "items");
JSON_AddItemToArray(ctx, arr, JSON_CreateString(ctx, "first"));
JSON_AddItemToArray(ctx, arr, JSON_CreateString(ctx, "second"));

char output[2048];
JSON_Print(ctx, root, output, sizeof(output), NULL);

JSON_ClearContext(ctx);
/* output: {"name":"Device","value":42,"items":["first","second"]} */
```

### 🔍 Парсинг JSON

```c
uint8_t buffer[4096];
JSON_Context* ctx = JSON_InitContext(buffer, sizeof(buffer));
JSON* root = JSON_Parse(ctx, "{\"name\":\"Device\",\"value\":42}");

if(root) {
  JSON* name = JSON_GetObjectItem(root, "name");
  if(JSON_Type(name, JSON_STRING)) {
    printf("name: %s\n", name->value_str);
  }

  JSON* value = JSON_GetObjectItem(root, "value");
  if(JSON_Type(value, JSON_NUMBER)) {
    printf("value: %f\n", value->value_num);
  }
}

JSON_ClearContext(ctx);
```

### 📌 Итерация по массиву

```c
JSON* array = JSON_GetObjectItem(root, "items");
if(JSON_Type(array, JSON_ARRAY)) {
  JSON* item;
  JSON_ArrayForEach(item, array) {
    printf("item: %s\n", item->value_str);
  }
}
```

## 🔤 Escape последовательности

Парсер поддерживает стандартные escape последовательности:

| Последовательность | Результат |
|-------------------|-----------|
| `\"` | `"` |
| `\\` | `\` |
| `\/` | `/` |
| `\b` | Backspace |
| `\f` | Form feed |
| `\n` | New line |
| `\r` | Carriage return |
| `\t` | Tab |
| `\uXXXX` | Unicode символ (UTF-8) |

## ⚠️ Ограничения

| Параметр | Значение | Примечание |
|----------|----------|------------|
| Максимальная глубина | 8 | `xPOE_JSON_MAX_DEPTH` (переопределяется через `#define` до включения `poe_json.h`) |
| Размер контекста | buffer - sizeof(JSON_Context) | Вся память в буфере |
| Кодировка | UTF-8 | Только |
| Числа | double | 64-bit IEEE 754 |
| `JSON_GetArraySize` | O(n) | Обход связного списка на каждый вызов, результат не кешируется |
| Числа вне диапазона `long long` | Насыщение | `\|value\| ≥ 2⁶³` (за пределами разумных телеметрийных величин) сериализуется с потерей точности до `±9223372036854775807` вместо UB на приведении к целому — double за пределами `2⁵²` дробной части всё равно не хранит |

## ✨ Особенности

1. **Нет динамической памяти** — вся память выделяется из предоставленного буфера
2. **Bump allocator** — быстрое линейное выделение, O(1) на операцию
3. **Выравнивание аллокатора — 8 байт, не 4.** `JSON` содержит `double`, а на Cortex-M `LDRD`/`VLDR`
   для 8-байтовых операндов требуют выровненного адреса (часть тулчейнов дополнительно закладывается
   на полное natural alignment `double` в структуре). Уменьшение константы до 4 байт ради экономии
   памяти — не оптимизация, а реальный риск `UsageFault` на таком MCU, поэтому она не вынесена в
   переопределяемый макрос (в отличие от `xPOE_JSON_MAX_DEPTH`)
4. **Откат при ошибке** — при ошибке парсинга или нехватке памяти в `Create*`/`Add*` контекст
   (`ctx->used`) возвращается в состояние до вызова, арена не «протекает» частично записанными узлами
5. **Двусвязные списки, вставка в конец за O(1)** — поле `prev` головного узла контейнера (`child`)
   не указывает на предыдущий элемент (у головы предыдущего и нет), а хранит указатель на **последний**
   элемент списка (хвост) — так `link_item` находит конец списка без отдельного поля `tail` в
   `JSON_Context`/`JSON`. После операций, перестраивающих список только через `next` (сортировка,
   см. ниже), этот инвариант приходится восстанавливать отдельным проходом
6. **Сортировка ключей** — `JSON_SortObject` упорядочивает ключи лексикографически
7. **Специальные числа** — поддержка `inf`, `-inf`, `nan` (записываются в кавычках)
8. **Без `%lld`/`%llu`** — сериализация целых вне диапазона `int`/`long` идёт вручную (`ll_to_str`,
   деление/остаток по 10, без `printf`) — актуально для встраиваемых libc (например, `newlib-nano`),
   которые не поддерживают 64-битные форматы `printf` ни при каком линкер-флаге
9. **Без `strtod`/`sprintf`** — разбор и печать чисел не зависят от текущей locale процесса (у `strtod`
   разделитель дробной части — `.` только в locale `"C"`) и не тянут `stdio.h`/`stdlib.h`. Разбор строк
   идёт в два прохода: сначала вычисляется итоговая длина (с учётом экранирования и `\uXXXX`→UTF-8) без
   записи, затем выделяется ровно нужный блок и пишется один раз — без реаллокаций
10. **`-0.0` сохраняет знак** — проверка через `signbit()`, а не `val < 0` (по IEEE 754 `-0.0 < 0` ложно)
11. **Числа вне диапазона `long long` не приводятся к нему напрямую** — приведение `(long long) aval`
    для `|val| ≥ 2⁶³` было бы неопределённым поведением; вместо этого значение насыщается границей
    `±9223372036854775807`. Потери здесь нет: double за пределами `2⁵²` всё равно хранит только целые
    значения, дробной части там физически нет
12. **`JSON_Duplicate` рекурсивен** — глубина ограничена той же константой `xPOE_JSON_MAX_DEPTH`, что и
    разбор JSON, поэтому стек безопасен при той же дисциплине входных данных, что и у парсера
13. **`JSON_SortObject` без рекурсии** — итеративная сортировка слиянием (bottom-up, по образцу
    алгоритма Саймона Тэтема), O(1) дополнительного стека независимо от числа ключей в объекте
14. **Одна ветка кода в `JSON_BeginObject` физически недостижима** и помечена `LCOV_EXCL_START/STOP`
    для инструментов покрытия: `JSON_InitContext` уже требует запас `+64` байт сверх `sizeof(JSON_Context)`,
    а `sizeof(JSON)` — 56 байт на большинстве платформ, так что создание первого узла сразу после
    успешной инициализации провалиться не может. Ветка оставлена как защита на случай, если структура
    `JSON` в будущем вырастет за эти 64 байта

## 🧾 Форматы чисел

| Вход | Выход |
|------|-------|
| `123` | `123` |
| `123.456` | `123.456` |
| `0.001` | `0.001` |
| `-0.5` | `-0.5` |
| `1e6` | `1000000` |
| `inf` | `"inf"` |
| `nan` | `"nan"` |

Дробная часть сериализуется с точностью до 6 знаков после запятой, хвостовые нули отбрасываются.

## 📦 Зависимости

Только стандартная библиотека C: `ctype.h`, `math.h` (`-lm` при линковке на хосте), `string.h`. Никаких
`stdio.h`/`stdlib.h`, сторонних библиотек, RTOS или платформенных SDK.

## 🧩 Интеграция в проект

Библиотека — два файла: `poe_json.h` + `poe_json.c`. Никакой генерации, никаких скрытых зависимостей
между файлами проекта.

**Любой проект (STM32, любой другой MCU, хост)** — скопируйте `poe_json.c`/`poe_json.h` в дерево
компонентов/исходников и добавьте `poe_json.c` в сборку. Это единственное, что нужно — сама библиотека
не завязана ни на какую платформу.

**ESP-IDF** — репозиторий одновременно является ESP-IDF компонентом (`idf_component.yml` +
`CMakeLists.txt` в корне, тот же `poe_json.c`/`poe_json.h`, без дублирования файлов):

```bash
idf.py add-dependency "poe_json^1.0.0"     # после публикации в ESP Component Registry
```

или локально, без публикации — путём в `dependencies` вашего компонента:

```yaml
dependencies:
  poe_json:
    path: "../xPOE-JSON"
```

`CMakeLists.txt` сам определяет контекст сборки: внутри `idf.py` вызывается `idf_component_register`,
вне его — обычная хостовая сборка с CTest (см. ниже). Один файл, два режима, без раздвоения исходников.

## 🧪 Сборка и тесты (автономно)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

`CMakeLists.txt` в корне собирает `poe_json` как статическую библиотеку и (по умолчанию,
`POE_JSON_BUILD_TESTS=ON`) юнит-тест `tests/test_json.c` через CTest. Для встраивания в проект как
исходники (а не как отдельная CMake-цель) сборочный файл не обязателен — достаточно двух `.c`/`.h`.

## 💡 Пример полного цикла

```c
/* 1. Создание JSON */
uint8_t create_buf[4096];
JSON_Context* create_ctx = JSON_BeginObject(create_buf, sizeof(create_buf));
JSON_AddStringToObject(create_ctx, create_ctx->root, "status", "ok");
JSON_AddNumberToObject(create_ctx, create_ctx->root, "progress", 50);

char json_str[1024];
JSON_Print(create_ctx, create_ctx->root, json_str, sizeof(json_str), NULL);
JSON_ClearContext(create_ctx);

/* 2. Парсинг JSON */
uint8_t parse_buf[4096];
JSON_Context* parse_ctx = JSON_InitContext(parse_buf, sizeof(parse_buf));
JSON* root = JSON_Parse(parse_ctx, json_str);

JSON* status = JSON_GetObjectItem(root, "status");
if(status && JSON_Type(status, JSON_STRING)) {
  printf("status: %s\n", status->value_str);
}

JSON* progress = JSON_GetObjectItem(root, "progress");
if(progress && JSON_Type(progress, JSON_NUMBER)) {
  printf("progress: %.0f%%\n", progress->value_num);
}

JSON_ClearContext(parse_ctx);
```

---
© 2026 Олег Перевышин — лицензия [MIT](LICENSE)
