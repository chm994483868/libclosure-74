/*
 * data.c
 * libclosure
 *
 * Copyright (c) 2008-2010 Apple Inc. All rights reserved.
 *
 * @APPLE_LLVM_LICENSE_HEADER@
 *
 */

/********************
NSBlock support

We allocate space and export a symbol to be used as the Class for the on-stack and malloc'ed copies until ObjC arrives on the scene.  These data areas are set up by Foundation to link in as real classes post facto.

We keep these in a separate file so that we can include the runtime code in test subprojects but not include the data so that compiled code that sees the data in libSystem doesn't get confused by a second copy.  Somehow these don't get unified in a common block.
**********************/
// 没看懂上面一段注释是什么意思 😭😭
// 在 Block.h 里面看到外联的使用
// 例如： BLOCK_EXPORT void * _NSConcreteStackBlock[32] __OSX_AVAILABLE_STARTING(__MAC_10_6, __IPHONE_3_2);

// 看定义是定义一个长度是 32 的 void* 数组，并且初始化为 0
void * _NSConcreteStackBlock[32] = { 0 };
void * _NSConcreteMallocBlock[32] = { 0 };
void * _NSConcreteAutoBlock[32] = { 0 };
void * _NSConcreteFinalizingBlock[32] = { 0 };
void * _NSConcreteGlobalBlock[32] = { 0 };
void * _NSConcreteWeakBlockVariable[32] = { 0 };
