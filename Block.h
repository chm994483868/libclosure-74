/*
 *  Block.h
 *
 * Copyright (c) 2008-2010 Apple Inc. All rights reserved.
 *
 * @APPLE_LLVM_LICENSE_HEADER@
 *
 */

#ifndef _Block_H_
#define _Block_H_

// 定义外联标识的宏
#if !defined(BLOCK_EXPORT)
#   if defined(__cplusplus)
#       define BLOCK_EXPORT extern "C" 
#   else
#       define BLOCK_EXPORT extern
#   endif
#endif

#include <Availability.h>
#include <TargetConditionals.h>

#if __cplusplus
extern "C" {
#endif

// Create a heap based copy of a Block or simply add a reference to an existing one.
// 创建基于堆的 block 副本，或仅添加对现有 block 的引用。
// 使用时必须与 Block_release 配对使用释放内存。
// This must be paired with Block_release to recover memory, even when running
// under Objective-C Garbage Collection.
BLOCK_EXPORT void *_Block_copy(const void *aBlock)
    __OSX_AVAILABLE_STARTING(__MAC_10_6, __IPHONE_3_2);

// Lose the reference, and if heap based and last reference, recover the memory
// 释放引用，如果是堆 Block 且释放的是最后一个引用，释放引用后并释放内存。
//（类似 ARC 的 release 操作，先是减少引用计数，如果减少到 0 了，则执行 dealloc）
BLOCK_EXPORT void _Block_release(const void *aBlock)
    __OSX_AVAILABLE_STARTING(__MAC_10_6, __IPHONE_3_2);

// Used by the compiler. Do not call this function yourself.
// 由编译器使用，不要自己调用此函数。
// 用于复制 block 捕获到的外部变量，比如当 Block 复制到堆区时也要把它捕获的 __block 变量从栈区复制到堆区
// 说是复制，其实分情况讨论：（等下细节在该函数实现里面分析）
// 1. 如果捕获是对象，则其实复制什么都没做，复制函数里面默认实现是空的。
// 2. 如果捕获的是 block 变量，则调用 _Block_copy
// 3. 如果是 __block 变量，如果牵涉到把它从栈区复制到堆区则比较复杂，
// 调用 _Block_byref_copy 函数来处理
BLOCK_EXPORT void _Block_object_assign(void *, const void *, const int)
    __OSX_AVAILABLE_STARTING(__MAC_10_6, __IPHONE_3_2);

// Used by the compiler. Do not call this function yourself.
// 由编译器使用，不要自己调用此函数。
// 同样根据捕获的不同变量来执行不同的释放操作。（等下细节在该函数实现里面分析）
BLOCK_EXPORT void _Block_object_dispose(const void *, const int)
    __OSX_AVAILABLE_STARTING(__MAC_10_6, __IPHONE_3_2);

// Used by the compiler. Do not use these variables yourself.
// 由编译器使用，不要自己调用此函数。

// 这两个 void* 数组真的没看出来是干什么用的
BLOCK_EXPORT void * _NSConcreteGlobalBlock[32]
    __OSX_AVAILABLE_STARTING(__MAC_10_6, __IPHONE_3_2);

BLOCK_EXPORT void * _NSConcreteStackBlock[32]
    __OSX_AVAILABLE_STARTING(__MAC_10_6, __IPHONE_3_2);

#if __cplusplus
}
#endif

// Type correct macros

// _Block_copy 和 _Block_release 的宏定义
// __VA_ARGS__ 可变参的宏
#define Block_copy(...) ((__typeof(__VA_ARGS__))_Block_copy((const void *)(__VA_ARGS__)))

#define Block_release(...) _Block_release((const void *)(__VA_ARGS__))

#endif
