/*
 * runtime.cpp
 * libclosure
 *
 * Copyright (c) 2008-2010 Apple Inc. All rights reserved.
 *
 * @APPLE_LLVM_LICENSE_HEADER@
 */


#include "Block_private.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <os/assumes.h>
#ifndef os_assumes
#define os_assumes(_x) os_assumes(_x)
#endif
#ifndef os_assert
#define os_assert(_x) os_assert(_x)
#endif

//Vanch:Cannot find _platform_memmove
//#define memmove _platform_memmove

#if TARGET_OS_WIN32
#define _CRT_SECURE_NO_WARNINGS 1
#include <windows.h>
static __inline bool OSAtomicCompareAndSwapLong(long oldl, long newl, long volatile *dst) 
{ 
    // fixme barrier is overkill -- see objc-os.h
    long original = InterlockedCompareExchange(dst, newl, oldl);
    return (original == oldl);
}

static __inline bool OSAtomicCompareAndSwapInt(int oldi, int newi, int volatile *dst) 
{ 
    // fixme barrier is overkill -- see objc-os.h
    int original = InterlockedCompareExchange(dst, newi, oldi);
    return (original == oldi);
}
#else
#define OSAtomicCompareAndSwapLong(_Old, _New, _Ptr) __sync_bool_compare_and_swap(_Ptr, _Old, _New)
#define OSAtomicCompareAndSwapInt(_Old, _New, _Ptr) __sync_bool_compare_and_swap(_Ptr, _Old, _New)
#endif


/*******************************************************************************
Internal Utilities
********************************************************************************/

// 传实参 &aBlock->flags 过来
static int32_t latching_incr_int(volatile int32_t *where) {
    while (1) {
        int32_t old_value = *where;
        // 如果 flags 含有 BLOCK_REFCOUNT_MASK 证明其引用计数达到最大，
        // 直接返回，需要三万多个指针指向，正常情况下不会出现。
        if ((old_value & BLOCK_REFCOUNT_MASK) == BLOCK_REFCOUNT_MASK) {
            return BLOCK_REFCOUNT_MASK;
        }
        
        // 做一次原子性判断其值当前是否被其他线程改动，
        // 如果被改动就进入下一次循环直到改动结束后赋值。
        // OSAtomicCompareAndSwapInt的作用就是在where取值与old_value相同时，
        // 将old_value+2赋给where。
        // 注: Block 的引用计数以 flags 的后 16 位代表，
        // 以 2 为单位，每次递增 2，1 被 BLOCK_DEALLOCATING 正在释放占用。
        if (OSAtomicCompareAndSwapInt(old_value, old_value+2, where)) {
            return old_value+2;
        }
    }
}

static bool latching_incr_int_not_deallocating(volatile int32_t *where) {
    while (1) {
        int32_t old_value = *where;
        if (old_value & BLOCK_DEALLOCATING) {
            // if deallocating we can't do this
            return false;
        }
        if ((old_value & BLOCK_REFCOUNT_MASK) == BLOCK_REFCOUNT_MASK) {
            // if latched, we're leaking this block, and we succeed
            return true;
        }
        if (OSAtomicCompareAndSwapInt(old_value, old_value+2, where)) {
            // otherwise, we must store a new retained value without the deallocating bit set
            return true;
        }
    }
}

// return should_deallocate?
// 实参传入 &aBlock->flags
static bool latching_decr_int_should_deallocate(volatile int32_t *where) {
    while (1) {
        int32_t old_value = *where;
        
        // 如果是引用计数为 0xfffe，返回 false 不做处理
        if ((old_value & BLOCK_REFCOUNT_MASK) == BLOCK_REFCOUNT_MASK) {
            return false; // latched high
        }
        
        // 如果引用计数为 0，返回 false 不做处理
        if ((old_value & BLOCK_REFCOUNT_MASK) == 0) {
            return false;   // underflow, latch low
        }
        
        // 如果大于 2，则将其引用计数 -2 并会在下面返回 false
        int32_t new_value = old_value - 2;
        bool result = false;
        
        // 如果引用计数为 2，将其减 1，为 BLOCK_DEALLOCATING，标明正在释放，返回 true
        if ((old_value & (BLOCK_REFCOUNT_MASK|BLOCK_DEALLOCATING)) == 2) {
            new_value = old_value - 1;
            result = true;
        }

        if (OSAtomicCompareAndSwapInt(old_value, new_value, where)) {
            return result;
        }
    }
}


/**************************************************************************
Framework callback functions and their default implementations.
***************************************************************************/
#if !TARGET_OS_WIN32
#pragma mark Framework Callback Routines
#endif

static void _Block_retain_object_default(const void *ptr __unused) { }

static void _Block_release_object_default(const void *ptr __unused) { }

static void _Block_destructInstance_default(const void *aBlock __unused) {}

static void (*_Block_retain_object)(const void *ptr) = _Block_retain_object_default;
static void (*_Block_release_object)(const void *ptr) = _Block_release_object_default;
static void (*_Block_destructInstance) (const void *aBlock) = _Block_destructInstance_default;


/**************************************************************************
Callback registration from ObjC runtime and CoreFoundation
***************************************************************************/

void _Block_use_RR2(const Block_callbacks_RR *callbacks) {
    _Block_retain_object = callbacks->retain;
    _Block_release_object = callbacks->release;
    _Block_destructInstance = callbacks->destructInstance;
}

/****************************************************************************
Accessors for block descriptor fields
*****************************************************************************/
#if 0
static struct Block_descriptor_1 * _Block_descriptor_1(struct Block_layout *aBlock)
{
    return aBlock->descriptor;
}
#endif

static struct Block_descriptor_2 * _Block_descriptor_2(struct Block_layout *aBlock)
{
    // 用 BLOCK_HAS_COPY_DISPOSE 来判断是否有 Block_descriptor_2
    if (! (aBlock->flags & BLOCK_HAS_COPY_DISPOSE)) return NULL;
    // 因为有无是编译器确定的，在Block结构体中并无保留，
    // 所以需要使用 指针相加 的方式来确定其指针位置。有就执行，没有就 return 掉。
    // 首先找到 descriptor 的位置，向后移动 sizeof(struct Block_descriptor_1)
    // 的长度，
    // 强转，看是否时 Block_descriptor_2 类型
    uint8_t *desc = (uint8_t *)aBlock->descriptor;
    desc += sizeof(struct Block_descriptor_1);
    return (struct Block_descriptor_2 *)desc;
}

static struct Block_descriptor_3 * _Block_descriptor_3(struct Block_layout *aBlock)
{
    if (! (aBlock->flags & BLOCK_HAS_SIGNATURE)) return NULL;
    uint8_t *desc = (uint8_t *)aBlock->descriptor;
    desc += sizeof(struct Block_descriptor_1);
    if (aBlock->flags & BLOCK_HAS_COPY_DISPOSE) {
        desc += sizeof(struct Block_descriptor_2);
    }
    return (struct Block_descriptor_3 *)desc;
}

static void _Block_call_copy_helper(void *result, struct Block_layout *aBlock)
{
    // 这里如果返回找到了 Block_descriptor_2，就执行它的 copy 函数，
    // 如果没有找到就直接返回 return
    // 这个 copy 函数，就是上面的 __main_block_copy_0 函数
    struct Block_descriptor_2 *desc = _Block_descriptor_2(aBlock);
    if (!desc) return;

    (*desc->copy)(result, aBlock); // do fixup
}

static void _Block_call_dispose_helper(struct Block_layout *aBlock)
{
    // 这里同上面
    // 这里如果返回找到了 Block_descriptor_2，就执行它的 dispose 函数，
    // 如果没有找到就直接返回 return
    // 这个 dispose 函数，就是上面的 __main_block_copy_0 函数
    struct Block_descriptor_2 *desc = _Block_descriptor_2(aBlock);
    if (!desc) return;

    (*desc->dispose)(aBlock);
}

/*******************************************************************************
Internal Support routines for copying
********************************************************************************/

#if !TARGET_OS_WIN32
#pragma mark Copy/Release support
#endif

// Copy, or bump refcount, of a block.  If really copying, call the copy helper if present.
void *_Block_copy(const void *arg) {
    // 1. 先声明一个Block_layout结构体类型的指针，如果传入的Block为NULL就直接返回。
    struct Block_layout *aBlock;

    if (!arg) return NULL;
    
    // The following would be better done as a switch statement
    // 以下最好作为 Switch 语句来完成
    
    // 2. 如果Block有值就强转成Block_layout的指针类型。
    aBlock = (struct Block_layout *)arg;
    
    // BLOCK_REFCOUNT_MASK 栈 block
    // BLOCK_NEEDS_FREE 堆 block
    // BLOCK_IS_GLOBAL 全局 block
    // 3. 如果Block的flags表明该 Block为堆Block时，就对其引用计数递增，然后返回原Block。
    if (aBlock->flags & BLOCK_NEEDS_FREE) {
        // latches on high
        latching_incr_int(&aBlock->flags);
        return aBlock;
    }
    // 4. 如果Block为全局Block就不做其他处理直接返回。
    else if (aBlock->flags & BLOCK_IS_GLOBAL) {
        return aBlock;
    }
    else {
        // Its a stack block.  Make a copy.
        // 5. 该else中就是栈Block了，
        // 按原Block的内存大小分配一块相同大小的内存，
        // 如果失败就返回NULL。
        struct Block_layout *result =
            (struct Block_layout *)malloc(aBlock->descriptor->size);
        if (!result) return NULL;
        // 6. memmove()用于复制位元，将aBlock的所有信息copy到result的位置上。
        // memmove 函数，如果旧空间和新空间由交集，那么以新空间为主，复制完毕，旧空间会被破坏
        memmove(result, aBlock, aBlock->descriptor->size); // bitcopy first
#if __has_feature(ptrauth_calls)
        // Resign the invoke pointer as it uses address authentication.
        result->invoke = aBlock->invoke;
#endif
        // reset refcount
        
        // BLOCK_DEALLOCATING =      (0x0001),
        // BLOCK_REFCOUNT_MASK =     (0xfffe),
        // 它们两者 ｜ 一下就是: 0xffff
        
        // 7. 将新Block的引用计数置零。
        // BLOCK_REFCOUNT_MASK|BLOCK_DEALLOCATING就是0xffff，
        // ~(0xffff)就是0x0000，
        // result->flags 与0x0000 与等 就将 result->flags 的后16位置零。
        // 然后将新 Block 标识为堆Block 并将其引用计数置为2。
        result->flags &= ~(BLOCK_REFCOUNT_MASK|BLOCK_DEALLOCATING);    // XXX not needed
        result->flags |= BLOCK_NEEDS_FREE | 2;  // logical refcount 1
        
        // 8. 如果有copy_dispose助手，就执行Block的保存的copy函数，
        // 就是上面的__main_block_copy_0。
        // 在_Block_descriptor_2函数中，
        // 用BLOCK_HAS_COPY_DISPOSE来判断是否有Block_descriptor_2，
        // 且取Block的Block_descriptor_2时，
        // 因为有无是编译器确定的，在Block结构体中并无保留，
        // 所以需要使用指针相加的方式来确定其指针位置。
        // 有就执行，没有就return掉。
        _Block_call_copy_helper(result, aBlock);
        // Set isa last so memory analysis tools see a fully-initialized object.
        // 9. 将堆Block的isa指针置为_NSConcreteMallocBlock，返回新Block，end。
        // 这里 isa 被修正，我们用 clang 转换时显示为是栈区 Block 是不能确认的
        result->isa = _NSConcreteMallocBlock;
        
        return result;
    }
}


// Runtime entry points for maintaining the sharing knowledge of byref data blocks.

// A closure has been copied and its fixup routine is asking us to fix up the reference to the shared byref data
// Closures that aren't copied must still work, so everyone always accesses variables after dereferencing the forwarding ptr.
// We ask if the byref pointer that we know about has already been copied to the heap, and if so, increment and return it.
// Otherwise we need to copy it and update the stack forwarding pointer
static struct Block_byref *_Block_byref_copy(const void *arg) {
    // 3.1 强转入参为(struct Block_byref *)类型。
    struct Block_byref *src = (struct Block_byref *)arg;

    // 当入参为栈 byref 时执行此步，
    // 表示先是栈 Block 有 __block 变量，
    // 此时处于把栈 block 复制到堆中，
    // 此时的操作是把这个 __block 变量复制到堆中去
    if ((src->forwarding->flags & BLOCK_REFCOUNT_MASK) == 0) {
        // src points to stack
        // 3.2 当入参为栈 byref 时执行此步。
        // 分配一份与当前 byref 相同的内存，并将 isa 指针置为 NULL。
        struct Block_byref *copy = (struct Block_byref *)malloc(src->size); // __Block_byref_val_0 这种结构体实例
        copy->isa = NULL;
        
        // byref value 4 is logical refcount of 2: one for caller, one for stack
        // 3.3 将新 byref 的引用计数置为 4 并标记为堆，一份为调用方、一份为栈持有，所以引用计数为4。
        copy->flags = src->flags | BLOCK_BYREF_NEEDS_FREE | 4;
        
        // 3.4 然后将当前 byref 和 malloc 的 byref 的 forwading 都指向 堆byref，然后操作堆栈都是同一份东西。
        // 这两行特关键：
        // 正印证那一句，栈区 Block 的 __block 变量的 __forwarding 指向堆中的 __block 变量
        // 堆中的 __block 的 __forwarding 指向自己
        copy->forwarding = copy; // patch heap copy to point to itself
        src->forwarding = copy;  // patch stack to point to heap copy
        
        // 3.5 最后将size赋值
        copy->size = src->size;

        if (src->flags & BLOCK_BYREF_HAS_COPY_DISPOSE) {
            // Trust copy helper to copy everything of interest
            // If more than one field shows up in a byref block this is wrong XXX
            // 3.6 如果 byref 含有需要内存管理的变量即有 copy_dispose 助手，执行此步。
            // 分别取出 src 的 Block_byref_2 和 copy 的 Block_byref_2。
            struct Block_byref_2 *src2 = (struct Block_byref_2 *)(src+1);
            struct Block_byref_2 *copy2 = (struct Block_byref_2 *)(copy+1);
            // 3.7 将 src 的管理内存函数指针赋值给 copy。
            copy2->byref_keep = src2->byref_keep;
            copy2->byref_destroy = src2->byref_destroy;

            if (src->flags & BLOCK_BYREF_LAYOUT_EXTENDED) {
                // 3.8 如果 src 含有 Block_byref_3，则将 src 的 Block_byref_3 赋值给 copy
                struct Block_byref_3 *src3 = (struct Block_byref_3 *)(src2+1);
                struct Block_byref_3 *copy3 = (struct Block_byref_3*)(copy2+1);
                
                copy3->layout = src3->layout;
            }
            
            // 3.9 执行 byref 的 byref_keep 函数(即 _Block_object_assign，不过会加上 BLOCK_BYREF_CALLER 标记)，管理捕获的对象内存。
            (*src2->byref_keep)(copy, src);
        }
        else {
            // Bitwise copy.
            // This copy includes Block_byref_3, if any.
            // 3.10 如果捕获的是普通变量，就没有 Block_byref_2，copy+1 和src+1 指向的就是 Block_byref_3，执行字节拷贝。
            memmove(copy+1, src+1, src->size - sizeof(*src));
        }
    }
    // already copied to heap
    else if ((src->forwarding->flags & BLOCK_BYREF_NEEDS_FREE) == BLOCK_BYREF_NEEDS_FREE) {
        // 3.11 如果该 byref 是存在于堆，则只需要增加其引用计数。
        latching_incr_int(&src->forwarding->flags);
    }
    
    // 3.12 返回forwarding。
    return src->forwarding;
}

static void _Block_byref_release(const void *arg) {
    // 1.1 将入参强转成(struct Block_byref *)，并将入参替换成 forwarding 。（指向堆中的 __block 变量）
    struct Block_byref *byref = (struct Block_byref *)arg;

    // dereference the forwarding pointer since the compiler isn't doing this anymore (ever?)
    byref = byref->forwarding;
    
    if (byref->flags & BLOCK_BYREF_NEEDS_FREE) {
        // 1.2 如果该byref在堆上执行此步，如果该byref 还被标记为栈则执行断言。
        int32_t refcount = byref->flags & BLOCK_REFCOUNT_MASK;
        os_assert(refcount);
        
        if (latching_decr_int_should_deallocate(&byref->flags)) {
            // 1.3 此函数上面有讲就不多提，判断是否需要释放内存，也可能是只需要减少引用，但是还有别的 block 使用它，此时还不能被废弃
            if (byref->flags & BLOCK_BYREF_HAS_COPY_DISPOSE) {
                // 1.4 如果有 copy_dispose 助手就执行 byref_destroy 管理捕获的变量内存。
                struct Block_byref_2 *byref2 = (struct Block_byref_2 *)(byref+1);
                (*byref2->byref_destroy)(byref);
            }
            // 1.5 释放byref。
            free(byref);
        }
    }
}


/************************************************************
 *
 * API supporting SPI
 * _Block_copy, _Block_release, and (old) _Block_destroy
 *
 ***********************************************************/

#if !TARGET_OS_WIN32
#pragma mark SPI/API
#endif


// API entry point to release a copied Block
// API 入口点以释放复制的 Block
void _Block_release(const void *arg) {
    // 1. 将入参 arg 强转成 (struct Block_layout *) 类型，如果入参为 NULL 则直接返回。
    struct Block_layout *aBlock = (struct Block_layout *)arg;
    if (!aBlock) return;
    
    // 2. 如果入参为全局 Block 则返回不做处理。
    if (aBlock->flags & BLOCK_IS_GLOBAL) return;
    
    // 3. 如果入参不为堆Block则返回不做处理。
    if (! (aBlock->flags & BLOCK_NEEDS_FREE)) return;
    
    // 4. 判断aBlock的引用计数是否需要释放内存。
    // 与copy同样的，latching_decr_int_should_deallocate
    // 也做了一次循环和原子性判断保证原子性。
    // 如果该block的引用计数过高(0xfffe)或者过低(0)返回false不做处理。如果其引用计数为2，
    // 则将其引用计数 -1 即 BLOCK_DEALLOCATING 标明正在释放，返回 true，
    // 如果大于 2 则将其引用计数 -2 并返回 false。
    if (latching_decr_int_should_deallocate(&aBlock->flags)) {
        // 5. 如果上一步骤返回了 ture，标明了该 block 需要被释放，就进入这个 if
        // 如果 aBlock 含有 copy_dispose 助手就执行 aBlock 中的 dispose 函数，
        // 与 copy 中的对应不再多做解释。
        _Block_call_dispose_helper(aBlock);
        // 6. 默认没做其他操作
        // _Block_destructInstance = callbacks->destructInstance;
        _Block_destructInstance(aBlock);
        // 7. 释放 aBlock
        free(aBlock);
    }
}

bool _Block_tryRetain(const void *arg) {
    struct Block_layout *aBlock = (struct Block_layout *)arg;
    return latching_incr_int_not_deallocating(&aBlock->flags);
}

bool _Block_isDeallocating(const void *arg) {
    struct Block_layout *aBlock = (struct Block_layout *)arg;
    return (aBlock->flags & BLOCK_DEALLOCATING) != 0;
}


/************************************************************
 *
 * SPI used by other layers
 *
 ***********************************************************/

size_t Block_size(void *aBlock) {
    return ((struct Block_layout *)aBlock)->descriptor->size;
}

bool _Block_use_stret(void *aBlock) {
    struct Block_layout *layout = (struct Block_layout *)aBlock;

    int requiredFlags = BLOCK_HAS_SIGNATURE | BLOCK_USE_STRET;
    return (layout->flags & requiredFlags) == requiredFlags;
}

// Checks for a valid signature, not merely the BLOCK_HAS_SIGNATURE bit.
bool _Block_has_signature(void *aBlock) {
    return _Block_signature(aBlock) ? true : false;
}

const char * _Block_signature(void *aBlock)
{
    struct Block_layout *layout = (struct Block_layout *)aBlock;
    struct Block_descriptor_3 *desc3 = _Block_descriptor_3(layout);
    if (!desc3) return NULL;

    return desc3->signature;
}

const char * _Block_layout(void *aBlock)
{
    // Don't return extended layout to callers expecting old GC layout
    struct Block_layout *layout = (struct Block_layout *)aBlock;
    if (layout->flags & BLOCK_HAS_EXTENDED_LAYOUT) return NULL;

    struct Block_descriptor_3 *desc3 = _Block_descriptor_3(layout);
    if (!desc3) return NULL;

    return desc3->layout;
}

const char * _Block_extended_layout(void *aBlock)
{
    // Don't return old GC layout to callers expecting extended layout
    struct Block_layout *layout = (struct Block_layout *)aBlock;
    if (! (layout->flags & BLOCK_HAS_EXTENDED_LAYOUT)) return NULL;

    struct Block_descriptor_3 *desc3 = _Block_descriptor_3(layout);
    if (!desc3) return NULL;

    // Return empty string (all non-object bytes) instead of NULL 
    // so callers can distinguish "empty layout" from "no layout".
    if (!desc3->layout) return "";
    else return desc3->layout;
}

#if !TARGET_OS_WIN32
#pragma mark Compiler SPI entry points
#endif

    
/*******************************************************

Entry points used by the compiler - the real API!


A Block can reference four different kinds of things that require help when the Block is copied to the heap.
1) C++ stack based objects
2) References to Objective-C objects
3) Other Blocks
4) __block variables

In these cases helper functions are synthesized by the compiler for use in Block_copy and Block_release, called the copy and dispose helpers.  The copy helper emits a call to the C++ const copy constructor for C++ stack based objects and for the rest calls into the runtime support function _Block_object_assign.  The dispose helper has a call to the C++ destructor for case 1 and a call into _Block_object_dispose for the rest.

The flags parameter of _Block_object_assign and _Block_object_dispose is set to
    * BLOCK_FIELD_IS_OBJECT (3), for the case of an Objective-C Object,
    * BLOCK_FIELD_IS_BLOCK (7), for the case of another Block, and
    * BLOCK_FIELD_IS_BYREF (8), for the case of a __block variable.
If the __block variable is marked weak the compiler also or's in BLOCK_FIELD_IS_WEAK (16)

So the Block copy/dispose helpers should only ever generate the four flag values of 3, 7, 8, and 24.

When  a __block variable is either a C++ object, an Objective-C object, or another Block then the compiler also generates copy/dispose helper functions.  Similarly to the Block copy helper, the "__block" copy helper (formerly and still a.k.a. "byref" copy helper) will do a C++ copy constructor (not a const one though!) and the dispose helper will do the destructor.  And similarly the helpers will call into the same two support functions with the same values for objects and Blocks with the additional BLOCK_BYREF_CALLER (128) bit of information supplied.

So the __block copy/dispose helpers will generate flag values of 3 or 7 for objects and Blocks respectively, with BLOCK_FIELD_IS_WEAK (16) or'ed as appropriate and always 128 or'd in, for the following set of possibilities:
    __block id                   128+3       (0x83)
    __block (^Block)             128+7       (0x87)
    __weak __block id            128+3+16    (0x93)
    __weak __block (^Block)      128+7+16    (0x97)
        

********************************************************/

//
// When Blocks or Block_byrefs hold objects then their copy routine helpers use this entry point to do the assignment.
// 当 Blocks 或者 Block_byrefs 持有对象时，copy routine helpers 使用此入口点进行分配
// 当个 Block 捕获了变量，Block 复制时，其捕获的这些变量也需要复制
/// _Block_object_assign
/// @param destArg 执行 Block_copy() 后的 block 中的对象、block、或者 BYREF 指针的指针 （堆上 block 中的）
/// @param object copy 之前的变量指针 （栈上 block 中）
/// @param flags flags
void _Block_object_assign(void *destArg, const void *object, const int flags) {
    const void **dest = (const void **)destArg;
    
    switch (os_assumes(flags & BLOCK_ALL_COPY_DISPOSE_FLAGS)) {
      case BLOCK_FIELD_IS_OBJECT:
        /*******
        id object = ...;
        [^{ object; } copy];
        ********/
        // 1. 当block捕获的变量为 OC 对象时执行此步，
        // ARC 中引用计数由 强指针 来确定，
        // 所以_Block_retain_object 默认是不做任何操作，只进行简单的指针赋值。
        _Block_retain_object(object);
        *dest = object;
        break;

      case BLOCK_FIELD_IS_BLOCK:
        /*******
        void (^object)(void) = ...;
        [^{ object; } copy];
        ********/
        // 2. 当 block 捕获的变量为另外一个 block 时执行此步，copy 一个新的 block 并赋值。
        *dest = _Block_copy(object);
        break;
    
      case BLOCK_FIELD_IS_BYREF | BLOCK_FIELD_IS_WEAK:
      case BLOCK_FIELD_IS_BYREF:
        /*******
         // copy the onstack __block container to the heap
         // Note this __weak is old GC-weak/MRC-unretained.
         // ARC-style __weak is handled by the copy helper directly.
         __block ... x;
         __weak __block ... x;
         [^{ x; } copy];
         ********/
        // 3. 当 block 捕获的变量为 __block 修饰的变量时会执行此步，执行 byref_copy 操作。
        *dest = _Block_byref_copy(object);
        break;
        
      case BLOCK_BYREF_CALLER | BLOCK_FIELD_IS_OBJECT:
      case BLOCK_BYREF_CALLER | BLOCK_FIELD_IS_BLOCK:
        /*******
         // copy the actual field held in the __block container
         // Note this is MRC unretained __block only. 
         // ARC retained __block is handled by the copy helper directly.
         __block id object;
         __block void (^object)(void);
         [^{ object; } copy];
         ********/
        // 4. 如果管理的是 __block 修饰的对象或者 block 的内存会执行此步，直接进行指针赋值。
        *dest = object;
        break;

      case BLOCK_BYREF_CALLER | BLOCK_FIELD_IS_OBJECT | BLOCK_FIELD_IS_WEAK:
      case BLOCK_BYREF_CALLER | BLOCK_FIELD_IS_BLOCK  | BLOCK_FIELD_IS_WEAK:
        /*******
         // copy the actual field held in the __block container
         // Note this __weak is old GC-weak/MRC-unretained.
         // ARC-style __weak is handled by the copy helper directly.
         __weak __block id object;
         __weak __block void (^object)(void);
         [^{ object; } copy];
         ********/
        // 5. 同时被 __weak和 __block 修饰的对象或者 block 执行此步，也是直接进行指针赋值。
        *dest = object;
        break;

      default:
        break;
    }
}

// 当 Block 释放时，其捕获了变量，此时也需要释放这些被 block 捕获的外部变量

// When Blocks or Block_byrefs hold objects their destroy helper routines call this
// entry point to help dispose of the contents
void _Block_object_dispose(const void *object, const int flags) {
    switch (os_assumes(flags & BLOCK_ALL_COPY_DISPOSE_FLAGS)) {
      case BLOCK_FIELD_IS_BYREF | BLOCK_FIELD_IS_WEAK:
      case BLOCK_FIELD_IS_BYREF:
        // get rid of the __block data structure held in a Block
        // 1. 如果需要管理的变量为 byref，则执行该步。 __block 变量
        _Block_byref_release(object);
        break;
      case BLOCK_FIELD_IS_BLOCK:
        // 2. 如果是 block 则调用 _Block_release 释放 block，上面有讲。
        _Block_release(object);
        break;
      case BLOCK_FIELD_IS_OBJECT:
        // 3. 如果是OC对象就进行release，默认没有做操作，由 ARC 管理。
        _Block_release_object(object);
        break;
      // 4. 如果是其他就不做处理，__block 修饰的变量只有一个强指针引用
      case BLOCK_BYREF_CALLER | BLOCK_FIELD_IS_OBJECT:
      case BLOCK_BYREF_CALLER | BLOCK_FIELD_IS_BLOCK:
      case BLOCK_BYREF_CALLER | BLOCK_FIELD_IS_OBJECT | BLOCK_FIELD_IS_WEAK:
      case BLOCK_BYREF_CALLER | BLOCK_FIELD_IS_BLOCK  | BLOCK_FIELD_IS_WEAK:
        break;
      default:
        break;
    }
}


// Workaround for <rdar://26015603> dylib with no __DATA segment fails to rebase
__attribute__((used))
static int let_there_be_data = 42;
