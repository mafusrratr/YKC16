/**
 * 测试进程实现（低Qt耦合版本）
 * BY ZF
 */

#include "test_process.h"
#include <iostream>
#include <unistd.h>

TestProcess::TestProcess()
    : BaseProcess(PROC_PILE_CONTROLLER, "TestProcess")
    , m_counter(0)
{
}

bool TestProcess::doInitialize()
{
    std::cout << "TestProcess::doInitialize" << std::endl;
    
    // BY ZF: 读取配置测试（使用 ConfigManagerLite）
    int gunCount = getConfig().getInt("Charge", "gun_count", 2);
    std::string protocol = getConfig().getString("Charge", "protocol_support", "both");
    
    std::cout << "Config test - gun_count: " << gunCount << " protocol: " << protocol << std::endl;
    
    return true;
}

void TestProcess::doRun()
{
    m_counter++;
    
    if (m_counter % 10 == 0) {  // 每10次循环输出一次
        logMessage(1, "TestProcess running, counter=%d", m_counter);
    }
    
    // 喂狗 - 表示进程正常工作
    feedWatchdog();
    
    // 模拟一些工作
    usleep(500000);  // 500ms
}

void TestProcess::doCleanup()
{
    std::cout << "TestProcess::doCleanup, final counter: " << m_counter << std::endl;
}

