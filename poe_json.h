/* poe_json.h | RFC 8259 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ********************************************************** */
/* Глубина JSON */
#ifndef xPOE_JSON_MAX_DEPTH
#define xPOE_JSON_MAX_DEPTH 8
#endif

/* Типы узлов (битовые флаги) */
#define JSON_Invalid                        0
#define JSON_Null                           (1 << 0)
#define JSON_False                          (1 << 1)
#define JSON_True                           (1 << 2)
#define JSON_Number                         (1 << 3)
#define JSON_String                         (1 << 4)
#define JSON_Array                          (1 << 5)
#define JSON_Object                         (1 << 6)

/* ********************************************************** */
/* Итератор */
#define JSON_ArrayForEach(element, array)   for(element = (array != NULL) ? (array)->child : NULL; element != NULL; element = element->next)
#define JSON_ObjectForEach(element, object) for(element = (object != NULL) ? (object)->child : NULL; element != NULL; element = element->next)

/* Структура узла DOM (двусвязный список для детей + данные) */
typedef struct JSON {
  struct JSON* next;      /* Указатель на следующий элемент в списке (для siblings) */
  struct JSON* prev;      /* Указатель на предыдущий элемент в списке */
  struct JSON* child;     /* Указатель на первый дочерний элемент */
  int          type;      /* Тип узла: JSON_NULL, JSON_False, JSON_True, JSON_Number, JSON_String, JSON_Array, JSON_Object */
  char*        key_name;  /* Имя ключа (только для объектов, для массивов = NULL) */
  char*        value_str; /* Строковое значение (только для JSON_String) */
  double       value_num; /* Числовое значение (только для JSON_Number, JSON_True=1, JSON_False=0) */
} JSON;

/* Типы для проверки */
typedef enum {
  JSON_INVALID = 0,
  JSON_NULL    = (1 << 0),            /* 1 */
  JSON_BOOL    = (1 << 1) | (1 << 2), /* 6 */
  JSON_NUMBER  = (1 << 3),            /* 8 */
  JSON_STRING  = (1 << 4),            /* 16 */
  JSON_ARRAY   = (1 << 5),            /* 32 */
  JSON_OBJECT  = (1 << 6),            /* 64 */
} JSON_check_type;

/* Контекст арена-аллокатора */
typedef struct {
  uint8_t* buffer;
  size_t   size;
  size_t   used;
  JSON*    root;
  bool     init;
} JSON_Context;

/* ********************************************************** */
/* Инициализация контекста для парсинга */
JSON_Context* JSON_InitContext(void* buffer, size_t buffer_size);

/* Инициализация контекста для создания JSON */
JSON_Context* JSON_BeginObject(void* buffer, size_t buffer_size);

/* Освобождение контекста */
void JSON_ClearContext(JSON_Context* ctx);

/* ********************************************************** */
/* Парсинг и сериализация */
JSON* JSON_Parse(JSON_Context* ctx, const char* value);
bool  JSON_Print(JSON_Context* ctx, const JSON* item, char* output, size_t out_len, size_t* written_len);

/* Доступ к данным */
int   JSON_GetArraySize(const JSON* array);                       /* Количество элементов массива, O(n) — обход связного списка, не кешируется */
JSON* JSON_GetArrayItem(const JSON* array, int index);            /* Элемент массива по индексу */
JSON* JSON_GetObjectItem(const JSON* object, const char* string); /* Поле объекта по имени ключа */
bool  JSON_Type(const JSON* item, JSON_check_type check);         /* Проверка типа узла */

/* Создание узлов */
JSON* JSON_CreateArray(JSON_Context* ctx);                      /* Новый пустой массив */
JSON* JSON_CreateObject(JSON_Context* ctx);                     /* Новый пустой объект */
JSON* JSON_CreateNull(JSON_Context* ctx);                       /* Новый узел null */
JSON* JSON_CreateBool(JSON_Context* ctx, bool boolean);         /* Новый булев узел */
JSON* JSON_CreateNumber(JSON_Context* ctx, double num);         /* Новый числовой узел */
JSON* JSON_CreateString(JSON_Context* ctx, const char* string); /* Новый строковый узел */

/* Добавление элементов в контейнер */
JSON* JSON_AddObjectToObject(JSON_Context* ctx, JSON* object, const char* name);             /* Вложенный объект по ключу */
JSON* JSON_AddArrayToObject(JSON_Context* ctx, JSON* object, const char* name);              /* Вложенный массив по ключу */
bool  JSON_AddItemToArray(JSON_Context* ctx, JSON* array, JSON* item);                       /* Готовый узел в конец массива */
bool  JSON_AddItemToObject(JSON_Context* ctx, JSON* object, const char* string, JSON* item); /* Готовый узел по ключу */
JSON* JSON_AddObjectToArray(JSON_Context* ctx, JSON* array);                                 /* Новый объект в конец массива */

/* Добавление примитивов в контейнер */
JSON* JSON_AddNullToObject(JSON_Context* ctx, JSON* object, const char* name);                       /* Поле null по ключу */
JSON* JSON_AddBoolToObject(JSON_Context* ctx, JSON* object, const char* name, bool boolean);         /* Булево поле по ключу */
JSON* JSON_AddNumberToObject(JSON_Context* ctx, JSON* object, const char* name, double number);      /* Числовое поле по ключу */
JSON* JSON_AddStringToObject(JSON_Context* ctx, JSON* object, const char* name, const char* string); /* Строковое поле по ключу */

/* Утилиты */
JSON* JSON_Duplicate(JSON_Context* ctx, const JSON* item); /* Глубокая копия узла (с детьми) */
void  JSON_SortObject(JSON_Context* ctx, JSON* object);    /* Сортировка полей объекта по имени ключа */
