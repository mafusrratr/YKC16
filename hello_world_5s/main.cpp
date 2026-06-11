#include <iostream>
#include <unistd.h>

int main()
{
    // BY ZF: 每 5 秒打印一次 helloworl。
    while (true) {
        std::cout << "helloworl" << std::endl;
        sleep(5);
    }

    return 0;
}
