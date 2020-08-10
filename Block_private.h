/*
 * Block_private.h
 *
 * SPI for Blocks
 *
 * Copyright (c) 2008-2010 Apple Inc. All rights reserved.
 *
 * @APPLE_LLVM_LICENSE_HEADER@
 *
 */

#ifndef _BLOCK_PRIVATE_H_
#define _BLOCK_PRIVATE_H_

#include <Availability.h>
#include <AvailabilityMacros.h>
#include <TargetConditionals.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <Block.h>

#if __has_include(<ptrauth.h>)
#include <ptrauth.h>
#endif

#if __has_feature(ptrauth_calls) &&  __cplusplus < 201103L

// C ptrauth or old C++ ptrauth

#define _Block_set_function_pointer(field, value)                       \
    ((value)                                                            \
     ? ((field) =                                                       \
        (__typeof__(field))                                             \
        ptrauth_auth_and_resign((void*)(value),                         \
                                ptrauth_key_function_pointer, 0,        \
                                ptrauth_key_block_function, &(field)))  \
     : ((field) = 0))

#define _Block_get_function_pointer(field)                              \
    ((field)                                                            \
     ? (__typeof__(field))                                              \
       ptrauth_auth_function((void*)(field),                            \
                             ptrauth_key_block_function, &(field))      \
     : (__typeof__(field))0)

#else

// C++11 ptrauth or no ptrauth

#define _Block_set_function_pointer(field, value)       \
    (field) = (value)

#define _Block_get_function_pointer(field)      \
    (field)

#endif


#if __has_feature(ptrauth_calls)  &&  __cplusplus >= 201103L

// StorageSignedFunctionPointer<Key, Fn> stores a function pointer of type
// Fn but signed with the given ptrauth key and with the address of its
// storage as extra data.
// Function pointers inside block objects are signed this way.
template <typename Fn, ptrauth_key Key>
class StorageSignedFunctionPointer {
    uintptr_t bits;

 public:

    // Authenticate function pointer fn as a C function pointer.
    // Re-sign it with our key and the storage address as extra data.
    // DOES NOT actually write to our storage.
    uintptr_t prepareWrite(Fn fn) const
    {
        if (fn == nullptr) {
            return 0;
        } else {
            return (uintptr_t)
                ptrauth_auth_and_resign(fn, ptrauth_key_function_pointer, 0,
                                        Key, &bits);
        }
    }

    // Authenticate otherBits at otherStorage.
    // Re-sign it with our storage address.
    // DOES NOT actually write to our storage.
    uintptr_t prepareWrite(const StorageSignedFunctionPointer& other) const
    {
        if (other.bits == 0) {
            return 0;
        } else {
            return (uintptr_t)
                ptrauth_auth_and_resign((void*)other.bits, Key, &other.bits,
                                        Key, &bits);
        }
    }

    // Authenticate ptr as if it were stored at our storage address.
    // Re-sign it as a C function pointer.
    // DOES NOT actually read from our storage.
    Fn completeReadFn(uintptr_t ptr) const
    {
        if (ptr == 0) {
            return nullptr;
        } else {
            return ptrauth_auth_function((Fn)ptr, Key, &bits);
        }
    }

    // Authenticate ptr as if it were at our storage address.
    // Return it as a dereferenceable pointer.
    // DOES NOT actually read from our storage.
    void* completeReadRaw(uintptr_t ptr) const
    {
        if (ptr == 0) {
            return nullptr;
        } else {
            return ptrauth_auth_data((void*)ptr, Key, &bits);
        }
    }

    StorageSignedFunctionPointer() { }

    StorageSignedFunctionPointer(Fn value)
        : bits(prepareWrite(value)) { }

    StorageSignedFunctionPointer(const StorageSignedFunctionPointer& value)
        : bits(prepareWrite(value)) { }

    StorageSignedFunctionPointer&
    operator = (Fn rhs) {
        bits = prepareWrite(rhs);
        return *this;
    }

    StorageSignedFunctionPointer&
    operator = (const StorageSignedFunctionPointer& rhs) {
        bits = prepareWrite(rhs);
        return *this;
    }

    operator Fn () const {
        return completeReadFn(bits);
    }

    explicit operator void* () const {
        return completeReadRaw(bits);
    }

    explicit operator bool () const {
        return completeReadRaw(bits) != nullptr;
    }
};

using BlockCopyFunction = StorageSignedFunctionPointer
    <void(*)(void *, const void *),
     ptrauth_key_block_function>;

using BlockDisposeFunction = StorageSignedFunctionPointer
    <void(*)(const void *),
     ptrauth_key_block_function>;

using BlockInvokeFunction = StorageSignedFunctionPointer
    <void(*)(void *, ...),
     ptrauth_key_block_function>;

using BlockByrefKeepFunction = StorageSignedFunctionPointer
    <void(*)(struct Block_byref *, struct Block_byref *),
     ptrauth_key_block_function>;

using BlockByrefDestroyFunction = StorageSignedFunctionPointer
    <void(*)(struct Block_byref *),
     ptrauth_key_block_function>;

// c++11 and ptrauth_calls
#elif !__has_feature(ptrauth_calls)
// not ptrauth_calls

typedef void(*BlockCopyFunction)(void *, const void *);
typedef void(*BlockDisposeFunction)(const void *);
typedef void(*BlockInvokeFunction)(void *, ...);
typedef void(*BlockByrefKeepFunction)(struct Block_byref*, struct Block_byref*);
typedef void(*BlockByrefDestroyFunction)(struct Block_byref *);

#else
// ptrauth_calls but not c++11

typedef uintptr_t BlockCopyFunction;
typedef uintptr_t BlockDisposeFunction;
typedef uintptr_t BlockInvokeFunction;
typedef uintptr_t BlockByrefKeepFunction;
typedef uintptr_t BlockByrefDestroyFunction;

#endif


// Values for Block_layout->flags to describe block objects
// 作为 Block_layour->flags 的值用于描述 Block 对象
enum {
    BLOCK_DEALLOCATING =      (0x0001),  // runtime
    BLOCK_REFCOUNT_MASK =     (0xfffe),  // runtime // 用来标识栈 Block // 用来标识Block类型
    BLOCK_NEEDS_FREE =        (1 << 24), // runtime // 用来标识堆 Block // 用来标识Block类型
    BLOCK_HAS_COPY_DISPOSE =  (1 << 25), // compiler // 编译器有 copy dispose 助手 // 判断 Block 是否有 copy_dispose 助手 即 description2 中的 copy 和 dispose 函数，用来管理捕获对象的内存
    BLOCK_HAS_CTOR =          (1 << 26), // compiler: helpers have C++ code
    BLOCK_IS_GC =             (1 << 27), // runtime
    BLOCK_IS_GLOBAL =         (1 << 28), // compiler // 是否是全局 block // 用来标识Block类型
    BLOCK_USE_STRET =         (1 << 29), // compiler: undefined if !BLOCK_HAS_SIGNATURE
    BLOCK_HAS_SIGNATURE  =    (1 << 30), // compiler
    BLOCK_HAS_EXTENDED_LAYOUT=(1 << 31)  // compiler
};

#define BLOCK_DESCRIPTOR_1 1
struct Block_descriptor_1 { // 常规态都有这两个值
    uintptr_t reserved;
    uintptr_t size;
};

#define BLOCK_DESCRIPTOR_2 1
struct Block_descriptor_2 { // 当有使用 __block 变量和捕获外部对象的类型的变量等情况下
    // requires BLOCK_HAS_COPY_DISPOSE
    // 需要 flags 是 BLOCK_HAS_COPY_DISPOSE
    BlockCopyFunction copy;
    BlockDisposeFunction dispose;
};

#define BLOCK_DESCRIPTOR_3 1
struct Block_descriptor_3 {
    // requires BLOCK_HAS_SIGNATURE
    const char *signature;
    const char *layout;     // contents depend on BLOCK_HAS_EXTENDED_LAYOUT
};

struct Block_layout {
    void *isa; // 指向父类的结构体，就是_NSConcreteStackBlock，_NSConcreteMallocBlock，_NSConcreteGlobalBlock这几个，说明OC本身也是一个对象。
    volatile int32_t flags; // contains ref count 包含引用计数 // 就是上面那几个枚举，用来保留 block 的一些信息
    int32_t reserved; // 保留信息
    BlockInvokeFunction invoke; // 函数指针，指向 block 具体的执行函数
    struct Block_descriptor_1 *descriptor; // block 附加描述信息，主要保存了内存 size 以及 copy 和 dispose 函数的指针及签名和 layout 等信息，通过源码可发现，layout 中只包含了 Block_descriptor_1，并未包含 Block_descriptor_2 和 Block_descriptor_3，这是因为在捕获不同类型变量或者没用到外部变量时，编译器会改变结构体的结构，按需添加 Block_descriptor_2 和 Block_descriptor_3，所以才需要 BLOCK_HAS_COPY_DISPOSE 和 BLOCK_HAS_SIGNATURE 等枚举来判断
    // imported variables capture 的外部变量，如果 Block 中使用了外部变量，结构体中就会有相应的信息，下面会解释。Block 将使用的变量或者变量指针 copy 过来，内部才可以访问
};


// Values for Block_byref->flags to describe __block variables
// 作为 Block_byref->flags 的值用于描述 __block 修饰的变量
// 结构体 Block_byref，变量在被 __block 修饰时由编译器来生成
enum {
    // Byref refcount must use the same bits as Block_layout's refcount.
    // BLOCK_DEALLOCATING =      (0x0001),  // runtime
    // BLOCK_REFCOUNT_MASK =     (0xfffe),  // runtime

    BLOCK_BYREF_LAYOUT_MASK =       (0xf << 28), // compiler
    BLOCK_BYREF_LAYOUT_EXTENDED =   (  1 << 28), // compiler
    BLOCK_BYREF_LAYOUT_NON_OBJECT = (  2 << 28), // compiler
    BLOCK_BYREF_LAYOUT_STRONG =     (  3 << 28), // compiler
    BLOCK_BYREF_LAYOUT_WEAK =       (  4 << 28), // compiler
    BLOCK_BYREF_LAYOUT_UNRETAINED = (  5 << 28), // compiler

    BLOCK_BYREF_IS_GC =             (  1 << 27), // runtime

    BLOCK_BYREF_HAS_COPY_DISPOSE =  (  1 << 25), // compiler
    BLOCK_BYREF_NEEDS_FREE =        (  1 << 24), // runtime
};

struct Block_byref {
    void *isa;
    struct Block_byref *forwarding;
    volatile int32_t flags; // contains ref count
    uint32_t size;
};

struct Block_byref_2 {
    // requires BLOCK_BYREF_HAS_COPY_DISPOSE
    BlockByrefKeepFunction byref_keep;
    BlockByrefDestroyFunction byref_destroy;
};

struct Block_byref_3 {
    // requires BLOCK_BYREF_LAYOUT_EXTENDED
    const char *layout;
};


// Extended layout encoding.

// Values for Block_descriptor_3->layout with BLOCK_HAS_EXTENDED_LAYOUT
// and for Block_byref_3->layout with BLOCK_BYREF_LAYOUT_EXTENDED

// If the layout field is less than 0x1000, then it is a compact encoding 
// of the form 0xXYZ: X strong pointers, then Y byref pointers, 
// then Z weak pointers.

// If the layout field is 0x1000 or greater, it points to a 
// string of layout bytes. Each byte is of the form 0xPN.
// Operator P is from the list below. Value N is a parameter for the operator.
// Byte 0x00 terminates the layout; remaining block data is non-pointer bytes.

enum {
    BLOCK_LAYOUT_ESCAPE = 0, // N=0 halt, rest is non-pointer. N!=0 reserved.
    BLOCK_LAYOUT_NON_OBJECT_BYTES = 1,    // N bytes non-objects
    BLOCK_LAYOUT_NON_OBJECT_WORDS = 2,    // N words non-objects
    BLOCK_LAYOUT_STRONG           = 3,    // N words strong pointers
    BLOCK_LAYOUT_BYREF            = 4,    // N words byref pointers
    BLOCK_LAYOUT_WEAK             = 5,    // N words weak pointers
    BLOCK_LAYOUT_UNRETAINED       = 6,    // N words unretained pointers
    BLOCK_LAYOUT_UNKNOWN_WORDS_7  = 7,    // N words, reserved
    BLOCK_LAYOUT_UNKNOWN_WORDS_8  = 8,    // N words, reserved
    BLOCK_LAYOUT_UNKNOWN_WORDS_9  = 9,    // N words, reserved
    BLOCK_LAYOUT_UNKNOWN_WORDS_A  = 0xA,  // N words, reserved
    BLOCK_LAYOUT_UNUSED_B         = 0xB,  // unspecified, reserved
    BLOCK_LAYOUT_UNUSED_C         = 0xC,  // unspecified, reserved
    BLOCK_LAYOUT_UNUSED_D         = 0xD,  // unspecified, reserved
    BLOCK_LAYOUT_UNUSED_E         = 0xE,  // unspecified, reserved
    BLOCK_LAYOUT_UNUSED_F         = 0xF,  // unspecified, reserved
};


// Runtime support functions used by compiler when generating copy/dispose helpers

// Values for _Block_object_assign() and _Block_object_dispose() parameters
enum {
    // see function implementation for a more complete description of these fields and combinations
    BLOCK_FIELD_IS_OBJECT   =  3,  // id, NSObject, __attribute__((NSObject)), block, ...
    BLOCK_FIELD_IS_BLOCK    =  7,  // a block variable
    BLOCK_FIELD_IS_BYREF    =  8,  // the on stack structure holding the __block variable
    BLOCK_FIELD_IS_WEAK     = 16,  // declared __weak, only used in byref copy helpers
    BLOCK_BYREF_CALLER      = 128, // called from __block (byref) copy/dispose support routines.
};

enum {
    BLOCK_ALL_COPY_DISPOSE_FLAGS = 
        BLOCK_FIELD_IS_OBJECT | BLOCK_FIELD_IS_BLOCK | BLOCK_FIELD_IS_BYREF |
        BLOCK_FIELD_IS_WEAK | BLOCK_BYREF_CALLER
};


// Function pointer accessors

static inline __typeof__(void (*)(void *, ...))
_Block_get_invoke_fn(struct Block_layout *block)
{
    return (void (*)(void *, ...))_Block_get_function_pointer(block->invoke);
}

static inline void 
_Block_set_invoke_fn(struct Block_layout *block, void (*fn)(void *, ...))
{
    _Block_set_function_pointer(block->invoke, fn);
}


static inline __typeof__(void (*)(void *, const void *))
_Block_get_copy_fn(struct Block_descriptor_2 *desc)
{
    return (void (*)(void *, const void *))_Block_get_function_pointer(desc->copy);
}

static inline void 
_Block_set_copy_fn(struct Block_descriptor_2 *desc,
                   void (*fn)(void *, const void *))
{
    _Block_set_function_pointer(desc->copy, fn);
}


static inline __typeof__(void (*)(const void *))
_Block_get_dispose_fn(struct Block_descriptor_2 *desc)
{
    return (void (*)(const void *))_Block_get_function_pointer(desc->dispose);
}

static inline void 
_Block_set_dispose_fn(struct Block_descriptor_2 *desc,
                      void (*fn)(const void *))
{
    _Block_set_function_pointer(desc->dispose, fn);
}


// Other support functions


// runtime entry to get total size of a closure
BLOCK_EXPORT size_t Block_size(void *aBlock);

// indicates whether block was compiled with compiler that sets the ABI related metadata bits
BLOCK_EXPORT bool _Block_has_signature(void *aBlock)
    __OSX_AVAILABLE_STARTING(__MAC_10_7, __IPHONE_4_3);

// returns TRUE if return value of block is on the stack, FALSE otherwise
BLOCK_EXPORT bool _Block_use_stret(void *aBlock)
    __OSX_AVAILABLE_STARTING(__MAC_10_7, __IPHONE_4_3);

// Returns a string describing the block's parameter and return types.
// The encoding scheme is the same as Objective-C @encode.
// Returns NULL for blocks compiled with some compilers.
BLOCK_EXPORT const char * _Block_signature(void *aBlock)
    __OSX_AVAILABLE_STARTING(__MAC_10_7, __IPHONE_4_3);

// Returns a string describing the block's GC layout.
// This uses the GC skip/scan encoding.
// May return NULL.
BLOCK_EXPORT const char * _Block_layout(void *aBlock)
    __OSX_AVAILABLE_STARTING(__MAC_10_7, __IPHONE_4_3);

// Returns a string describing the block's layout.
// This uses the "extended layout" form described above.
// May return NULL.
BLOCK_EXPORT const char * _Block_extended_layout(void *aBlock)
    __OSX_AVAILABLE_STARTING(__MAC_10_8, __IPHONE_7_0);

// Callable only from the ARR weak subsystem while in exclusion zone
BLOCK_EXPORT bool _Block_tryRetain(const void *aBlock)
    __OSX_AVAILABLE_STARTING(__MAC_10_7, __IPHONE_4_3);

// Callable only from the ARR weak subsystem while in exclusion zone
BLOCK_EXPORT bool _Block_isDeallocating(const void *aBlock)
    __OSX_AVAILABLE_STARTING(__MAC_10_7, __IPHONE_4_3);


// the raw data space for runtime classes for blocks
// class+meta used for stack, malloc, and collectable based blocks
BLOCK_EXPORT void * _NSConcreteMallocBlock[32]
    __OSX_AVAILABLE_STARTING(__MAC_10_6, __IPHONE_3_2);
BLOCK_EXPORT void * _NSConcreteAutoBlock[32]
    __OSX_AVAILABLE_STARTING(__MAC_10_6, __IPHONE_3_2);
BLOCK_EXPORT void * _NSConcreteFinalizingBlock[32]
    __OSX_AVAILABLE_STARTING(__MAC_10_6, __IPHONE_3_2);
BLOCK_EXPORT void * _NSConcreteWeakBlockVariable[32]
    __OSX_AVAILABLE_STARTING(__MAC_10_6, __IPHONE_3_2);
// declared in Block.h
// BLOCK_EXPORT void * _NSConcreteGlobalBlock[32];
// BLOCK_EXPORT void * _NSConcreteStackBlock[32];


struct Block_callbacks_RR {
    size_t  size;                   // size == sizeof(struct Block_callbacks_RR)
    void  (*retain)(const void *);
    void  (*release)(const void *);
    void  (*destructInstance)(const void *);
};
typedef struct Block_callbacks_RR Block_callbacks_RR;

BLOCK_EXPORT void _Block_use_RR2(const Block_callbacks_RR *callbacks);


#endif
