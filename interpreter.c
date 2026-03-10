#include "interpreter.h"

#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ==========================================
// 哈希表实现
// ==========================================

// fnv1a
#define FNV_OFFSET_BASIS 0x811c9dc5 // 常数
#define FNV_PRIME 0x01000193        // 质数

size_t fnv1a_hash_cstr(const char *key) {
  return fnv1a_hash_cstr_len(key, strlen(key));
}
size_t fnv1a_hash_cstr_len(const char *key, int len) {
  size_t hash = FNV_OFFSET_BASIS;
  for (int i = 0; i < len; i++) {
    hash ^= key[i];
    hash *= FNV_PRIME;
  }
  return hash;
}
size_t fnv1a_hash_obj_str(ObjString *key) {
  assert(key != NULL);
  vm_ensure_obj_str_hashed(key);
  return key->hash;
}
void vm_ensure_obj_str_hashed(ObjString *key) {
  if (key->hash == 0) {
    key->hash = fnv1a_hash_cstr_len(key->data, key->length);
  }
}

HashTable *hash_table_new() {
  HashTable *tab = calloc(1, sizeof(HashTable));
  if (tab == NULL) {
    return NULL;
  }

  tab->capacity = 16; // 初始容量
  tab->count = 0;
  tab->buckets = calloc(tab->capacity, sizeof(HashTableEntry *));

  if (tab->buckets == NULL) {
    free(tab);
    return NULL;
  }
  return tab;
}
void hash_table_free(HashTable *tab) {
  if (tab == NULL)
    return;

  // 释放所有 entry
  for (int i = 0; i < tab->capacity; i++) {
    HashTableEntry *entry = tab->buckets[i];
    while (entry != NULL) {
      HashTableEntry *next = entry->next;
      free(entry); // 只释放 entry 本身，key 由 GC 管理
      entry = next;
    }
  }

  free(tab->buckets);
  free(tab);
}
bool hash_table_clear(HashTable *tab) {
  if (tab == NULL)
    return false;

  for (int i = 0; i < tab->capacity; i++) {
    HashTableEntry *entry = tab->buckets[i];
    while (entry != NULL) {
      HashTableEntry *next = entry->next;
      free(entry);
      entry = next;
    }
    tab->buckets[i] = NULL;
  }

  tab->count = 0;
  return true;
}

static HashTableEntry *hash_table_find_entry_impl(HashTable *tab,
                                                  const char *str, int len,
                                                  uint32_t hash) {
  if (tab == NULL || str == NULL || tab->count == 0) {
    return NULL;
  }

  int idx = hash % tab->capacity;
  HashTableEntry *entry = tab->buckets[idx];

  while (entry != NULL) {
    // 长度相同，哈希相同，且内容相同
    if (entry->key->length == len && entry->key->hash == hash &&
        memcmp(entry->key->data, str, len) == 0) {
      return entry; // 返回 entry 指针
    }
    entry = entry->next;
  }
  return NULL;
}
static HashTableEntry *hash_table_find_entry_obj_str(HashTable *tab,
                                                     ObjString *key) {
  vm_ensure_obj_str_hashed(key);
  return hash_table_find_entry_impl(tab, key->data, key->length, key->hash);
}
static HashTableEntry *
hash_table_find_entry_cstr_len(HashTable *tab, const char *str, int len) {
  uint32_t hash = fnv1a_hash_cstr_len(str, len);
  return hash_table_find_entry_impl(tab, str, len, hash);
}

bool hash_table_get(HashTable *tab, ObjString *key, Value *out) {
  if (tab == NULL || key == NULL)
    return false;

  HashTableEntry *entry = hash_table_find_entry_obj_str(tab, key);
  if (entry != NULL) {
    if (out)
      *out = entry->value;
    return true;
  }

  return false;
}

// 扩容
static void hash_table_grow(HashTable *tab) {
  int old_capacity = tab->capacity;
  HashTableEntry **old_buckets = tab->buckets;

  // 新容量翻倍
  tab->capacity = old_capacity * 2;
  tab->buckets = calloc(tab->capacity, sizeof(HashTableEntry *));
  tab->count = 0;

  // 重新哈希所有 entry
  for (int i = 0; i < old_capacity; i++) {
    HashTableEntry *entry = old_buckets[i];
    while (entry != NULL) {
      HashTableEntry *next = entry->next;

      // 重新插入
      int new_idx = entry->key->hash % tab->capacity;
      entry->next = tab->buckets[new_idx];
      tab->buckets[new_idx] = entry;
      tab->count++;

      entry = next;
    }
  }

  free(old_buckets);
}

bool hash_table_put(HashTable *tab, ObjString *key, Value value) {
  if (tab == NULL || key == NULL)
    return false;

  HashTableEntry *existing = hash_table_find_entry_obj_str(tab, key);
  if (existing != NULL) {
    existing->value = value; // 更新现有键
    return true;
  }

  // 检查是否需要扩容（负载因子 0.75）
  if (tab->count >= tab->capacity * 0.75) {
    hash_table_grow(tab);
  }

  // 计算索引
  int idx = key->hash % tab->capacity;

  // 创建新 entry
  HashTableEntry *new_entry = malloc(sizeof(HashTableEntry));
  if (new_entry == NULL)
    return false;

  // 设置键值对
  new_entry->key = key;
  new_entry->value = value;

  // 头插法
  new_entry->next = tab->buckets[idx];
  tab->buckets[idx] = new_entry;
  tab->count++;

  return true;
}

void hash_table_remove(HashTable *tab, ObjString *key) {
  if (tab == NULL || key == NULL)
    return;

  vm_ensure_obj_str_hashed(key);

  uint32_t hash = key->hash;
  int idx = hash % tab->capacity;

  HashTableEntry **prev_next = &tab->buckets[idx];
  HashTableEntry *entry = tab->buckets[idx];

  int len = key->length;
  while (entry != NULL) {
    if (entry->key->hash == hash && entry->key->length == len &&
        memcmp(entry->key->data, key->data, len) == 0) {

      // 从链表中移除
      *prev_next = entry->next;
      free(entry); // 只释放 entry，key 由 GC 管理
      tab->count--;
      return;
    }

    prev_next = &entry->next;
    entry = entry->next;
  }
}

// ==========================================
// 解释器实现
// ==========================================

void vm_fatal_error_terminate(const char *fmt, ...) {
#define PREFIX "VM FATAL ERROR: "

  va_list args;
  va_start(args, fmt);

  fprintf(stderr, PREFIX);
  vfprintf(stderr, fmt, args);
  fprintf(stderr, "\n");

  va_end(args);

  abort();
}

Runtime *vm_runtime_new() {
  return vm_runtime_new_impl(VM_DEF_STACK_SIZE, VM_DEF_FRAME_SIZE,
                             VM_MAX_STACK_SIZE, VM_MAX_FRAME_SIZE,
                             VM_GC_THRESHOLD);
}

Runtime *vm_runtime_new_impl(int def_stack_size, int def_frame_size,
                             int max_stack_size, int max_frame_size,
                             int gc_threshold) {
  assert(def_stack_size > 0);
  assert(def_frame_size > 0);
  assert(max_stack_size >= def_stack_size);
  assert(max_frame_size >= def_frame_size);

  assert(gc_threshold > 0);

  Runtime *rt = calloc(1, sizeof(Runtime));
  if (rt == NULL) {
    return NULL;
  }

  rt->max_stack_size = max_stack_size;
  rt->max_frame_count = max_frame_size;

  rt->default_stack_size = def_stack_size;
  rt->default_frame_count = def_frame_size;

  rt->gc_objects = NULL;
  rt->bytes_allocated = 0;
  rt->next_gc_threshold = gc_threshold;

  rt->interned_strings = hash_table_new(); // 初始化全局字符串表

  return rt;
}

void vm_runtime_free(Runtime *rt) {
  if (rt != NULL) {
    hash_table_free(rt->interned_strings); // 释放全局字符串表

    // 释放 rt->gc_objects 链表中的所有对象
    Object *curr = rt->gc_objects;
    while (curr != NULL) {
      Object *next = curr->next;
      vm_gc_free_object(rt, curr);
      curr = next;
    }

    free(rt);
  }
}

Context *vm_context_new(Runtime *rt) {
  Context *ctx = calloc(1, sizeof(Context));
  if (ctx == NULL) {
    return NULL;
  }

  ctx->runtime = rt; // 绑定运行时

  ctx->stack_capacity = rt->default_stack_size;
  ctx->frame_capacity = rt->default_frame_count;

  if (ctx->stack_capacity <= 0 || ctx->frame_capacity <= 0) {
    free(ctx);
    return NULL;
  }

  // 初始化栈和帧
  ctx->stack = calloc(ctx->stack_capacity, sizeof(Value));
  ctx->frames = calloc(ctx->frame_capacity, sizeof(CallFrame));

  ctx->globals = hash_table_new();

  if (ctx->stack == NULL || ctx->frames == NULL || ctx->globals == NULL) {
    free(ctx->stack);
    free(ctx->frames);
    hash_table_free(ctx->globals);
    free(ctx);
    return NULL;
  }

  ctx->sp = 0;
  ctx->frame_count = 0;
  ctx->open_upvalues = NULL;

  return ctx;
}

void vm_context_free(Context *ctx) {
  if (ctx == NULL) {
    return;
  }

  // TODO: 处理闭包和GC?
  hash_table_free(ctx->globals);
  free(ctx->frames);
  free(ctx->stack);
  free(ctx);
}

void vm_stack_push(Context *ctx, Value val) {
  if (ctx->sp >= ctx->stack_capacity) {
    if (ctx->sp >= ctx->runtime->max_stack_size) {
      vm_fatal_error_terminate("Stack Overflow, sp=%d, capacity=%d, max=%d",
                               ctx->sp, ctx->stack_capacity,
                               ctx->runtime->max_stack_size);
    }
    // 扩容栈

    // todo: 审查扩容是否安全，可能的内存泄漏
    ctx->stack_capacity *= 2;
    ctx->stack = realloc(ctx->stack, ctx->stack_capacity * sizeof(Value));
  }
  ctx->stack[ctx->sp++] = val;
}
Value vm_stack_pop(Context *ctx) {
  assert(ctx->sp > 0 && "Stack Underflow");
  return ctx->stack[--ctx->sp];
}
Value vm_stack_peek(Context *ctx, int distance) {
  return ctx->stack[ctx->sp - 1 - distance];
}

void chunk_init(Chunk *chunk) {
  chunk->code = NULL;
  chunk->code_count = 0;
  chunk->code_capacity = 0;
  chunk->constants = NULL;
  chunk->const_count = 0;
  chunk->const_capacity = 0;
}
void chunk_write(Chunk *chunk, uint8_t byte) {
  if (chunk->code_capacity < chunk->code_count + 1) {
    int old_capacity = chunk->code_capacity;
    chunk->code_capacity = old_capacity < 8 ? 8 : old_capacity * 2;
    chunk->code = realloc(chunk->code, chunk->code_capacity * sizeof(uint8_t));
  }
  chunk->code[chunk->code_count++] = byte;
}
int chunk_add_constant(Chunk *chunk, Value value) {
  if (chunk->const_capacity < chunk->const_count + 1) {
    int old_capacity = chunk->const_capacity;
    chunk->const_capacity = old_capacity < 8 ? 8 : old_capacity * 2;
    chunk->constants =
        realloc(chunk->constants, chunk->const_capacity * sizeof(Value));
  }
  chunk->constants[chunk->const_count] = value;
  return chunk->const_count++; // 返回常量所在的索引
}
void chunk_free(Chunk *chunk) {
  free(chunk->code);
  free(chunk->constants);
  chunk_init(chunk);
}

// GC System
void vm_gc_collect(Context *ctx) {
  Runtime *rt = ctx->runtime;
  size_t before_gc = rt->bytes_allocated;

  // ==========================================
  // 标记所有根节点
  // ==========================================

  // 数据栈
  for (int i = 0; i < ctx->sp; i++) {
    vm_gc_mark_value(ctx, ctx->stack[i]);
  }

  // 调用栈帧中的闭包
  for (int i = 0; i < ctx->frame_count; i++) {
    vm_gc_mark_object(ctx, (Object *)ctx->frames[i].closure);
  }

  // 所有的 Open Upvalues
  for (ObjUpvalue *upvalue = ctx->open_upvalues; upvalue != NULL;
       upvalue = upvalue->next) {
    vm_gc_mark_object(ctx, (Object *)upvalue);
  }

  // 全局变量
  if (ctx->globals) {
    for (int i = 0; i < ctx->globals->capacity; i++) {
      HashTableEntry *entry = ctx->globals->buckets[i];
      while (entry != NULL) {
        vm_gc_mark_object(ctx, (Object *)entry->key);
        vm_gc_mark_value(ctx, entry->value);
        entry = entry->next;
      }
    }
  }

  // ==========================================
  // 处理弱引用 (全局字符串常量池)
  // ==========================================

  // todo: 审查是否安全，这里直接修改了哈希表中的数据
  HashTable *strings = rt->interned_strings;
  for (int i = 0; i < strings->capacity; i++) {
    HashTableEntry **prev = &strings->buckets[i];
    while (*prev != NULL) {
      HashTableEntry *entry = *prev;
      if (!entry->key->base.is_marked) {
        // 字符串未被标记，即将被销毁，将其从常量池中摘除
        *prev = entry->next;
        free(entry); // 仅释放Entry本身，ObjString 在 Sweep 阶段统一释放
        strings->count--;
      } else {
        prev = &entry->next;
      }
    }
  }

  // ==========================================
  // 扫描并清除
  // ==========================================
  Object **object_link = &rt->gc_objects;
  while (*object_link != NULL) {
    Object *obj = *object_link;
    if (obj->is_marked) {
      // 保留对象，清除标记等待下次 GC
      obj->is_marked = false;
      object_link = &obj->next;
    } else {
      // 摘除节点并释放内存
      *object_link = obj->next;
      vm_gc_free_object(rt, obj);
    }
  }

  // 调整下一次 GC 触发阈值
  rt->next_gc_threshold = rt->bytes_allocated * 2;

#ifdef VM_DEBUG
  printf("GC Run: %zu bytes before, %zu bytes after.\n", before_gc,
         rt->bytes_allocated);
#endif
}
void vm_gc_mark_value(Context *ctx, Value value) {
  if (value.kind == VAL_OBJ) {
    vm_gc_mark_object(ctx, value.as.obj);
  }
}
void vm_gc_mark_object(Context *ctx, Object *obj) {
  if (obj == NULL || obj->is_marked) {
    return; // 空指针或已标记过，直接跳过防死循环
  }

  obj->is_marked = true;

  // 递归标记其引用的内部对象 (Trace)
  switch (obj->kind) {
  case OBJ_STRING:
    // 字符串没有内部引用
    break;
  case OBJ_ARRAY: {
    ObjArray *arr = (ObjArray *)obj;
    for (int i = 0; i < arr->count; i++) {
      vm_gc_mark_value(ctx, arr->items[i]);
    }
    break;
  }
  case OBJ_MAP: {
    // ObjMap *map = (ObjMap *)obj;
    // todo: 遍历 map->table 标记 key 和 value
    break;
  }
  case OBJ_FUNCTION: {
    ObjFunction *func = (ObjFunction *)obj;
    if (func->name != NULL)
      vm_gc_mark_object(ctx, (Object *)func->name);
    for (int i = 0; i < func->chunk.const_count; i++) {
      vm_gc_mark_value(ctx, func->chunk.constants[i]);
    }
    break;
  }
  case OBJ_CLOSURE: {
    ObjClosure *closure = (ObjClosure *)obj;
    vm_gc_mark_object(ctx, (Object *)closure->function);
    for (int i = 0; i < closure->upvalue_count; i++) {
      vm_gc_mark_object(ctx, (Object *)closure->upvalues[i]);
    }
    break;
  }
  case OBJ_UPVALUE: {
    ObjUpvalue *upvalue = (ObjUpvalue *)obj;
    vm_gc_mark_value(ctx, upvalue->closed);
    break;
  }
  }
}

Object *vm_gc_alloc(Context *ctx, size_t size, ObjectKind kind) {
  Runtime *rt = ctx->runtime;
  assert(rt != NULL);

  // 当前分配的内存大小超过阈值，触发GC
  if (rt->bytes_allocated + size > rt->next_gc_threshold) {
    vm_gc_collect(ctx);
  }

  // 分配内存并初始化对象头
  Object *obj = calloc(1, sizeof(Object));
  if (obj == NULL) {
    vm_fatal_error_terminate("Out of memory.");
  }

  obj->kind = kind;
  obj->is_marked = false;

  // 将对象插入到 VM 的垃圾回收链表头部
  obj->next = rt->gc_objects;
  rt->gc_objects = obj;

  rt->bytes_allocated += size;
  return obj;
}
void vm_gc_free_object(Runtime *rt, Object *obj) {
  assert(obj != NULL);
  switch (obj->kind) {
  case OBJ_STRING: {
    ObjString *str = (ObjString *)obj;
    free(str->data);
    rt->bytes_allocated -= sizeof(ObjString) + str->length + 1;
    break;
  }
  case OBJ_ARRAY: {
    ObjArray *arr = (ObjArray *)obj;
    free(arr->items);
    rt->bytes_allocated -= sizeof(ObjArray) + (arr->capacity * sizeof(Value));
    break;
  }
  case OBJ_MAP: {
    // ObjMap *map = (ObjMap *)obj;
    // hash_table_free(map->table); // todo: 等 map 内部结构完毕后补上
    rt->bytes_allocated -= sizeof(ObjMap);
    break;
  }
  case OBJ_FUNCTION: {
    ObjFunction *func = (ObjFunction *)obj;
    chunk_free(&func->chunk);
    rt->bytes_allocated -= sizeof(ObjFunction);
    break;
  }
  case OBJ_CLOSURE: {
    ObjClosure *closure = (ObjClosure *)obj;
    free(closure->upvalues);
    rt->bytes_allocated -=
        sizeof(ObjClosure) + (closure->upvalue_count * sizeof(ObjUpvalue *));
    break;
  }
  case OBJ_UPVALUE: {
    rt->bytes_allocated -= sizeof(ObjUpvalue);
    break;
  }
  }
  free(obj); // 释放对象内存
}

// object factory
// todo: 因 vm_gc_alloc 可能触发GC，对象可能在构造过程中被回收，需要处理
ObjString *vm_mk_string(Context *ctx, const char *text, int length) {
  // 在全局字符串常量池中查找，如果已经存在，直接返回
  HashTableEntry *entry = hash_table_find_entry_cstr_len(
      ctx->runtime->interned_strings, text, length);
  if (entry != NULL) {
    return entry->key;
  }

  // 否则，创建新的字符串对象
  ObjString *str = (ObjString *)vm_gc_alloc(ctx, sizeof(ObjString), OBJ_STRING);
  str->length = length;
  str->data = malloc(length + 1);
  memcpy(str->data, text, length);
  str->data[length] = '\0';
  str->hash = fnv1a_hash_cstr_len(text, length);

  ctx->runtime->bytes_allocated += length + 1; // 追加字符串所占内存

  // 将字符串对象插入到全局字符串常量池
  hash_table_put(ctx->runtime->interned_strings, str, MK_VAL_NULL());
  return str;
}
ObjArray *vm_mk_array(Context *ctx) {
  ObjArray *arr = (ObjArray *)vm_gc_alloc(ctx, sizeof(ObjArray), OBJ_ARRAY);
  arr->capacity = 0;
  arr->count = 0;
  arr->items = NULL;
  return arr;
}
ObjMap *vm_mk_map(Context *ctx) {
  ObjMap *map = (ObjMap *)vm_gc_alloc(ctx, sizeof(ObjMap), OBJ_MAP);
  // map->table = hash_table_new(); // todo: 等 map 内部结构完毕后补上
  return map;
}
ObjFunction *vm_mk_function(Context *ctx) {
  ObjFunction *func =
      (ObjFunction *)vm_gc_alloc(ctx, sizeof(ObjFunction), OBJ_FUNCTION);
  func->arity = 0;
  func->upvalue_count = 0;
  func->name = NULL;
  chunk_init(&func->chunk);
  return func;
}
ObjClosure *vm_mk_closure(Context *ctx, ObjFunction *func) {
  ObjClosure *closure =
      (ObjClosure *)vm_gc_alloc(ctx, sizeof(ObjClosure), OBJ_CLOSURE);
  closure->function = func;

  // 分配捕获的 Upvalues 数组
  closure->upvalues = calloc(func->upvalue_count, sizeof(ObjUpvalue *));
  closure->upvalue_count = func->upvalue_count;
  ctx->runtime->bytes_allocated += sizeof(ObjUpvalue *) * func->upvalue_count;

  return closure;
}
ObjUpvalue *vm_mk_upvalue(Context *ctx, Value *slot) {
  ObjUpvalue *upvalue =
      (ObjUpvalue *)vm_gc_alloc(ctx, sizeof(ObjUpvalue), OBJ_UPVALUE);
  upvalue->location = slot;
  upvalue->closed = MK_VAL_NULL();
  upvalue->next = NULL;
  return upvalue;
}

InterpretResult vm_run(Context *ctx) {
  CallFrame *frame = &ctx->frames[ctx->frame_count - 1];

  // 寄存器缓存，提升局部性与性能
  // 注意：在进行函数调用(OP_CALL)或可能触发GC的操作前，
  // 必须将局部的 ip 写回 frame->ip 以同步状态 (即: frame->ip = ip;)
  uint8_t *ip = frame->ip;

  // 读取字节并递增 ip
#define READ_BYTE() (*ip++)

// 读取常量并递增 ip
#define READ_CONSTANT() (frame->closure->function->chunk.constants[READ_BYTE()])

// 二进制操作宏
#define BINARY_OP(value_kind, op)                                              \
  do {                                                                         \
    if (vm_stack_peek(ctx, 0).kind != VAL_DOUBLE ||                            \
        vm_stack_peek(ctx, 1).kind != VAL_DOUBLE) {                            \
      vm_fatal_error_terminate("Operands must be numbers.");                   \
      return INTERPRET_RUNTIME_ERROR;                                          \
    }                                                                          \
    double b = vm_stack_pop(ctx).as.d;                                         \
    double a = vm_stack_pop(ctx).as.d;                                         \
    Value res = {.kind = value_kind, .as.d = a op b};                          \
    vm_stack_push(ctx, res);                                                   \
  } while (false)

  // 跳转表的顺序必须与 interpreter.h 中的 OP_Code 枚举顺序绝对一致
  static void *dispatch_table[] = {
      &&do_OP_NOP,

      &&do_OP_LOAD_CONST, &&do_OP_LOAD_NULL,     &&do_OP_LOAD_TRUE,
      &&do_OP_LOAD_FALSE, &&do_OP_POP,           &&do_OP_DUP,

      &&do_OP_GET_GLOBAL, &&do_OP_SET_GLOBAL,    &&do_OP_GET_LOCAL,
      &&do_OP_SET_LOCAL,  &&do_OP_GET_UPVALUE,   &&do_OP_SET_UPVALUE,

      &&do_OP_NEW_MAP,    &&do_OP_NEW_ARRAY,     &&do_OP_GET_PROP,
      &&do_OP_SET_PROP,

      &&do_OP_ADD,        &&do_OP_SUB,           &&do_OP_MUL,
      &&do_OP_DIV,        &&do_OP_MOD,           &&do_OP_NEGATE,
      &&do_OP_INC,        &&do_OP_DEC,

      &&do_OP_EQ,         &&do_OP_NEQ,           &&do_OP_LT,
      &&do_OP_GT,         &&do_OP_LTE,           &&do_OP_GTE,
      &&do_OP_NOT,        &&do_OP_TYPEOF,        &&do_OP_INSTANCEOF,
      &&do_OP_IN,

      &&do_OP_JMP,        &&do_OP_JMP_IF_FALSE,  &&do_OP_LOOP,
      &&do_OP_CALL,       &&do_OP_RETURN,

      &&do_OP_CLOSURE,    &&do_OP_CLOSE_UPVALUE, &&do_OP_THROW};

// 派发宏：读取下一个字节，作为索引去查找 dispatch_table，直接跳转到对应标签
#define DISPATCH() goto *dispatch_table[READ_BYTE()]

  DISPATCH(); // 跳转到初始指令

do_OP_NOP:
  DISPATCH();

// --- 栈操作与常量 ---
do_OP_LOAD_CONST: {
  Value constant = READ_CONSTANT();
  vm_stack_push(ctx, constant);
  DISPATCH();
}
do_OP_LOAD_NULL: {
  Value val = MK_VAL_NULL();
  vm_stack_push(ctx, val);
  DISPATCH();
}
do_OP_LOAD_TRUE: {
  Value val = MK_VAL_BOOL(true);
  vm_stack_push(ctx, val);
  DISPATCH();
}
do_OP_LOAD_FALSE: {
  Value val = MK_VAL_BOOL(false);
  vm_stack_push(ctx, val);
  DISPATCH();
}
do_OP_POP: {
  vm_stack_pop(ctx);
  DISPATCH();
}
do_OP_DUP: {
  vm_stack_push(ctx, vm_stack_peek(ctx, 0));
  DISPATCH();
}

// --- 算术与位运算 ---
do_OP_ADD:
  BINARY_OP(VAL_DOUBLE, +);
  DISPATCH();
do_OP_SUB:
  BINARY_OP(VAL_DOUBLE, -);
  DISPATCH();
do_OP_MUL:
  BINARY_OP(VAL_DOUBLE, *);
  DISPATCH();
do_OP_DIV:
  BINARY_OP(VAL_DOUBLE, /);
  DISPATCH();

do_OP_NEGATE: {
  if (vm_stack_peek(ctx, 0).kind != VAL_DOUBLE) {
    vm_fatal_error_terminate("Operand must be a number.");
    return INTERPRET_RUNTIME_ERROR;
  }
  Value val = vm_stack_pop(ctx);
  val.as.d = -val.as.d;
  vm_stack_push(ctx, val);
  DISPATCH();
}
do_OP_NOT: {
  Value val = vm_stack_pop(ctx);
  // TODO: 处理 Truthy/Falsy 逻辑。Null 和 false 为假，其它待处理。
  bool is_falsey =
      (val.kind == VAL_NULL) || (val.kind == VAL_BOOL && !val.as.b);
  Value res = MK_VAL_BOOL(is_falsey);
  vm_stack_push(ctx, res);
  DISPATCH();
}

// --- 控制流 ---
do_OP_RETURN: {
  Value result = vm_stack_pop(ctx);

  // TODO: 处理 CallFrame 的退栈逻辑
  printf("Result: ");
  if (result.kind == VAL_DOUBLE)
    printf("%g\n", result.as.d);
  else if (result.kind == VAL_INT)
    printf("%d\n", result.as.i);
  else if (result.kind == VAL_BOOL)
    printf("%s\n", result.as.b ? "true" : "false");
  else if (result.kind == VAL_NULL)
    printf("null\n");
  else
    printf("<object %p>\n", (void *)result.as.obj);

  return INTERPRET_OK;
}

// ========================================================
// 以下指令尚未实现 (TODO 区)，暂时拦截避免 Crash
// ========================================================
do_OP_MOD:
do_OP_GET_GLOBAL:
do_OP_SET_GLOBAL:
do_OP_GET_LOCAL:
do_OP_SET_LOCAL:
do_OP_GET_UPVALUE:
do_OP_SET_UPVALUE:
do_OP_NEW_MAP:
do_OP_NEW_ARRAY:
do_OP_GET_PROP:
do_OP_SET_PROP:
do_OP_INC:
do_OP_DEC:
do_OP_EQ:
do_OP_NEQ:
do_OP_LT:
do_OP_GT:
do_OP_LTE:
do_OP_GTE:
do_OP_TYPEOF:
do_OP_INSTANCEOF:
do_OP_IN:
do_OP_JMP:
do_OP_JMP_IF_FALSE:
do_OP_LOOP:
do_OP_CALL:
do_OP_CLOSURE:
do_OP_CLOSE_UPVALUE:
do_OP_THROW:
  vm_fatal_error_terminate("Unimplemented opcode!");
  return INTERPRET_RUNTIME_ERROR;

#undef READ_BYTE
#undef READ_CONSTANT
#undef BINARY_OP
#undef DISPATCH
}