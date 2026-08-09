/* poe_json.c */
#include "poe_json.h"

#include <ctype.h>
#include <math.h>
#include <string.h>

/* ********************************************************** */
/* Инициализация контекста */
JSON_Context* JSON_InitContext(void* buffer, size_t buffer_size) {
  if(!buffer || buffer_size < sizeof(JSON_Context) + 64) return NULL;
  JSON_Context* ctx = (JSON_Context*) buffer;
  ctx->buffer       = (uint8_t*) buffer + sizeof(JSON_Context);
  ctx->size         = buffer_size - sizeof(JSON_Context);
  ctx->used         = 0;
  ctx->root         = NULL;
  ctx->init         = true;
  return ctx;
}

/* Инициализация JSON объекта */
JSON_Context* JSON_BeginObject(void* buffer, size_t buffer_size) {
  JSON_Context* ctx = JSON_InitContext(buffer, buffer_size);
  if(!ctx) return NULL;
  ctx->root = JSON_CreateObject(ctx);
  if(!ctx->root) { /* недостижимо при текущих константах, см. README */
    /* LCOV_EXCL_START */
    ctx->used = 0;
    ctx->init = false;
    return NULL;
    /* LCOV_EXCL_STOP */
  }
  return ctx;
}

/* Очистка контекста */
void JSON_ClearContext(JSON_Context* ctx) {
  if(ctx && ctx->init) { ctx->used = 0; }
}

/* ********************************************************** */
/* Выделение из буфера (Bump Allocator) */
static void* json_bump_alloc(JSON_Context* ctx, size_t size) {
  if(!ctx || !ctx->init) return NULL;
  size = (size + 7) & ~7; /* 8 байт — не менять, см. README */
  if(ctx->used + size > ctx->size) return NULL;
  void* ptr = ctx->buffer + ctx->used;
  ctx->used += size;
  return ptr;
}

/* Выделение строки с копированием */
static char* json_str_alloc(JSON_Context* ctx, const char* src, size_t len) {
  if(!src) return NULL;
  char* dst = (char*) json_bump_alloc(ctx, len + 1);
  if(dst) {
    memcpy(dst, src, len);
    dst[len] = '\0';
  }
  return dst;
}

/* ********************************************************** */
/* Парсинг строк (два прохода: расчет длины -> выделение -> запись) */
static bool parse_string(JSON* item, const char* input, size_t max_len, size_t* out_offset, JSON_Context* ctx) {
  if(input[0] != '"') return false;
  const char* p       = input + 1;
  size_t      raw_len = 0, unescaped_len = 0;
  while(raw_len < max_len && p[raw_len] != '"') {
    if(p[raw_len] == '\\') {
      if(raw_len + 1 >= max_len) return false;
      char c = p[raw_len + 1];
      unescaped_len += (c == 'u') ? 4 : 1;
      raw_len += 2;
    } else {
      unescaped_len++;
      raw_len++;
    }
  }
  if(p[raw_len] != '"') return false;

  char* dst = (char*) json_bump_alloc(ctx, unescaped_len + 1);
  if(!dst) return false;

  char* q = dst;
  for(size_t i = 0; i < raw_len;) {
    if(p[i] == '\\') {
      char c = p[i + 1];
      switch(c) {
        case 'b':
          *q++ = '\b';
          i += 2;
          break;
        case 'f':
          *q++ = '\f';
          i += 2;
          break;
        case 'n':
          *q++ = '\n';
          i += 2;
          break;
        case 'r':
          *q++ = '\r';
          i += 2;
          break;
        case 't':
          *q++ = '\t';
          i += 2;
          break;
        case 'u': {
          unsigned int h = 0;
          for(int j = 0; j < 4; j++) {
            char ch = p[i + 2 + j];
            h <<= 4;
            if(ch >= '0' && ch <= '9') h += ch - '0';
            else if(ch >= 'A' && ch <= 'F') h += ch - 'A' + 10;
            else if(ch >= 'a' && ch <= 'f') h += ch - 'a' + 10;
            else return false;
          }
          if(h < 0x80) *q++ = (char) h;
          else if(h < 0x800) {
            *q++ = 0xC0 | (h >> 6);
            *q++ = 0x80 | (h & 0x3F);
          } else if(h < 0x10000) {
            *q++ = 0xE0 | (h >> 12);
            *q++ = 0x80 | ((h >> 6) & 0x3F);
            *q++ = 0x80 | (h & 0x3F);
          } else return false;

          i += 6;
          break;
        }
        default:
          *q++ = c;
          i += 2;
          break;
      }
    } else {
      *q++ = p[i];
      i++;
    }
  }
  *q              = '\0';
  item->type      = JSON_String;
  item->value_str = dst;
  if(out_offset) *out_offset = raw_len + 2; /* +2 для кавычек */
  return true;
}

/* Разбор числа без strtod — не зависит от locale, см. README */
static double parse_number(const char* input, size_t num_len) {
  size_t i   = 0;
  bool   neg = false;
  if(i < num_len && input[i] == '-') {
    neg = true;
    i++;
  }
  double mantissa = 0.0;
  while(i < num_len && input[i] >= '0' && input[i] <= '9') {
    mantissa = mantissa * 10.0 + (double) (input[i] - '0');
    i++;
  }
  int frac_digits = 0;
  if(i < num_len && input[i] == '.') {
    i++;
    while(i < num_len && input[i] >= '0' && input[i] <= '9') {
      mantissa = mantissa * 10.0 + (double) (input[i] - '0');
      frac_digits++;
      i++;
    }
  }
  int  exp     = 0;
  bool exp_neg = false;
  if(i < num_len && (input[i] == 'e' || input[i] == 'E')) {
    i++;
    if(i < num_len && (input[i] == '+' || input[i] == '-')) {
      exp_neg = (input[i] == '-');
      i++;
    }
    while(i < num_len && input[i] >= '0' && input[i] <= '9') {
      exp = exp * 10 + (input[i] - '0');
      i++;
    }
  }
  int    scale  = (exp_neg ? -exp : exp) - frac_digits;
  double result = mantissa;
  while(scale > 0) {
    result *= 10.0;
    scale--;
  }
  while(scale < 0) {
    result /= 10.0;
    scale++;
  }
  return neg ? -result : result;
}

/* Парсинг значения */
static bool parse_value(JSON* item, const char* input, size_t len, size_t* out_offset, JSON_Context* ctx, size_t depth) {
  if(depth > xPOE_JSON_MAX_DEPTH) return false;
  size_t i = 0;
  while(i < len && input[i] <= 32) i++;
  if(i >= len) return false;
  input += i;
  len -= i;

  if(len >= 4 && strncmp(input, "null", 4) == 0) {
    item->type = JSON_NULL;
    *out_offset += i + 4;
    return true;
  }

  if(len >= 5 && strncmp(input, "false", 5) == 0) {
    item->type = JSON_False;
    *out_offset += i + 5;
    return true;
  }

  if(len >= 4 && strncmp(input, "true", 4) == 0) {
    item->type      = JSON_True;
    item->value_num = 1;
    *out_offset += i + 4;
    return true;
  }

  if(input[0] == '-' || isdigit((unsigned char) input[0])) {
    const char* p = input;
    if(*p == '-') p++;
    if(!isdigit((unsigned char) *p)) return false;
    while(isdigit((unsigned char) *p)) p++;
    if(*p == '.') {
      p++;
      if(!isdigit((unsigned char) *p)) return false;
      while(isdigit((unsigned char) *p)) p++;
    }
    if(*p == 'e' || *p == 'E') {
      p++;
      if(*p == '+' || *p == '-') p++;
      if(!isdigit((unsigned char) *p)) return false;
      while(isdigit((unsigned char) *p)) p++;
    }
    size_t num_len  = (size_t) (p - input);
    item->type      = JSON_Number;
    item->value_num = parse_number(input, num_len);
    *out_offset += i + num_len;
    return true;
  }

  if(input[0] == '"') {
    size_t str_offset = 0;
    if(!parse_string(item, input, len, &str_offset, ctx)) return false;
    *out_offset += i + str_offset;
    return true;
  }

  if(input[0] == '[') {
    JSON*  head = NULL;
    JSON*  prev = NULL;
    size_t cur  = 1;
    while(cur < len && input[cur] <= 32) cur++;
    if(cur < len && input[cur] == ']') {
      *out_offset += i + cur + 1;
      item->type = JSON_Array;
      return true;
    }

    do {
      JSON* new_node = (JSON*) json_bump_alloc(ctx, sizeof(JSON));
      if(!new_node) return false;
      memset(new_node, 0, sizeof(JSON));
      size_t child_off = 0;
      if(!parse_value(new_node, input + cur, len - cur, &child_off, ctx, depth + 1)) return false;
      cur += child_off;
      if(!head) {
        head = new_node;
        prev = new_node;
      } else {
        prev->next     = new_node;
        new_node->prev = prev;
        prev           = new_node;
      }
      while(cur < len && input[cur] <= 32) cur++;
      if(cur < len && input[cur] == ',') cur++;
      else break;
    } while(cur < len);

    while(cur < len && input[cur] <= 32) cur++;
    if(cur >= len || input[cur] != ']') return false;
    *out_offset += i + cur + 1;
    item->type  = JSON_Array;
    item->child = head;
    if(head) head->prev = prev;
    return true;
  }

  if(input[0] == '{') {
    JSON*  head = NULL;
    JSON*  prev = NULL;
    size_t cur  = 1;
    while(cur < len && input[cur] <= 32) cur++;
    if(cur < len && input[cur] == '}') {
      *out_offset += i + cur + 1;
      item->type = JSON_Object;
      return true;
    }

    do {
      while(cur < len && input[cur] <= 32) cur++;
      JSON* new_node = (JSON*) json_bump_alloc(ctx, sizeof(JSON));
      if(!new_node) return false;
      memset(new_node, 0, sizeof(JSON));

      size_t key_off = 0;
      if(!parse_string(new_node, input + cur, len - cur, &key_off, ctx)) return false;
      new_node->key_name  = new_node->value_str;
      new_node->value_str = NULL;
      cur += key_off;

      while(cur < len && input[cur] <= 32) cur++;
      if(cur >= len || input[cur] != ':') return false;
      cur++;

      size_t val_off = 0;
      if(!parse_value(new_node, input + cur, len - cur, &val_off, ctx, depth + 1)) return false;
      cur += val_off;

      if(!head) {
        head = new_node;
        prev = new_node;
      } else {
        prev->next     = new_node;
        new_node->prev = prev;
        prev           = new_node;
      }
      while(cur < len && input[cur] <= 32) cur++;
      if(cur < len && input[cur] == ',') cur++;
      else break;
    } while(cur < len);

    while(cur < len && input[cur] <= 32) cur++;
    if(cur >= len || input[cur] != '}') return false;
    *out_offset += i + cur + 1;
    item->type  = JSON_Object;
    item->child = head;
    if(head) head->prev = prev;
    return true;
  }
  return false;
}

/* Парсинг и сериализация */
JSON* JSON_Parse(JSON_Context* ctx, const char* value) {
  if(!ctx || !ctx->init || !value) return NULL;
  size_t len = strlen(value);
  if(len == 0) return NULL;
  size_t saved_used = ctx->used;

  JSON* root        = (JSON*) json_bump_alloc(ctx, sizeof(JSON));
  if(!root) return NULL;
  memset(root, 0, sizeof(JSON));

  size_t offset = 0;
  if(!parse_value(root, value, len, &offset, ctx, 0)) {
    ctx->used = saved_used;
    return NULL;
  }

  while(offset < len && value[offset] <= 32) offset++;
  if(offset < len) {
    ctx->used = saved_used;
    return NULL;
  }
  return root;
}

/* ********************************************************** */
/* int64 → строка без printf, см. README */
static int ll_to_str(char* buf, long long v) {
  bool               neg = v < 0;
  unsigned long long uv  = neg ? (unsigned long long) (-(v + 1)) + 1ULL : (unsigned long long) v;
  char               tmp[24];
  int                n = 0;
  do {
    tmp[n++] = (char) ('0' + (uv % 10));
    uv /= 10;
  } while(uv > 0);
  int len = 0;
  if(neg) buf[len++] = '-';
  while(n > 0) buf[len++] = tmp[--n];
  buf[len] = '\0';
  return len;
}

/* ********************************************************** */
/* Сериализация */
static bool print_value(const JSON* item, char* buf, size_t size, size_t* pos, size_t depth) {
  if(depth > xPOE_JSON_MAX_DEPTH || !item || *pos >= size) return false;
  switch(item->type & 0xFF) {
    case JSON_NULL: {
      if(*pos + 4 > size) return false;
      memcpy(buf + *pos, "null", 4);
      *pos += 4;
      break;
    }
    case JSON_False: {
      if(*pos + 5 > size) return false;
      memcpy(buf + *pos, "false", 5);
      *pos += 5;
      break;
    }
    case JSON_True: {
      if(*pos + 4 > size) return false;
      memcpy(buf + *pos, "true", 4);
      *pos += 4;
      break;
    }
    case JSON_Number: {
      double val = item->value_num;
      if(isnan(val)) {
        if(*pos + 5 > size) return false;
        memcpy(buf + *pos, "\"nan\"", 5);
        *pos += 5;
      } else if(isinf(val)) {
        if(val > 0) {
          if(*pos + 5 > size) return false;
          memcpy(buf + *pos, "\"inf\"", 5);
          *pos += 5;
        } else {
          if(*pos + 6 > size) return false;
          memcpy(buf + *pos, "\"-inf\"", 6);
          *pos += 6;
        }
      } else {
        char      tmp[40];
        int       len;
        bool      negative = signbit(val); /* val<0 не ловит -0.0, см. README */
        double    aval     = negative ? -val : val;
        long long int_part;
        int       frac_part = 0;

        if(aval < 9223372036854775808.0) {
          int_part    = (long long) aval;
          double frac = aval - (double) int_part;
          frac_part   = (int) (frac * 1000000.0 + 0.5);
          if(frac_part >= 1000000) {
            frac_part -= 1000000;
            int_part++;
          }
        } else {
          int_part = 9223372036854775807LL; /* насыщение вместо UB, см. README */
        }

        int p = 0;
        if(negative) tmp[p++] = '-';
        p += ll_to_str(tmp + p, int_part);
        if(frac_part != 0) {
          /* Обрезаем хвостовые нули строкой, не int-делением */
          char frac_str[7];
          for(int i = 5; i >= 0; i--) {
            frac_str[i] = (char) ('0' + frac_part % 10);
            frac_part /= 10;
          }
          frac_str[6] = '\0';
          int flen    = 6;
          while(flen > 1 && frac_str[flen - 1] == '0') flen--;
          tmp[p++] = '.';
          memcpy(tmp + p, frac_str, (size_t) flen);
          p += flen;
        }
        len = p;

        if(*pos + (size_t) len > size) return false;
        memcpy(buf + *pos, tmp, (size_t) len);
        *pos += len;
      }
      break;
    }
    case JSON_String: {
      if(*pos + 1 > size) return false;
      buf[(*pos)++] = '"';
      char* p       = item->value_str;
      while(p && *p) {
        unsigned char c = *p++;
        if(c == '"' || c == '\\') {
          if(*pos + 2 > size) return false;
          buf[(*pos)++] = '\\';
          buf[(*pos)++] = c;
        } else if(c < 32) {
          static const char hex_digits[] = "0123456789abcdef";
          if(*pos + 6 > size) return false;
          buf[(*pos)++] = '\\';
          buf[(*pos)++] = 'u';
          buf[(*pos)++] = '0';
          buf[(*pos)++] = '0';
          buf[(*pos)++] = hex_digits[(c >> 4) & 0xF];
          buf[(*pos)++] = hex_digits[c & 0xF];
        } else {
          if(*pos + 1 > size) return false;
          buf[(*pos)++] = c;
        }
      }
      if(*pos + 1 > size) return false;
      buf[(*pos)++] = '"';
      break;
    }
    case JSON_Array: {
      if(*pos + 1 > size) return false;
      buf[(*pos)++] = '[';
      JSON* ch      = item->child;
      bool  first   = true;
      while(ch) {
        if(!first) {
          if(*pos + 1 > size) return false;
          buf[(*pos)++] = ',';
        }
        first = false;
        if(!print_value(ch, buf, size, pos, depth + 1)) return false;
        ch = ch->next;
      }
      if(*pos + 1 > size) return false;
      buf[(*pos)++] = ']';
      break;
    }
    case JSON_Object: {
      if(*pos + 1 > size) return false;
      buf[(*pos)++] = '{';
      JSON* ch      = item->child;
      bool  first   = true;
      while(ch) {
        if(!first) {
          if(*pos + 1 > size) return false;
          buf[(*pos)++] = ',';
        }
        first = false;

        if(*pos + 1 > size) return false;
        buf[(*pos)++] = '"';
        const char* p = ch->key_name;
        while(p && *p) {
          unsigned char c = *p++;
          if(*pos + 1 > size) return false;
          if(c == '"' || c == '\\') {
            buf[(*pos)++] = '\\';
            buf[(*pos)++] = c;
          } else buf[(*pos)++] = c;
        }
        if(*pos + 1 > size) return false;
        buf[(*pos)++] = '"';

        if(*pos + 1 > size) return false;
        buf[(*pos)++] = ':';
        if(!print_value(ch, buf, size, pos, depth + 1)) return false;
        ch = ch->next;
      }
      if(*pos + 1 > size) return false;
      buf[(*pos)++] = '}';
      break;
    }
    default: return false;
  }
  return true;
}

bool JSON_Print(JSON_Context* ctx, const JSON* item, char* output, size_t out_len, size_t* written_len) {
  (void) ctx;
  if(!item || !output || out_len == 0) return false;
  size_t pos = 0;
  bool   res = print_value(item, output, out_len, &pos, 0);
  if(res && pos < out_len) {
    output[pos] = '\0';
    if(written_len) *written_len = pos;
  } else if(out_len > 0) {
    output[out_len - 1] = '\0';
    if(written_len) *written_len = out_len - 1;
  } else if(written_len) *written_len = 0;
  return res;
}

/* ********************************************************** */
/* Доступ к данным */
int JSON_GetArraySize(const JSON* array) {
  if(!array || (array->type & 0xFF) != JSON_Array) return 0;
  int c = 0;
  for(JSON* p = array->child; p; p = p->next) c++;
  return c;
}

JSON* JSON_GetArrayItem(const JSON* array, int index) {
  if(!array || index < 0 || (array->type & 0xFF) != JSON_Array) return NULL;
  JSON* p = array->child;
  while(p && index--) p = p->next;
  return p;
}

JSON* JSON_GetObjectItem(const JSON* object, const char* string) {
  if(!object || !string || (object->type & 0xFF) != JSON_Object) return NULL;
  for(JSON* p = object->child; p; p = p->next)
    if(p->key_name && strcmp(string, p->key_name) == 0) return p;
  return NULL;
}

bool JSON_Type(const JSON* item, JSON_check_type check) {
  if(!item) return false;
  int t = item->type & 0xFF;
  if(check == JSON_BOOL) return (t & (JSON_True | JSON_False)) != 0;
  return t == (int) check;
}

static JSON* alloc_node(JSON_Context* ctx) {
  if(!ctx || !ctx->init) return NULL;
  JSON* n = (JSON*) json_bump_alloc(ctx, sizeof(JSON));
  if(n) memset(n, 0, sizeof(JSON));
  return n;
}

/* ********************************************************** */
/* Создание узлов */
JSON* JSON_CreateArray(JSON_Context* ctx) {
  JSON* n = alloc_node(ctx);
  if(n) n->type = JSON_Array;
  return n;
}

JSON* JSON_CreateObject(JSON_Context* ctx) {
  JSON* n = alloc_node(ctx);
  if(n) n->type = JSON_Object;
  return n;
}

JSON* JSON_CreateNull(JSON_Context* ctx) {
  JSON* n = alloc_node(ctx);
  if(n) n->type = JSON_NULL;
  return n;
}

JSON* JSON_CreateBool(JSON_Context* ctx, bool b) {
  JSON* n = alloc_node(ctx);
  if(n) n->type = b ? JSON_True : JSON_False;
  return n;
}

JSON* JSON_CreateNumber(JSON_Context* ctx, double n) {
  JSON* i = alloc_node(ctx);
  if(i) {
    i->type      = JSON_Number;
    i->value_num = n;
  }
  return i;
}

JSON* JSON_CreateString(JSON_Context* ctx, const char* s) {
  JSON* i = alloc_node(ctx);
  if(i) {
    i->type      = JSON_String;
    i->value_str = json_str_alloc(ctx, s, strlen(s));
    if(!i->value_str) {
      ctx->used -= sizeof(JSON);
      return NULL;
    }
  }
  return i;
}

/* ********************************************************** */
/* Добавление в контейнер */
static bool link_item(JSON* container, JSON* item) {
  if(!container || !item) return false;
  if(!container->child) {
    container->child = item;
    item->prev       = item;
    return true;
  }
  JSON* last             = container->child->prev;
  last->next             = item;
  item->prev             = last;
  item->next             = NULL;
  container->child->prev = item;
  return true;
}

bool JSON_AddItemToArray(JSON_Context* ctx, JSON* array, JSON* item) {
  (void) ctx;
  if(!array || !item || (array->type & 0xFF) != JSON_Array || item->next) return false;
  return link_item(array, item);
}

bool JSON_AddItemToObject(JSON_Context* ctx, JSON* object, const char* string, JSON* item) {
  if(!ctx || !object || !string || !item || (object->type & 0xFF) != JSON_Object || item->next) return false;
  item->key_name = json_str_alloc(ctx, string, strlen(string));
  if(!item->key_name) return false;
  return link_item(object, item);
}

JSON* JSON_AddNullToObject(JSON_Context* ctx, JSON* obj, const char* k) {
  size_t saved_used = ctx->used;
  JSON*  i          = JSON_CreateNull(ctx);
  if(i && JSON_AddItemToObject(ctx, obj, k, i)) return i;
  ctx->used = saved_used;
  return NULL;
}

JSON* JSON_AddBoolToObject(JSON_Context* ctx, JSON* obj, const char* k, bool v) {
  size_t saved_used = ctx->used;
  JSON*  i          = JSON_CreateBool(ctx, v);
  if(i && JSON_AddItemToObject(ctx, obj, k, i)) return i;
  ctx->used = saved_used;
  return NULL;
}

JSON* JSON_AddNumberToObject(JSON_Context* ctx, JSON* obj, const char* k, double v) {
  size_t saved_used = ctx->used;
  JSON*  i          = JSON_CreateNumber(ctx, v);
  if(i && JSON_AddItemToObject(ctx, obj, k, i)) return i;
  ctx->used = saved_used;
  return NULL;
}

JSON* JSON_AddStringToObject(JSON_Context* ctx, JSON* obj, const char* k, const char* v) {
  size_t saved_used = ctx->used;
  JSON*  i          = JSON_CreateString(ctx, v);
  if(i && JSON_AddItemToObject(ctx, obj, k, i)) return i;
  ctx->used = saved_used;
  return NULL;
}

JSON* JSON_AddObjectToObject(JSON_Context* ctx, JSON* obj, const char* k) {
  size_t saved_used = ctx->used;
  JSON*  i          = JSON_CreateObject(ctx);
  if(i && JSON_AddItemToObject(ctx, obj, k, i)) return i;
  ctx->used = saved_used;
  return NULL;
}

JSON* JSON_AddArrayToObject(JSON_Context* ctx, JSON* obj, const char* k) {
  size_t saved_used = ctx->used;
  JSON*  i          = JSON_CreateArray(ctx);
  if(i && JSON_AddItemToObject(ctx, obj, k, i)) return i;
  ctx->used = saved_used;
  return NULL;
}

JSON* JSON_AddObjectToArray(JSON_Context* ctx, JSON* array) {
  size_t saved_used = ctx->used;
  JSON*  i          = JSON_CreateObject(ctx);
  if(i && JSON_AddItemToArray(ctx, array, i)) return i;
  ctx->used = saved_used;
  return NULL;
}

/* ********************************************************** */
/* Дублирование — рекурсивно, глубина ограничена, см. README */
static JSON* dup_rec(JSON_Context* ctx, const JSON* src, size_t d) {
  if(!src || d > xPOE_JSON_MAX_DEPTH) return NULL;
  JSON* n = alloc_node(ctx);
  if(!n) return NULL;
  n->type      = src->type;
  n->value_num = src->value_num;
  if(src->key_name) {
    n->key_name = json_str_alloc(ctx, src->key_name, strlen(src->key_name));
    if(!n->key_name) return NULL;
  }
  if(src->value_str) {
    n->value_str = json_str_alloc(ctx, src->value_str, strlen(src->value_str));
    if(!n->value_str) return NULL;
  }
  JSON* last = NULL;
  for(const JSON* c = src->child; c; c = c->next) {
    JSON* nc = dup_rec(ctx, c, d + 1);
    if(!nc) return NULL;
    if(!n->child) n->child = nc;
    if(last) {
      last->next = nc;
      nc->prev   = last;
    }
    last = nc;
  }
  if(last) {
    last->next = NULL;
    if(n->child) n->child->prev = last;
  }
  return n;
}

JSON* JSON_Duplicate(JSON_Context* ctx, const JSON* item) { return dup_rec(ctx, item, 0); }

/* ********************************************************** */
/* Сортировка — итеративная сортировка слиянием, без рекурсии, см. README */
static JSON* list_merge_sort(JSON* list) {
  if(!list) return NULL;
  size_t insize = 1;
  while(1) {
    JSON*  p       = list;
    JSON*  tail    = NULL;
    size_t nmerges = 0;
    list           = NULL;

    while(p) {
      nmerges++;
      JSON*  q     = p;
      size_t psize = 0;
      for(size_t i = 0; i < insize; i++) {
        psize++;
        q = q->next;
        if(!q) break;
      }
      size_t qsize = insize;

      while(psize > 0 || (qsize > 0 && q)) {
        JSON* e;
        if(psize == 0) {
          e = q;
          q = q->next;
          qsize--;
        } else if(qsize == 0 || !q) {
          e = p;
          p = p->next;
          psize--;
        } else if(strcmp(p->key_name, q->key_name) <= 0) {
          e = p;
          p = p->next;
          psize--;
        } else {
          e = q;
          q = q->next;
          qsize--;
        }
        if(tail) tail->next = e;
        else list = e;
        tail = e;
      }
      p = q;
    }
    tail->next = NULL;

    if(nmerges <= 1) return list;
    insize *= 2;
  }
}

void JSON_SortObject(JSON_Context* ctx, JSON* obj) {
  (void) ctx;
  if(!obj || (obj->type & 0xFF) != JSON_Object) return;
  JSON* head = list_merge_sort(obj->child);
  /* prev не участвует в сортировке — восстанавливаем отдельно, см. README */
  JSON* prev = NULL;
  for(JSON* p = head; p; p = p->next) {
    p->prev = prev;
    prev    = p;
  }
  if(head) head->prev = prev; /* голова хранит хвост, см. link_item */
  obj->child = head;
}
