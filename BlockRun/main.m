//
//  main.m
//  BlockRun
//
//  Created by Vanch on 2020/3/3.
//

#import <Foundation/Foundation.h>

typedef void(^Blk_T)(void);
void (^globalBlock0)(void) = ^{
    NSLog(@"全局区的 block");
};

int main(int argc, const char * argv[]) {
    @autoreleasepool {
        
        // 0. 在全局区定义的 NSGlobalBlock
        NSLog(@"🎉🎉🎉 GlobalBlock0 is %@", globalBlock0);
        
        // 1. 不捕获外部变量时是 NSGlobalBlock。
        //（此处即使发生赋值时 ARC 下会调用 copy，但是由于左值是 NSGlobalBlock，它调用 copy 函数时依然返回它自己）
        void (^globalBlock1)(void) = ^{ };
        NSLog(@"🎉🎉🎉 GlobalBlock1 is %@", globalBlock1);
        
        static int b = 10;
        // 2. 仅捕获外部静态局部变量的是 NSGlobalBlock
        //（此处即使发生赋值时 ARC 下会调用 copy，但是由于左值是 NSGlobalBlock，它调用 copy 函数时依然返回它自己）
        void (^globalBlock2)(void) = ^{
            b = 20;
        };
        NSLog(@"🎉🎉🎉 GlobalBlock2 is %@", globalBlock2);

        int a = 0;
        // 3. 仅捕获外部局部变量是的 NSStackBlock
        NSLog(@"🎉🎉🎉 StackBlock is %@", ^{ NSLog(@"%d", a); });

        // 4. ARC 下 NSStackBlock 赋值给 __strong 变量时发生 copy，创建一个 NSMallocBlock 赋给右值
        // MRC 下编译器不会自动发生 copy，赋值以后右值同样也是 NSStackBlock，如果想实现和 ARC 同样效果需要手动调用 copy
        void (^mallocBlock)(void) = ^{
            NSLog(@"%d", a);
        };
        NSLog(@"🎉🎉🎉 MallocBlock is %@", mallocBlock);
        
        // 5. ARC 或 MRC 下赋值给 __weak/__unsafe_unretained 变量均不发生 copy，
        // 手动调用 copy 是可转为 NSMallocBlock
        // __unsafe_unretained / __weak
        __unsafe_unretained Blk_T mallocBlock2;
        mallocBlock2 = ^{
            NSLog(@"%d", a);
        };
        // mallocBlock2 是：NSStackBlock，其实应该和上面的 StackBlock 写在一起
        NSLog(@"🎉🎉🎉 MallocBlock2 is %@", mallocBlock2);
        
    }
    return 0;
}
