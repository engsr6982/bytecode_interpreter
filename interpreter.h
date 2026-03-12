// 字节码解释器(栈机、C99、GC)
//
// 1. 数据类型
//  1.1 值类型(拷贝传递)
//      null  空值(对于声明的变量, 未赋值时默认为 null)
//      void  无值(函数返回值) null != void
//      bool  布尔值
//      number  数值(int, double)
//
//  1.2 对象类型(引用传递)
//      string  字符串(utf8)
//      array  数组
//      object  对象(键值对, key 为 string, value 为任意类型)
//      function  函数
//
// 2. 关键字
//  2.1 控制流
//      if  条件语句
//      else  条件语句
//      continue  跳过本次循环
//      return  返回值
//      while  循环语句
//      for  循环语句
//      switch  选择语句
//      case  选择语句
//      break  跳出循环/选择语句
//      try  异常处理
//      catch  异常处理
//      finally  异常处理
//      throw  抛出异常
//
//  2.2 变量声明
//      auto  自动推导变量
//          const  常量修饰符(修饰 auto 变量, e.g. auto const a = 1)
//      fn  函数声明
//
//  2.3 类型关键字
//      null  空值类型
//      number  数值类型
//      bool  布尔类型
//          false  布尔值 false
//          true  布尔值 true
//      string  字符串类型(使用 "" 声明字符串字面量)
//      array  数组类型
//      object  对象类型
//      function  函数类型
//
//  2.4 操作符
//      +  加法
//      -  减法
//      *  乘法
//      /  除法
//      %  取模
//      <  小于
//      >  大于
//      <=  小于等于
//      >=  大于等于
//      ==  等于
//      !=  不等于
//      &&  逻辑与
//      ||  逻辑或
//      !  逻辑非
//      =  赋值
//      +=  加赋值
//      -=  减赋值
//      *=  乘赋值
//      /=  除赋值
//      %=  取模赋值
//      ++  自增
//      --  自减
//      ()  函数调用
//      []  数组/对象访问
//      .  对象访问
//      new  创建对象
//      delete  删除对象
//      typeof  获取类型
//      instanceof  判断类型
//      in  判断属性是否存在
//      ... 展开运算符
//      ?  三元运算符
//      :  三元运算符
//
// 3. 作用域
//  作用域决定了变量的可见性和生命周期
//      全局作用域  全局变量
//      局部作用域  函数内部变量
//      块级作用域  {} 内部变量
//      闭包作用域  函数内部定义的函数
//
// 4. 函数
//  4.1 函数声明
//      fn functionName(parameters): returnType { body }
//      fn functionName(parameters) { body } // 返回值类型自动推导
//      fn functionName(): fn(): void {
//          fn doSomething() { body }
//          return doSomething // 返回函数/闭包
//      }
//      fn functionName(...args): void { body } // 变长参数 (args 为 array)
//
//  4.2 函数调用
//      functionName(parameters)
//
//  4.3 函数参数
//      参数类型自动推导
//      参数默认值
//      参数解构
//      参数展开
//
//  4.4 函数返回值
//      return value
//      return // 返回 void
//
// 5. 异常处理
//  5.1 try-catch
//      try { throw "t"; } catch (e) { body }
//      try { body } catch (e) { body } finally { body }
//

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
  VAL_NULL,   // 空值
  VAL_VOID,   // 无值
  VAL_BOOL,   // 布尔值
  VAL_INT,    // 整数
  VAL_DOUBLE, // 浮点数
  VAL_OBJ,    // 对象(引用,指向堆上 Object)
} ValueKind;

typedef struct Object Object; // fwd

typedef struct Value {
  ValueKind kind;

  union {
    bool b;
    int i;
    double d;
    Object *obj;
  } as;
} Value;

typedef enum {
  OBJ_STRING,   // 字符串
  OBJ_ARRAY,    // 数组
  OBJ_MAP,      // 字典
  OBJ_FUNCTION, // 纯函数(字节码+元数据)
  OBJ_CLOSURE,  // 闭包(Function + 捕获的环境变量 Upvalues)
  OBJ_UPVALUE,  // 闭包中的变量
} ObjectKind;

struct Object {
  ObjectKind kind;
  bool is_marked;      // GC Mark 阶段标记位
  struct Object *next; // GC 链表
};

// ==========================================
// 基础对象
// ==========================================
typedef struct {
  Object base;
  int length;    // 字符串长度
  char *data;    // 字符串数据
  uint32_t hash; // 字符串哈希值
} ObjString;

typedef struct {
  Object base;
  Value *items; // 数组元素
  int capacity; // 数组容量
  int count;    // 数组长度
} ObjArray;

typedef struct HashTable HashTable; // fwd

typedef struct {
  Object base;
  // HashTable *table; // 字典 // todo
} ObjMap;

// ==========================================
// 闭包
// ==========================================
typedef struct ObjUpvalue {
  Object base;
  Value *location; // 变量还存活时，指向数据栈上的地址；逃逸后指向下方的 closed
  Value closed;    // 变量离开作用域被销毁时，将值拷贝到此处保存
  struct ObjUpvalue *next; // 虚拟机的打开状态的 Upvalue 链表
} ObjUpvalue;

typedef struct {
  uint8_t *code;     // 字节码
  int code_count;    // 字节码长度
  int code_capacity; // 字节码容量

  Value *constants;   // 常量表 (存放字符串字面量、大数值等)
  int const_count;    // 常量表长度
  int const_capacity; // 常量表容量
} Chunk;

typedef struct {
  Object base;
  int arity;         // 参数个数
  int upvalue_count; // 需要捕获的外层变量个数
  Chunk chunk;       // 对应的字节码
  ObjString *name;   // 函数名(调试)
} ObjFunction;       // 纯函数 (编译期产物，只读)

typedef struct ObjClosure {
  Object base;
  ObjFunction *function; // 指向具体的代码
  ObjUpvalue **upvalues; // 动态数组，存储捕获的 Upvalue 指针
  int upvalue_count;     // 捕获的 Upvalue 个数
} ObjClosure;            // 闭包 (运行期产物)

typedef enum {
  OP_NOP, // 无操作

  // -- 栈操作与常量 --
  OP_LOAD_CONST, // [const_idx] 将常量表中的值压栈 PUSH(consts[idx])
  OP_LOAD_NULL,  // PUSH(null)
  OP_LOAD_TRUE,  // PUSH(true)
  OP_LOAD_FALSE, // PUSH(false)
  OP_POP,        // 弹出栈顶 POP()
  OP_DUP,        // 复制栈顶 PUSH(PEEK())

  // -- 变量操作 --
  OP_GET_GLOBAL, // [name_idx] 根据名字查找全局变量并压栈
  OP_SET_GLOBAL, // [name_idx] 将栈顶赋值给全局变量
  OP_GET_LOCAL,  // [local_idx]
                 // 读取当前栈帧的局部变量：PUSH(stack[frame->stack_base + idx])
  OP_SET_LOCAL,  // [local_idx] 赋值给局部变量
  OP_GET_UPVALUE, // [upvalue_idx] 读取闭包变量
  OP_SET_UPVALUE, // [upvalue_idx] 赋值给闭包变量

  // -- 对象与数组访问 --
  OP_NEW_MAP,   // 创建空对象并压栈
  OP_NEW_ARRAY, // 创建空数组并压栈
  OP_GET_PROP,  // POP key, POP obj, 查找 obj[key] 压栈
  OP_SET_PROP,  // POP val, POP key, POP obj, 设置 obj[key] = val

  // -- 算术与位运算 -- (全部为: b = POP(), a = POP(), PUSH(a OP b))
  OP_ADD,    // a + b
  OP_SUB,    // a - b
  OP_MUL,    // a * b
  OP_DIV,    // a / b
  OP_MOD,    // a % b
  OP_NEGATE, // a = POP(), PUSH(-a)
  OP_INC,    // ++
  OP_DEC,    // --

  // -- 逻辑与比较 --
  OP_EQ,         // a == b
  OP_NEQ,        // a != b
  OP_LT,         // a < b
  OP_GT,         // a > b
  OP_LTE,        // a <= b
  OP_GTE,        // a >= b
  OP_NOT,        // a = POP(), PUSH(!a)
  OP_TYPEOF,     // a = POP(), PUSH(typeof a)
  OP_INSTANCEOF, // a = POP(), b = POP(), PUSH(a instanceof b)
  OP_IN,         // a = POP(), b = POP(), PUSH(a in b)

  // -- 控制流 --
  OP_JMP,          // [offset] 无条件跳转 PC += offset
  OP_JMP_IF_FALSE, // [offset] 如果栈顶是 false，PC += offset
  OP_LOOP,         // [offset] 循环专用后向跳转 PC -= offset
  OP_CALL,         // [arg_count] 调用栈顶的函数
  OP_RETURN,       // 返回栈顶的值给上一层调用者

  // -- 闭包与异常 --
  OP_CLOSURE,       // [func_idx] 把函数实例化为闭包并压栈
  OP_CLOSE_UPVALUE, // 当变量离开作用域时，将其移入堆中(闭包逃逸)
  OP_THROW,         // 抛出栈顶作为异常
} OP_Code;          // 指令集

// ==========================================
// 异常与调用帧
// ==========================================
typedef struct {
  int try_start_pc; // try 块在字节码中的起始 PC
  int try_end_pc;   // try 块结束 PC
  int catch_pc;     // catch 块的入口 PC
  int finally_pc;   // finally 块的入口 PC (如果没有则为 -1)
  int stack_base;   // 记录进入 try 时的栈深度，用于 unwind
} ExceptionHandler;

typedef struct CallFrame {
  ObjClosure *closure; // 闭包

  uint8_t *ip;    // 指令指针
  int stack_base; // 栈帧基地址(当前帧在 Context.stack 上的起始索引)

  ExceptionHandler *handlers; // 异常处理
  int handler_count;          // 异常处理数量
} CallFrame;

// ==========================================
// 哈希桶
// ==========================================
typedef struct HashTableEntry {
  ObjString *key; // 键
  Value value;    // 值

  struct HashTableEntry *next; // 链表
} HashTableEntry;

struct HashTable {
  HashTableEntry **buckets; // 哈希桶(指针数组,每个元素为链表头指针)
  int count;                // 当前元素个数
  int capacity;             // 桶的数量
};

size_t fnv1a_hash_cstr(const char *key);
size_t fnv1a_hash_cstr_len(const char *key, int len);
size_t fnv1a_hash_obj_str(ObjString *key);
void vm_ensure_obj_str_hashed(ObjString *key);

HashTable *hash_table_new();
void hash_table_free(HashTable *tab);
bool hash_table_clear(HashTable *tab);

bool hash_table_get(HashTable *tab, ObjString *key, Value *out);

bool hash_table_put(HashTable *tab, ObjString *key, Value value);

void hash_table_remove(HashTable *tab, ObjString *key);

// ==========================================
// 运行时
// ==========================================
typedef struct Runtime {
  Object *gc_objects;       // GC 链表
  size_t bytes_allocated;   // 已分配的字节数
  size_t next_gc_threshold; // 下一次 GC 的阈值

  int max_stack_size;  // 栈的最大容量
  int max_frame_count; // 调用栈的最大深度

  int default_stack_size;  // 默认栈大小(初始化)
  int default_frame_count; // 默认调用栈深度(初始化)

  HashTable *interned_strings; // 全局字符串常量池

  // GC: 灰色对象栈
  Object **grey_stack;
  int grey_count;    // GC: 灰色对象栈数量
  int grey_capacity; // GC: 灰色对象栈容量
  int object_count;  // 对象数量
} Runtime;

typedef struct Context {
  Runtime *runtime; // 运行时(绑定的宿主)

  HashTable *globals; // 全局环境

  Value *stack;       // 数据栈(堆, calloc(stack_capacity, sizeof(Value))
  int stack_capacity; // 栈容量
  int sp;             // 栈指针, 指向下一个可用的栈帧(空)

  CallFrame *frames;  // 调用栈
  int frame_capacity; // 调用栈容量
  int frame_count;    // 调用栈深度

  ObjUpvalue
      *open_upvalues; // 当前还在数据栈上，但被内层闭包引用的所有 Upvalue 链表
} Context;

// =========================================
// 运行时
// =========================================

#define VM_DEF_STACK_SIZE 256
#define VM_DEF_FRAME_SIZE 32
#define VM_MAX_STACK_SIZE 8192
#define VM_MAX_FRAME_SIZE 1024
#define VM_GC_THRESHOLD 1024 * 1024 // 1MB

#define MK_VAL_VOID() ((Value){.kind = VAL_VOID})
#define MK_VAL_NULL() ((Value){.kind = VAL_NULL})
#define MK_VAL_BOOL(BOOL) ((Value){.kind = VAL_BOOL, .as.b = BOOL})
#define MK_VAL_INT(I32) ((Value){.kind = VAL_INT, .as.i = I32})
#define MK_VAL_DOUBLE(DOUBLE) ((Value){.kind = VAL_DOUBLE, .as.d = DOUBLE})
#define MK_VAL_OBJ(OBJ_PTR)                                                    \
  ((Value){.kind = VAL_OBJ, .as.obj = (Object *)(OBJ_PTR)})

bool vm_is_void(Value val) { return val.kind == VAL_VOID; }
bool vm_is_null(Value val) { return val.kind == VAL_NULL; }
bool vm_is_bool(Value val) { return val.kind == VAL_BOOL; }
bool vm_is_integer(Value val) { return val.kind == VAL_INT; }
bool vm_is_double(Value val) { return val.kind == VAL_DOUBLE; }
bool vm_is_number(Value val) {
  return val.kind == VAL_INT || val.kind == VAL_DOUBLE;
}

void vm_fatal_error_abort(const char *fmt, ...);

/**
 * 创建一个运行时实例
 * @note 默认使用宏定义的默认值
 */
Runtime *vm_runtime_new();

Runtime *vm_runtime_new_impl(int def_stack_size, int def_frame_size,
                             int max_stack_size, int max_frame_size,
                             int gc_threshold);

void vm_runtime_free(Runtime *rt);

Context *vm_context_new(Runtime *rt);

void vm_context_free(Context *ctx);

void vm_stack_push(Context *ctx, Value val);
Value vm_stack_pop(Context *ctx);
Value vm_stack_peek(Context *ctx, int distance);

void chunk_init(Chunk *chunk);
void chunk_write(Chunk *chunk, uint8_t byte);
int chunk_add_constant(Chunk *chunk, Value value);
void chunk_free(Chunk *chunk);

// GC System
void vm_gc_collect(Context *ctx);
void vm_gc_mark_value(Context *ctx, Value value);
void vm_gc_mark_object(Context *ctx, Object *obj);

void vm_memory_adjust(Runtime *rt, ptrdiff_t bytes);

Object *vm_gc_alloc(Context *ctx, size_t size, ObjectKind kind);
void vm_gc_free_object(Runtime *rt, Object *obj);

// object factory
ObjString *vm_mk_string(Context *ctx, const char *text, int length);
ObjArray *vm_mk_array(Context *ctx);
ObjMap *vm_mk_map(Context *ctx);
ObjFunction *vm_mk_function(Context *ctx);
ObjClosure *vm_mk_closure(Context *ctx, ObjFunction *func);
ObjUpvalue *vm_mk_upvalue(Context *ctx, Value *slot);

typedef enum {
  INTERPRET_OK,
  INTERPRET_COMPILE_ERROR,
  INTERPRET_RUNTIME_ERROR
} InterpretResult;

InterpretResult vm_run(Context *ctx);
