#include "interpreter.h"

#include <assert.h>
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