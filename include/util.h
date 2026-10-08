#ifndef UTIL_H
#define UTIL_H

#include <cstdio>
#include <cstdlib>

// 错误检查工具：condition 为真时打印错误并退出。
// 说明：这里直接 exit(EXIT_FAILURE) 是早期实现；生产代码应改为错误码向上传递，
//       由调用方决定降级还是退出。当前保留原行为，只去掉未被使用的辅助函数。
inline void errif(bool condition, const char* errmsg) {
    if (condition) {
        perror(errmsg);
        exit(EXIT_FAILURE);
    }
}

#endif
