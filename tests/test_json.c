#include <locale.h>
#include <math.h>
#include <string.h>

#include "poe_json.h"
#include "test_assert.h"

static uint8_t s_buf[1 << 20];
static uint8_t s_buf2[1 << 20];
static char    s_out[262144];

static JSON_Context* fresh_ctx(void) { return JSON_InitContext(s_buf, sizeof(s_buf)); }

static void test_basic_types(void) {
  TEST_SECTION("создание и печать базовых типов");
  JSON_Context* ctx = JSON_InitContext(s_buf, sizeof(s_buf));
  size_t        written;

  JSON* n = JSON_CreateNull(ctx);
  TEST_CHECK(JSON_Print(ctx, n, s_out, sizeof(s_out), &written) && strcmp(s_out, "null") == 0);

  JSON* t = JSON_CreateBool(ctx, true);
  TEST_CHECK(JSON_Print(ctx, t, s_out, sizeof(s_out), &written) && strcmp(s_out, "true") == 0);

  JSON* f = JSON_CreateBool(ctx, false);
  TEST_CHECK(JSON_Print(ctx, f, s_out, sizeof(s_out), &written) && strcmp(s_out, "false") == 0);

  JSON* s = JSON_CreateString(ctx, "hello");
  TEST_CHECK(JSON_Print(ctx, s, s_out, sizeof(s_out), &written) && strcmp(s_out, "\"hello\"") == 0);

  JSON* arr = JSON_CreateArray(ctx);
  TEST_CHECK(JSON_Type(arr, JSON_ARRAY));
  TEST_CHECK(JSON_GetArraySize(arr) == 0);

  JSON* obj = JSON_CreateObject(ctx);
  TEST_CHECK(JSON_Type(obj, JSON_OBJECT));
}

static void test_number_print(void) {
  JSON_Context* ctx = fresh_ctx();
  size_t        written;

  TEST_SECTION("числа — обычные значения");
  struct {
    double      value;
    const char* expect;
  } cases[] = {
      {0.0, "0"}, {1.0, "1"}, {-1.0, "-1"}, {1000000.0, "1000000"}, {-0.5, "-0.5"}, {123.456, "123.456"}, {0.001, "0.001"}, {-42.5, "-42.5"},
  };
  for(size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    JSON* n = JSON_CreateNumber(ctx, cases[i].value);
    TEST_CHECK(JSON_Print(ctx, n, s_out, sizeof(s_out), &written) && strcmp(s_out, cases[i].expect) == 0);
  }

  TEST_SECTION("-0.0 сохраняет знак при сериализации");
  JSON* neg_zero = JSON_CreateNumber(ctx, -0.0);
  TEST_CHECK(JSON_Print(ctx, neg_zero, s_out, sizeof(s_out), &written) && strcmp(s_out, "-0") == 0);
  JSON* pos_zero = JSON_CreateNumber(ctx, 0.0);
  TEST_CHECK(JSON_Print(ctx, pos_zero, s_out, sizeof(s_out), &written) && strcmp(s_out, "0") == 0);

  TEST_SECTION("числа вне диапазона long long — насыщение, без UB");
  JSON* huge_pos = JSON_CreateNumber(ctx, 1e20 + 0.5);
  TEST_CHECK(JSON_Print(ctx, huge_pos, s_out, sizeof(s_out), &written) && s_out[0] != '-' && strlen(s_out) > 0);
  JSON* huge_neg = JSON_CreateNumber(ctx, -1e20 - 0.5);
  TEST_CHECK(JSON_Print(ctx, huge_neg, s_out, sizeof(s_out), &written) && s_out[0] == '-');
  JSON* dbl_max = JSON_CreateNumber(ctx, 1.7e308);
  TEST_CHECK(JSON_Print(ctx, dbl_max, s_out, sizeof(s_out), &written));

  TEST_SECTION("nan/inf сериализуются в кавычках");
  JSON* nan_node = JSON_CreateNumber(ctx, NAN);
  TEST_CHECK(JSON_Print(ctx, nan_node, s_out, sizeof(s_out), &written) && strcmp(s_out, "\"nan\"") == 0);
  JSON* inf_node = JSON_CreateNumber(ctx, INFINITY);
  TEST_CHECK(JSON_Print(ctx, inf_node, s_out, sizeof(s_out), &written) && strcmp(s_out, "\"inf\"") == 0);
  JSON* ninf_node = JSON_CreateNumber(ctx, -INFINITY);
  TEST_CHECK(JSON_Print(ctx, ninf_node, s_out, sizeof(s_out), &written) && strcmp(s_out, "\"-inf\"") == 0);

  TEST_SECTION("границы точности и переноса разряда");
  JSON* neg_zero_text = JSON_Parse(ctx, "-0");
  TEST_CHECK(neg_zero_text != NULL);
  TEST_CHECK(JSON_Print(ctx, neg_zero_text, s_out, sizeof(s_out), &written) && strcmp(s_out, "-0") == 0);

  JSON* one_point_zero = JSON_Parse(ctx, "1.0");
  TEST_CHECK(one_point_zero && JSON_Print(ctx, one_point_zero, s_out, sizeof(s_out), &written) && strcmp(s_out, "1") == 0);

  JSON* near_ll_max = JSON_CreateNumber(ctx, 4611686018427387904.0); /* 2^62 */
  TEST_CHECK(JSON_Print(ctx, near_ll_max, s_out, sizeof(s_out), &written) && strcmp(s_out, "4611686018427387904") == 0);

  JSON* tiny = JSON_CreateNumber(ctx, -0.000001);
  TEST_CHECK(JSON_Print(ctx, tiny, s_out, sizeof(s_out), &written) && strcmp(s_out, "-0.000001") == 0);

  JSON* carry = JSON_CreateNumber(ctx, 1.9999996);
  TEST_CHECK(JSON_Print(ctx, carry, s_out, sizeof(s_out), &written) && strcmp(s_out, "2") == 0);
}

static void test_number_parse(void) {
  TEST_SECTION("разбор чисел (целые, дробные, экспонента)");
  struct {
    const char* input;
    double      expect;
  } cases[] = {
      {"0", 0.0}, {"-1", -1.0}, {"3.14159", 3.14159}, {"-123.456e2", -12345.6}, {"1e6", 1000000.0}, {"1E-3", 0.001}, {"-0.5", -0.5},
  };
  for(size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    JSON_Context* ctx = fresh_ctx();
    JSON*         n   = JSON_Parse(ctx, cases[i].input);
    TEST_CHECK(n && JSON_Type(n, JSON_NUMBER) && fabs(n->value_num - cases[i].expect) < 1e-6);
  }

  TEST_SECTION("разбор чисел не зависит от locale (strtod был бы чувствителен)");
  char* prev = setlocale(LC_ALL, NULL);
  char  prev_copy[64];
  strncpy(prev_copy, prev ? prev : "C", sizeof(prev_copy) - 1);
  prev_copy[sizeof(prev_copy) - 1] = '\0';

  setlocale(LC_ALL, "ru_RU.UTF-8"); /* разделитель дробной части в этой locale — запятая, не точка */
  JSON_Context* ctx = fresh_ctx();
  JSON*         n   = JSON_Parse(ctx, "3.14159");
  TEST_CHECK(n && JSON_Type(n, JSON_NUMBER) && fabs(n->value_num - 3.14159) < 1e-6);
  setlocale(LC_ALL, prev_copy);
}

static void test_parse_literals(void) {
  TEST_SECTION("разбор null/true/false");
  JSON_Context* ctx = fresh_ctx();
  TEST_CHECK(JSON_Type(JSON_Parse(ctx, "null"), JSON_NULL));
  TEST_CHECK(JSON_Type(JSON_Parse(ctx, "true"), JSON_BOOL));
  TEST_CHECK(JSON_Type(JSON_Parse(ctx, "false"), JSON_BOOL));
}

static void test_string_escapes(void) {
  JSON_Context* ctx = fresh_ctx();
  size_t        written;

  TEST_SECTION("разбор escape-последовательностей строк");
  JSON* s = JSON_Parse(ctx, "\"a\\nb\\tc\\\"d\\\\e\\/f\"");
  TEST_CHECK(s && JSON_Type(s, JSON_STRING) && strcmp(s->value_str, "a\nb\tc\"d\\e/f") == 0);

  JSON* bfr = JSON_Parse(ctx, "\"x\\by\\fz\\rw\"");
  TEST_CHECK(bfr && JSON_Type(bfr, JSON_STRING) && strcmp(bfr->value_str, "x\by\fz\rw") == 0);

  JSON* u = JSON_Parse(ctx, "\"\\u0041\\u00e9\"");
  TEST_CHECK(u && JSON_Type(u, JSON_STRING) && (unsigned char) u->value_str[0] == 'A');

  TEST_CHECK(JSON_Parse(ctx, "\"\\uZZZZ\"") == NULL);

  TEST_SECTION("сериализация строк — экранирование на ВЫХОДЕ (кавычка/бэкслеш/control-байт)");
  JSON* print_s = JSON_CreateString(ctx, "a\"b\\c\001d");
  TEST_CHECK(JSON_Print(ctx, print_s, s_out, sizeof(s_out), &written) && strcmp(s_out, "\"a\\\"b\\\\c\\u0001d\"") == 0);

  JSON_Context* ctx2  = JSON_BeginObject(s_buf2, sizeof(s_buf2));
  JSON*         root2 = ctx2->root;
  JSON_AddNumberToObject(ctx2, root2, "a\"b\\c", 1);
  TEST_CHECK(JSON_Print(ctx2, root2, s_out, sizeof(s_out), &written) && strcmp(s_out, "{\"a\\\"b\\\\c\":1}") == 0);
}

static void test_utf8(void) {
  TEST_SECTION("сырые многобайтовые символы проходят насквозь (не \\u-escaped)");
  JSON_Context* build_ctx = JSON_BeginObject(s_buf, sizeof(s_buf));
  const char*   text      = "Привет, мир! 中文测试 😀🚀";
  JSON_AddStringToObject(build_ctx, build_ctx->root, "s", text);
  size_t written;
  TEST_CHECK(JSON_Print(build_ctx, build_ctx->root, s_out, sizeof(s_out), &written));

  JSON_Context* ctx    = fresh_ctx();
  JSON*         parsed = JSON_Parse(ctx, s_out);
  TEST_CHECK(parsed != NULL);
  JSON* s = JSON_GetObjectItem(parsed, "s");
  TEST_CHECK(s && JSON_Type(s, JSON_STRING) && strcmp(s->value_str, text) == 0);

  TEST_SECTION("\\uXXXX для кодовых точек, требующих 2 и 3 байта");
  JSON* e_acute = JSON_Parse(ctx, "\"\\u00e9\"");
  TEST_CHECK(e_acute && JSON_Type(e_acute, JSON_STRING) && strlen(e_acute->value_str) == 2 && (unsigned char) e_acute->value_str[0] == 0xC3 &&
             (unsigned char) e_acute->value_str[1] == 0xA9);

  JSON* zhong = JSON_Parse(ctx, "\"\\u4e2d\"");
  TEST_CHECK(zhong && JSON_Type(zhong, JSON_STRING) && strlen(zhong->value_str) == 3 && (unsigned char) zhong->value_str[0] == 0xE4 &&
             (unsigned char) zhong->value_str[1] == 0xB8 && (unsigned char) zhong->value_str[2] == 0xAD);

  TEST_SECTION("суррогатные пары \\uXXXX не объединяются (ограничение, не крэш)");
  JSON* surrogate = JSON_Parse(ctx, "\"\\uD83D\\uDE00\"");
  TEST_CHECK(surrogate && JSON_Type(surrogate, JSON_STRING));
}

static void test_parse_reject_invalid(void) {
  TEST_SECTION("некорректный JSON отклоняется, контекст откатывается");
  JSON_Context* ctx        = fresh_ctx();
  size_t        used_empty = ctx->used;
  TEST_CHECK(JSON_Parse(ctx, "") == NULL);
  TEST_CHECK(JSON_Parse(ctx, "{") == NULL);
  TEST_CHECK(JSON_Parse(ctx, "[1,2,") == NULL);
  TEST_CHECK(JSON_Parse(ctx, "{\"a\":}") == NULL);
  TEST_CHECK(JSON_Parse(ctx, "nul") == NULL);
  TEST_CHECK(JSON_Parse(ctx, "123abc") == NULL);
  TEST_CHECK(JSON_Parse(ctx, "{}garbage") == NULL);
  TEST_CHECK(ctx->used == used_empty);
}

static void test_depth_limit(void) {
  TEST_SECTION("ограничение глубины вложенности xPOE_JSON_MAX_DEPTH — массивы");
  char deep[256] = {0};
  int  p         = 0;
  for(int i = 0; i < xPOE_JSON_MAX_DEPTH + 2; i++) deep[p++] = '[';
  for(int i = 0; i < xPOE_JSON_MAX_DEPTH + 2; i++) deep[p++] = ']';
  deep[p]           = '\0';

  JSON_Context* ctx = fresh_ctx();
  TEST_CHECK(JSON_Parse(ctx, deep) == NULL);

  char shallow[32] = {0};
  p                = 0;
  for(int i = 0; i < xPOE_JSON_MAX_DEPTH - 1; i++) shallow[p++] = '[';
  for(int i = 0; i < xPOE_JSON_MAX_DEPTH - 1; i++) shallow[p++] = ']';
  shallow[p] = '\0';
  TEST_CHECK(JSON_Parse(ctx, shallow) != NULL);

  TEST_SECTION("ограничение глубины вложенности xPOE_JSON_MAX_DEPTH — объекты");
  char obj_buf[512] = {0};
  p                 = 0;
  for(int i = 0; i < xPOE_JSON_MAX_DEPTH + 2; i++) {
    memcpy(obj_buf + p, "{\"a\":", 5);
    p += 5;
  }
  obj_buf[p++] = '1';
  for(int i = 0; i < xPOE_JSON_MAX_DEPTH + 2; i++) obj_buf[p++] = '}';
  obj_buf[p] = '\0';
  TEST_CHECK(JSON_Parse(ctx, obj_buf) == NULL);
}

static void test_mixed_nesting_and_empty_containers(void) {
  TEST_SECTION("смешанная вложенность и пустые контейнеры");
  JSON_Context* ctx = fresh_ctx();

  TEST_CHECK(JSON_Type(JSON_Parse(ctx, "[]"), JSON_ARRAY) && JSON_GetArraySize(JSON_Parse(ctx, "[]")) == 0);
  TEST_CHECK(JSON_Type(JSON_Parse(ctx, "{}"), JSON_OBJECT));

  JSON* nested_empty = JSON_Parse(ctx, "[[],{},[[]]]");
  TEST_CHECK(nested_empty && JSON_GetArraySize(nested_empty) == 3);

  JSON* arr_of_obj = JSON_Parse(ctx, "[{\"id\":1},{\"id\":2},{\"id\":3}]");
  TEST_CHECK(arr_of_obj && JSON_GetArraySize(arr_of_obj) == 3);
  JSON* second = JSON_GetArrayItem(arr_of_obj, 1);
  TEST_CHECK(second && JSON_GetObjectItem(second, "id")->value_num == 2);

  JSON* obj_of_arr = JSON_Parse(ctx, "{\"a\":[1,2,3],\"b\":[4,5]}");
  TEST_CHECK(obj_of_arr);
  JSON* a = JSON_GetObjectItem(obj_of_arr, "a");
  JSON* b = JSON_GetObjectItem(obj_of_arr, "b");
  TEST_CHECK(a && JSON_GetArraySize(a) == 3 && b && JSON_GetArraySize(b) == 2);
}

static void test_large_array(void) {
  TEST_SECTION("большой массив (5000 элементов) — доступ по индексу корректен");
  JSON_Context* ctx = JSON_BeginObject(s_buf, sizeof(s_buf));
  JSON*         arr = JSON_CreateArray(ctx);
  const int     N   = 5000;
  for(int i = 0; i < N; i++) JSON_AddItemToArray(ctx, arr, JSON_CreateNumber(ctx, i));

  TEST_CHECK(JSON_GetArraySize(arr) == N);
  TEST_CHECK(JSON_GetArrayItem(arr, 0)->value_num == 0);
  TEST_CHECK(JSON_GetArrayItem(arr, N / 2)->value_num == N / 2);
  TEST_CHECK(JSON_GetArrayItem(arr, N - 1)->value_num == N - 1);
  TEST_CHECK(JSON_GetArrayItem(arr, N) == NULL);

  size_t written;
  TEST_CHECK(JSON_Print(ctx, arr, s_out, sizeof(s_out), &written));
  JSON_Context* parse_ctx = JSON_InitContext(s_buf2, sizeof(s_buf2));
  JSON*         reparsed  = JSON_Parse(parse_ctx, s_out);
  TEST_CHECK(reparsed && JSON_GetArraySize(reparsed) == N);
}

static void test_resource_limits(void) {
  TEST_SECTION("исчерпание арены при разборе — отказ без крэша, без порчи состояния");
  static uint8_t small_buf[200];
  JSON_Context*  ctx = JSON_InitContext(small_buf, sizeof(small_buf));
  TEST_CHECK(ctx != NULL);
  JSON* parsed = JSON_Parse(ctx, "{\"a\":1,\"b\":2,\"c\":3,\"d\":4,\"e\":5,\"f\":6,\"g\":7,\"h\":8,\"i\":9,\"j\":10}");
  TEST_CHECK(parsed == NULL);
  TEST_CHECK(JSON_Parse(ctx, "1") != NULL);

  TEST_SECTION("откат used при нехватке места — Create*/Add*-хелперы, не только строки");
  static uint8_t tiny_buf[200];
  JSON_Context*  tiny_ctx = JSON_BeginObject(tiny_buf, sizeof(tiny_buf));
  TEST_CHECK(tiny_ctx != NULL);
  if(tiny_ctx) {
    size_t before = tiny_ctx->used;
    JSON*  fail   = JSON_AddStringToObject(tiny_ctx, tiny_ctx->root, "key", "a value definitely too long for this buffer size");
    TEST_CHECK(fail == NULL);
    TEST_CHECK(tiny_ctx->used == before);
  }

  static uint8_t rollback_buf[136];
  JSON_Context*  rctx = JSON_BeginObject(rollback_buf, sizeof(rollback_buf));
  TEST_CHECK(rctx != NULL);
  if(rctx) {
    size_t before;

    before = rctx->used;
    TEST_CHECK(JSON_AddNullToObject(rctx, rctx->root, "a") == NULL || rctx->used > before);
    rctx->used      = before;

    before          = rctx->used;
    JSON* bool_fail = JSON_AddBoolToObject(rctx, rctx->root, "b", true);
    if(!bool_fail) TEST_CHECK(rctx->used == before);
    rctx->used     = before;

    before         = rctx->used;
    JSON* obj_fail = JSON_AddObjectToObject(rctx, rctx->root, "c");
    if(!obj_fail) TEST_CHECK(rctx->used == before);
    rctx->used     = before;

    before         = rctx->used;
    JSON* arr_fail = JSON_AddArrayToObject(rctx, rctx->root, "d");
    if(!arr_fail) TEST_CHECK(rctx->used == before);
    rctx->used     = before;

    before         = rctx->used;
    JSON* num_fail = JSON_AddNumberToObject(rctx, rctx->root, "e", 1.5);
    if(!num_fail) TEST_CHECK(rctx->used == before);
    rctx->used            = before;

    JSON* arr_container   = JSON_CreateArray(rctx);
    before                = rctx->used;
    JSON* obj_in_arr_fail = JSON_AddObjectToArray(rctx, arr_container);
    if(!obj_in_arr_fail) TEST_CHECK(rctx->used == before);
  }

  TEST_SECTION("JSON_BeginObject — буфер меньше минимума JSON_InitContext");
  static uint8_t min_buf[80];
  TEST_CHECK(JSON_BeginObject(min_buf, sizeof(min_buf)) == NULL);

  TEST_SECTION("JSON_Print с недостаточным выходным буфером — безопасный отказ/усечение");
  JSON_Context* print_ctx  = JSON_BeginObject(s_buf, sizeof(s_buf));
  JSON*         print_root = print_ctx->root;
  JSON_AddStringToObject(print_ctx, print_root, "key", "0123456789012345678901234567890123456789");
  char   tiny_out[8];
  size_t written = 12345;
  bool   ok      = JSON_Print(print_ctx, print_root, tiny_out, sizeof(tiny_out), &written);
  TEST_CHECK(ok == false);
  TEST_CHECK(written < sizeof(tiny_out));
  TEST_CHECK(tiny_out[written] == '\0');
  TEST_CHECK(JSON_Print(print_ctx, print_root, tiny_out, 0, &written) == false);
}

static void test_whitespace_variety(void) {
  TEST_SECTION("разнообразные пробельные символы вокруг токенов");
  JSON_Context* ctx    = fresh_ctx();
  const char*   padded = "\t\n {  \"a\"\r\n :\t[ 1 ,\n2 , 3]  , \"b\"  :  null }\n ";
  JSON*         parsed = JSON_Parse(ctx, padded);
  TEST_CHECK(parsed && JSON_Type(parsed, JSON_OBJECT));
  JSON* a = JSON_GetObjectItem(parsed, "a");
  TEST_CHECK(a && JSON_GetArraySize(a) == 3);
  TEST_CHECK(JSON_Type(JSON_GetObjectItem(parsed, "b"), JSON_NULL));
}

static void test_roundtrip(void) {
  TEST_SECTION("полный цикл: создание -> печать -> разбор -> сверка");
  JSON_Context* ctx  = JSON_BeginObject(s_buf, sizeof(s_buf));
  JSON*         root = ctx->root;
  JSON_AddStringToObject(ctx, root, "name", "Device");
  JSON_AddNumberToObject(ctx, root, "value", 42);
  JSON_AddBoolToObject(ctx, root, "ok", true);
  JSON_AddNullToObject(ctx, root, "extra");
  JSON* arr = JSON_AddArrayToObject(ctx, root, "items");
  JSON_AddItemToArray(ctx, arr, JSON_CreateNumber(ctx, 1));
  JSON_AddItemToArray(ctx, arr, JSON_CreateNumber(ctx, 2));
  JSON_AddItemToArray(ctx, arr, JSON_CreateString(ctx, "three"));

  size_t written;
  TEST_CHECK(JSON_Print(ctx, root, s_out, sizeof(s_out), &written));

  JSON_Context* parse_ctx = JSON_InitContext(s_buf + sizeof(s_buf) / 2, sizeof(s_buf) / 2);
  JSON*         parsed    = JSON_Parse(parse_ctx, s_out);
  TEST_CHECK(parsed != NULL);

  JSON* name = JSON_GetObjectItem(parsed, "name");
  TEST_CHECK(name && JSON_Type(name, JSON_STRING) && strcmp(name->value_str, "Device") == 0);

  JSON* value = JSON_GetObjectItem(parsed, "value");
  TEST_CHECK(value && JSON_Type(value, JSON_NUMBER) && value->value_num == 42);

  JSON* items = JSON_GetObjectItem(parsed, "items");
  TEST_CHECK(items && JSON_Type(items, JSON_ARRAY) && JSON_GetArraySize(items) == 3);
  TEST_CHECK(JSON_GetArrayItem(items, 0)->value_num == 1);
  TEST_CHECK(JSON_GetArrayItem(items, 2) && strcmp(JSON_GetArrayItem(items, 2)->value_str, "three") == 0);
  TEST_CHECK(JSON_GetArrayItem(items, 3) == NULL);

  TEST_SECTION("JSON_GetObjectItem — отсутствующий ключ возвращает NULL");
  TEST_CHECK(JSON_GetObjectItem(parsed, "missing") == NULL);
}

static void test_duplicate(void) {
  TEST_SECTION("глубокое копирование JSON_Duplicate");
  JSON_Context* ctx  = JSON_BeginObject(s_buf, sizeof(s_buf));
  JSON*         root = ctx->root;
  JSON_AddNumberToObject(ctx, root, "a", 1);
  JSON_AddStringToObject(ctx, root, "s", "test string");
  JSON* arr = JSON_AddArrayToObject(ctx, root, "b");
  JSON_AddItemToArray(ctx, arr, JSON_CreateNumber(ctx, 7));
  JSON_AddItemToArray(ctx, arr, JSON_CreateString(ctx, "in array"));
  JSON* nested_obj = JSON_AddObjectToObject(ctx, root, "nested");
  JSON_AddStringToObject(ctx, nested_obj, "inner", "value");

  JSON* copy = JSON_Duplicate(ctx, root);
  TEST_CHECK(copy != NULL && copy != root);

  size_t written;
  char   out2[256];
  TEST_CHECK(JSON_Print(ctx, root, s_out, sizeof(s_out), &written));
  TEST_CHECK(JSON_Print(ctx, copy, out2, sizeof(out2), &written));
  TEST_CHECK(strcmp(s_out, out2) == 0);

  JSON* copy_s = JSON_GetObjectItem(copy, "s");
  TEST_CHECK(copy_s && strcmp(copy_s->value_str, "test string") == 0);
  TEST_CHECK(copy_s->value_str != JSON_GetObjectItem(root, "s")->value_str);
}

static void test_container_helpers_direct(void) {
  TEST_SECTION("прямые вызовы JSON_AddObjectToObject/JSON_AddObjectToArray/JSON_AddItemToObject");
  JSON_Context* ctx  = JSON_BeginObject(s_buf, sizeof(s_buf));
  JSON*         root = ctx->root;

  JSON* nested       = JSON_AddObjectToObject(ctx, root, "nested");
  TEST_CHECK(nested != NULL && JSON_Type(nested, JSON_OBJECT));
  JSON_AddNumberToObject(ctx, nested, "x", 5);

  JSON* arr        = JSON_AddArrayToObject(ctx, root, "arr");
  JSON* obj_in_arr = JSON_AddObjectToArray(ctx, arr);
  TEST_CHECK(obj_in_arr != NULL && JSON_Type(obj_in_arr, JSON_OBJECT));
  JSON_AddNumberToObject(ctx, obj_in_arr, "y", 9);

  JSON* manual_item = JSON_CreateNumber(ctx, 42);
  TEST_CHECK(JSON_AddItemToObject(ctx, root, "manual", manual_item));

  size_t written;
  TEST_CHECK(JSON_Print(ctx, root, s_out, sizeof(s_out), &written));

  JSON_Context* parse_ctx = JSON_InitContext(s_buf2, sizeof(s_buf2));
  JSON*         parsed    = JSON_Parse(parse_ctx, s_out);
  TEST_CHECK(parsed != NULL);
  TEST_CHECK(JSON_GetObjectItem(JSON_GetObjectItem(parsed, "nested"), "x")->value_num == 5);
  TEST_CHECK(JSON_GetObjectItem(JSON_GetArrayItem(JSON_GetObjectItem(parsed, "arr"), 0), "y")->value_num == 9);
  TEST_CHECK(JSON_GetObjectItem(parsed, "manual")->value_num == 42);
}

static void test_sort_object(void) {
  TEST_SECTION("устойчивость к большому числу ключей (строго убывающий порядок вставки)");
  JSON_Context* ctx  = JSON_BeginObject(s_buf, sizeof(s_buf));
  JSON*         root = ctx->root;

  char key[16];
  for(int i = 3000; i >= 0; i--) {
    snprintf(key, sizeof(key), "k%05d", i);
    JSON_AddNumberToObject(ctx, root, key, i);
  }
  JSON_SortObject(ctx, root);

  JSON* prev  = NULL;
  int   count = 0;
  int   ok    = 1;
  JSON* it;
  JSON_ObjectForEach(it, root) {
    if(prev && strcmp(prev->key_name, it->key_name) > 0) ok = 0;
    prev = it;
    count++;
  }
  TEST_CHECK(ok && count == 3001);

  JSON* appended = JSON_AddNumberToObject(ctx, root, "zzz", 1);
  TEST_CHECK(appended != NULL && root->child->prev == appended);

  TEST_SECTION("перемешанный порядок ключей — все 4 ветки слияния, не только 2");
  JSON_Context* ctx2  = JSON_BeginObject(s_buf2, sizeof(s_buf2));
  JSON*         root2 = ctx2->root;
  const int     N     = 1000;
  for(int i = 0; i < N; i++) {
    int idx = (i * 601) % N;
    snprintf(key, sizeof(key), "k%05d", idx);
    JSON_AddNumberToObject(ctx2, root2, key, idx);
  }
  JSON_SortObject(ctx2, root2);

  prev  = NULL;
  count = 0;
  ok    = 1;
  JSON_ObjectForEach(it, root2) {
    if(prev && strcmp(prev->key_name, it->key_name) > 0) ok = 0;
    prev = it;
    count++;
  }
  TEST_CHECK(ok && count == N);
}

static void test_clear_context(void) {
  TEST_SECTION("JSON_ClearContext сбрасывает арену для повторного использования");
  JSON_Context* ctx = fresh_ctx();
  JSON_Parse(ctx, "{\"a\":1,\"b\":2}");
  size_t used_after_parse = ctx->used;
  TEST_CHECK(used_after_parse > 0);

  JSON_ClearContext(ctx);
  TEST_CHECK(ctx->used == 0);
  TEST_CHECK(JSON_Parse(ctx, "{\"c\":3}") != NULL);
}

static void test_invalid_args(void) {
  TEST_SECTION("защита от некорректных аргументов");
  TEST_CHECK(JSON_InitContext(NULL, 1024) == NULL);
  TEST_CHECK(JSON_InitContext(s_buf, 4) == NULL);

  JSON_Context* ctx = fresh_ctx();
  TEST_CHECK(JSON_Parse(ctx, NULL) == NULL);
  TEST_CHECK(JSON_GetObjectItem(NULL, "x") == NULL);
  TEST_CHECK(JSON_GetArrayItem(NULL, 0) == NULL);
  TEST_CHECK(JSON_Type(NULL, JSON_NULL) == false);

  TEST_SECTION("JSON_Print на узле с неизвестным type — безопасный отказ, не крэш");
  JSON* n = JSON_CreateNull(ctx);
  n->type = 0; /* JSON_Invalid — намеренно портим для проверки default-ветки сериализатора */
  size_t written;
  TEST_CHECK(JSON_Print(ctx, n, s_out, sizeof(s_out), &written) == false);
}

int main(void) {
  test_basic_types();
  test_number_print();
  test_number_parse();
  test_parse_literals();
  test_string_escapes();
  test_utf8();
  test_parse_reject_invalid();
  test_depth_limit();
  test_mixed_nesting_and_empty_containers();
  test_large_array();
  test_resource_limits();
  test_whitespace_variety();
  test_roundtrip();
  test_duplicate();
  test_container_helpers_direct();
  test_sort_object();
  test_clear_context();
  test_invalid_args();

  TEST_REPORT();
  return s_test_failed == 0 ? 0 : 1;
}
