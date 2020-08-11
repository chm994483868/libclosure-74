//
//  main.m
//  BlockRun
//
//  Created by Vanch on 2020/3/3.
//

#import <Foundation/Foundation.h>

int main(int argc, const char * argv[]) {
    @autoreleasepool {
        void (^globalBlock)(void) = ^void {};
        NSLog(@"GlobalBlock is %@", globalBlock);
        
        static int b = 10;
        void (^globalBlock2)(void) = ^void { b = 20; };
        NSLog(@"GlobalBlock2 is %@", globalBlock2);

        int a = 0;
        NSLog(@"StackBlock is %@", ^void { a; });

        void (^mallocBlock)(void) = ^void { a; };
        NSLog(@"MallocBlock is %@", mallocBlock);
        
        // 。
    }
    return 0;
}
