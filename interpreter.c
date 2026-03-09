#include "interpreter.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

  Runtime *rt = malloc(sizeof(Runtime));
  if (rt == NULL) {
    return NULL;
  }
  memset(rt, 0, sizeof(Runtime));

  rt->max_stack_size = max_stack_size;
  rt->max_frame_count = max_frame_size;

  rt->default_stack_size = def_stack_size;
  rt->default_frame_count = def_frame_size;

  rt->gc_objects = NULL;
  rt->bytes_allocated = 0;
  rt->next_gc_threshold = gc_threshold;

  return rt;
}

void vm_runtime_free(Runtime *rt) {
  if (rt != NULL) {

    // TODO: 回收/检查所有资源

    free(rt);
  }
}

Context *vm_context_new(Runtime *rt) {
  Context *ctx = malloc(sizeof(Context));
  if (ctx == NULL) {
    return NULL;
  }
  memset(ctx, 0, sizeof(Context));

  ctx->runtime = rt; // 绑定运行时

  ctx->stack_capacity = rt->default_stack_size;
  ctx->frame_capacity = rt->default_frame_count;

  if (ctx->stack_capacity <= 0 || ctx->frame_capacity <= 0) {
    free(ctx);
    return NULL;
  }

  // 初始化栈和帧
  ctx->stack = malloc(ctx->stack_capacity * sizeof(Value));
  ctx->frames = malloc(ctx->frame_capacity * sizeof(CallFrame));

  if (ctx->stack == NULL || ctx->frames == NULL) {
    free(ctx->stack);
    free(ctx->frames);
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

  // TODO: 处理闭包和GC

  free(ctx->frames);
  free(ctx->stack);
  free(ctx);
}

void vm_push(Context *ctx, Value val) {
  if (ctx->sp >= ctx->stack_capacity) {
    if (ctx->sp >= ctx->runtime->max_stack_size) {
      // TODO: 抛出语言异常?
      fprintf(stderr, "Stack Overflow.\n");
      exit(1);
    }
    // 扩容栈
    ctx->stack_capacity *= 2;
    ctx->stack = realloc(ctx->stack, ctx->stack_capacity * sizeof(Value));
  }
  ctx->stack[ctx->sp++] = val;
}
Value vm_pop(Context *ctx) {
  assert(ctx->sp > 0 && "Stack Underflow");
  return ctx->stack[--ctx->sp];
}
Value vm_peek(Context *ctx, int distance) {
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
    if (vm_peek(ctx, 0).kind != VAL_DOUBLE ||                                  \
        vm_peek(ctx, 1).kind != VAL_DOUBLE) {                                  \
      fprintf(stderr, "Operands must be numbers.\n");                          \
      return INTERPRET_RUNTIME_ERROR;                                          \
    }                                                                          \
    double b = vm_pop(ctx).as.d;                                               \
    double a = vm_pop(ctx).as.d;                                               \
    Value res = {.kind = value_kind, .as.d = a op b};                          \
    vm_push(ctx, res);                                                         \
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
  vm_push(ctx, constant);
  DISPATCH();
}
do_OP_LOAD_NULL: {
  Value val = {.kind = VAL_NULL};
  vm_push(ctx, val);
  DISPATCH();
}
do_OP_LOAD_TRUE: {
  Value val = {.kind = VAL_BOOL, .as.b = true};
  vm_push(ctx, val);
  DISPATCH();
}
do_OP_LOAD_FALSE: {
  Value val = {.kind = VAL_BOOL, .as.b = false};
  vm_push(ctx, val);
  DISPATCH();
}
do_OP_POP: {
  vm_pop(ctx);
  DISPATCH();
}
do_OP_DUP: {
  vm_push(ctx, vm_peek(ctx, 0));
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
  if (vm_peek(ctx, 0).kind != VAL_DOUBLE) {
    fprintf(stderr, "Operand must be a number.\n");
    return INTERPRET_RUNTIME_ERROR;
  }
  Value val = vm_pop(ctx);
  val.as.d = -val.as.d;
  vm_push(ctx, val);
  DISPATCH();
}
do_OP_NOT: {
  Value val = vm_pop(ctx);
  // TODO: 处理 Truthy/Falsy 逻辑。Null 和 false 为假，其它待处理。
  bool is_falsey =
      (val.kind == VAL_NULL) || (val.kind == VAL_BOOL && !val.as.b);
  Value res = {.kind = VAL_BOOL, .as.b = is_falsey};
  vm_push(ctx, res);
  DISPATCH();
}

// --- 控制流 ---
do_OP_RETURN: {
  Value result = vm_pop(ctx);

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
  fprintf(stderr, "Unimplemented opcode!\n");
  return INTERPRET_RUNTIME_ERROR;

#undef READ_BYTE
#undef READ_CONSTANT
#undef BINARY_OP
#undef DISPATCH
}