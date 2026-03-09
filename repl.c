#include <stdio.h>

#include "interpreter.h"

int test_core() {
  printf("--- Booting Virtual Machine ---\n");

  // 初始化隔离区与上下文
  Runtime *rt = vm_runtime_new();
  Context *ctx = vm_context_new(rt);

  // 创建 Chunk (字节码块)
  Chunk chunk;
  chunk_init(&chunk);

  // 制作 Value: 1.5 和 2.0
  Value a = MK_VAL_DOUBLE(1.5);
  Value b = MK_VAL_DOUBLE(2.0);

  // 将常量加入 Chunk，并获取索引
  int a_idx = chunk_add_constant(&chunk, a);
  int b_idx = chunk_add_constant(&chunk, b);

  // 写入指令：LOAD_CONST a, LOAD_CONST b, ADD, RETURN
  chunk_write(&chunk, OP_LOAD_CONST);
  chunk_write(&chunk, a_idx);

  chunk_write(&chunk, OP_LOAD_CONST);
  chunk_write(&chunk, b_idx);

  chunk_write(&chunk, OP_ADD);
  chunk_write(&chunk, OP_RETURN);

  // 伪造一个 ObjFunction 和 ObjClosure 给 CallFrame 使用
  // (因为我们的 VM 假设代码总是在一个函数/闭包中执行)
  ObjFunction func = {0};
  func.chunk = chunk;

  ObjClosure closure = {0};
  closure.function = &func;

  // 将闭包推入调用栈帧 (CallFrame)
  ctx->frames[0].closure = &closure;
  ctx->frames[0].ip = func.chunk.code; // 设置指令指针起始位置
  ctx->frames[0].stack_base = 0;       // 数据栈起点
  ctx->frame_count = 1;

  printf("Executing Bytecode...\n");
  vm_run(ctx); // 预期输出 Result: 3.5

  chunk_free(&chunk);
  vm_context_free(ctx);
  vm_runtime_free(rt);

  printf("--- Virtual Machine Shutdown ---\n");
  return 0;
}

int main(int argc, char **argv) {
  // TODO

  int code = test_core();

  return code;
}