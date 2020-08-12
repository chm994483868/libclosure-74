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
// Fn but signed with the given ptrauth key and with the
// address of its storage as extra data.

// StorageSignedFunctionPointer 存储类型为 Fn 的函数指针，
// 但使用给定的 ptrauth 键对其签名并使用其存储地址作为额外数据。

// Function pointers inside block objects are signed this way.
// block 对象内部的函数指针以这种方式签名

// 这个模版类涉及的内容太深了，暂时忽略，好像等看 objc4 的时候还会再遇到！
template <typename Fn, ptrauth_key Key>
class StorageSignedFunctionPointer {
    uintptr_t bits; // unsigned long

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

// 声明一个模版类名
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

// 这里定义的函数指针和模版同名，不会有问题吗？
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
    BLOCK_REFCOUNT_MASK =     (0xfffe),  // runtime // 用来标识栈 Block
    
    BLOCK_NEEDS_FREE =        (1 << 24), // runtime // 用来标识堆 Block
    
    BLOCK_HAS_COPY_DISPOSE =  (1 << 25), // compiler // 编译器有 copy dispose 助手
    // 判断 Block 是否有 copy_dispose 助手 即 description2 中的 copy 和 dispose 函数，用来管理捕获对象的内存
    
    BLOCK_HAS_CTOR =          (1 << 26), // compiler: helpers have C++ code
    BLOCK_IS_GC =             (1 << 27), // runtime
    BLOCK_IS_GLOBAL =         (1 << 28), // compiler // 是否是全局 block
    
    BLOCK_USE_STRET =         (1 << 29), // compiler: undefined if !BLOCK_HAS_SIGNATURE
    
    BLOCK_HAS_SIGNATURE  =    (1 << 30), // compiler
    BLOCK_HAS_EXTENDED_LAYOUT=(1 << 31)  // compiler
};

#define BLOCK_DESCRIPTOR_1 1
struct Block_descriptor_1 { // 常态所有 block 都有这两个值
    uintptr_t reserved; // 保留字段 unsigned long
    uintptr_t size; // block 的大小 unsigned long
};

#define BLOCK_DESCRIPTOR_2 1
struct Block_descriptor_2 {
    // requires BLOCK_HAS_COPY_DISPOSE
    // 需要 flags 是 BLOCK_HAS_COPY_DISPOSE
    BlockCopyFunction copy;
    BlockDisposeFunction dispose;
};

#define BLOCK_DESCRIPTOR_3 1
struct Block_descriptor_3 {
    // requires BLOCK_HAS_SIGNATURE
    // Block 存在延伸布局 ？
    const char *signature;
    const char *layout;     // contents depend on BLOCK_HAS_EXTENDED_LAYOUT
};

struct Block_layout {
    // 指向父类的结构体，
    // 就是 _NSConcreteStackBlock，
    // _NSConcreteMallocBlock，
    // _NSConcreteGlobalBlock 这几个，
    // 说明 Block 本身也是一个 OC 对象
    void *isa;
    
    // 对应的值就是上面的枚举值，用来保留 block 的一些信息
    volatile int32_t flags; // contains ref count
    
    // block 的保留信息
    int32_t reserved;
    
    // 函数指针，指向 block 要执行的函数（即 block 定义中花括号中的表达式）
    BlockInvokeFunction invoke;
    
    // block 附加描述信息，
    struct Block_descriptor_1 *descriptor;
    // 主要保存了内存 size 大小以及 copy 和 dispose 函数的指针及签名和 layout 等信息，
    // 通过源码可发现，layout 中只包含了 Block_descriptor_1，
    // 并未包含 Block_descriptor_2 和 Block_descriptor_3，
    // 这是因为在捕获不同类型变量或者没用到外部变量时，编译器会改变结构体的结构，
    // 按需添加 Block_descriptor_2 和 Block_descriptor_3，
    // 所以才需要 BLOCK_HAS_COPY_DISPOSE 和 BLOCK_HAS_SIGNATURE 等枚举来判断
    
    // imported variables
    
    // capture 的外部变量，
    // 如果 Block 中使用了外部变量，结构体中就会有相应的信息，
    // 如果是 __Block 变量则添加对应结构体类型为其成员变量，非 __block 的则直接添加对应类型的成员变量。
    // Block 将使用的变量或者变量指针 copy 过来，内部才可以访问
};

// Values for Block_byref->flags to describe __block variables
// 作为 Block_byref->flags 的值用于描述 __block 修饰的变量是什么类型

// 结构体 Block_byref 变量在被 __block 修饰时由编译器来生成
enum {
    // Byref refcount must use the same bits as Block_layout's refcount.
    // Byref refcount 必须使用与 Block_layout 的 refcount 相同的位
    
    // BLOCK_DEALLOCATING =      (0x0001),  // runtime
    // BLOCK_REFCOUNT_MASK =     (0xfffe),  // runtime

    BLOCK_BYREF_LAYOUT_MASK =       (0xf << 28), // compiler
    BLOCK_BYREF_LAYOUT_EXTENDED =   (  1 << 28), // compiler // 表示含有 layout
    BLOCK_BYREF_LAYOUT_NON_OBJECT = (  2 << 28), // compiler
    BLOCK_BYREF_LAYOUT_STRONG =     (  3 << 28), // compiler
    BLOCK_BYREF_LAYOUT_WEAK =       (  4 << 28), // compiler
    BLOCK_BYREF_LAYOUT_UNRETAINED = (  5 << 28), // compiler

    BLOCK_BYREF_IS_GC =             (  1 << 27), // runtime

    BLOCK_BYREF_HAS_COPY_DISPOSE =  (  1 << 25), // compiler // 表示 byref 含有 copy dispose 函数，
    // 在 __block 捕获的变量为对象类型时就会生成 copy dispose 函数来管理对象内存
    BLOCK_BYREF_NEEDS_FREE =        (  1 << 24), // runtime // 判断是否需要释放
};

// 结构体 Block_byref，变量在被 __block 修饰时由编译器来生成
struct Block_byref {
    // 指向父类，一般直接指向 0
    void *isa;
    
    struct Block_byref *forwarding;
    // __block 变量在栈中时指向自己，
    // Block 执行 copy 后，
    // 栈中 __block 变量的 __forwarding 指向堆中的 byref（__block 变量），
    // 堆中 __block 变量的 __forwarding 指向自己
    
    // 对应上面的枚举值
    volatile int32_t flags; // contains ref count
    
    // __block 变量结构体所占内存大小
    uint32_t size;
};

struct Block_byref_2 {
    // requires BLOCK_BYREF_HAS_COPY_DISPOSE
    // 含有 copy_dispose
    BlockByrefKeepFunction byref_keep;
    BlockByrefDestroyFunction byref_destroy;
};

struct Block_byref_3 {
    // requires BLOCK_BYREF_LAYOUT_EXTENDED
    // 含有 layout
    const char *layout;
};

// Extended layout encoding.
// 扩展布局编码

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
// 当编译器生成 copy/dispose helpers 时 Runtime 支持的函数

// Values for _Block_object_assign() and _Block_object_dispose() parameters
// 作为 _Block_object_assign() 和 _Block_object_dispose() 函数的参数

enum {
    // see function implementation for a more complete description of these fields and combinations
    
    // OC 对象类型
    BLOCK_FIELD_IS_OBJECT   =  3,  // id, NSObject, __attribute__((NSObject)), block, ...
    // 为一个 Block 变量
    BLOCK_FIELD_IS_BLOCK    =  7,  // a block variable
    // 为一个被 __block 修饰后生成的结构体
    // 持有 __block 变量的堆栈结构
    BLOCK_FIELD_IS_BYREF    =  8,  // the on stack structure holding the __block variable
    // 被 __weak 修饰过的弱引用，只在 Block_byref 管理内部对象内存时使用
    // 也就是 __block __weak id; 仅使用 __weak 时，还是 BLOCK_FIELD_IS_OBJECT，即如果是对象类型，有没有添加 __weak 修饰都是一样的
    BLOCK_FIELD_IS_WEAK     = 16,  // declared __weak, only used in byref copy helpers
    // 在处理 Block_byref 内部对象内存的时候会加一个额外标记，配合上面的枚举一起使用
    BLOCK_BYREF_CALLER      = 128, // called from __block (byref) copy/dispose support routines.
};

enum {
    // 上述情况的整合，即以上都会包含 copy_dispose 助手
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

// 一组外联函数
// runtime entry to get total size of a closure
// 获取 block 完整大小
BLOCK_EXPORT size_t Block_size(void *aBlock);

// indicates whether block was compiled with compiler that sets the ABI related metadata bits
BLOCK_EXPORT bool _Block_has_signature(void *aBlock)
    __OSX_AVAILABLE_STARTING(__MAC_10_7, __IPHONE_4_3);

// returns TRUE if return value of block is on the stack, FALSE otherwise
// 栈区 block 返回 TRUE，其他位置返回 FALSE，判断一个 Block 是否位于栈区
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
    size_t  size; // size == sizeof(struct Block_callbacks_RR)
    void  (*retain)(const void *);
    void  (*release)(const void *);
    void  (*destructInstance)(const void *);
};

typedef struct Block_callbacks_RR Block_callbacks_RR;

BLOCK_EXPORT void _Block_use_RR2(const Block_callbacks_RR *callbacks);

#endif
