/**
 * 测试进程实现
 * 用于验证BaseProcess基类功能
 * BY ZF
 */

#ifndef TEST_PROCESS_H
#define TEST_PROCESS_H

#include "base_process.h"

class TestProcess : public BaseProcess {
public:
    TestProcess();
    virtual ~TestProcess() {}
    
protected:
    bool doInitialize() override;
    void doRun() override;
    void doCleanup() override;
    
private:
    int m_counter;
};

#endif // TEST_PROCESS_H

