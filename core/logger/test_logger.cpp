/**
 * 日志系统测试程序
 * BY ZF
 */

#include <iostream>
#include <unistd.h>
#include <signal.h>
#include <ctime>
#include <cstdio>
#include <chrono>
#include <thread>
#include <vector>
#include <atomic>
#include <iomanip>
#include "../base/logger/log_sender.h"
#include "../base/common/logger_types.h"
#include "../../libv2gshm/libcshm/v2gshm.h"

bool g_running = true;

void signalHandler(int sig) {
    std::cout << "Test process received signal: " << sig << std::endl;
    g_running = false;
}

int main(int argc, char* argv[]) {
    std::cout << "=== Logger Test Process ===" << std::endl;
    std::cout << "Version: 1.0.0" << std::endl;
    std::cout << "Build: " << __DATE__ << " " << __TIME__ << std::endl;
    
    // 设置信号处理
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    // 创建日志发送器
    LogSender logger("test_process");
    
    std::cout << "Starting log test..." << std::endl;
    
    int counter = 0;
    while (g_running) {
        // 发送不同级别的日志
        logger.info("Test log message", "Counter: " + std::to_string(counter));
        
        if (counter % 5 == 0) {
            logger.warn("Warning message", "This is a warning");
        }
        
        if (counter % 10 == 0) {
            logger.error("Error message", "This is an error");
        }
        
        counter++;
        sleep(2);
        
        if (counter >= 10) {
            std::cout << "Test completed, exiting..." << std::endl;
            break;
        }
    }
    
    // // 追加：trade_record 测试（合法与非法各一条）
    // {
    //     std::cout << "Sending valid trade_record..." << std::endl;
    //     TradeRecord rec;
    //     rec.gunNo = 1;
    //     rec.preTradeNo = "PRE123";
    //     rec.tradeNo = std::string("TNO_VALID_") + std::to_string(time(nullptr));
    //     rec.vinCode = "VIN123456789";
    //     rec.timeDivType = 1;
    //     rec.startType = 1;
    //     rec.chargeStartTime = 1730265600;
    //     rec.chargeEndTime = 1730269200;
    //     rec.startSoc = 20;
    //     rec.endSoc = 80;
    //     rec.reason = 0;
    //     rec.feeModelId = "FM01";
    //     rec.sumStart = 1000;
    //     rec.sumEnd = 1250;
    //     rec.totalElect = 2500;
    //     rec.totalPowerCost = 1000;
    //     rec.totalServCost = 300;
    //     rec.totalCost = 1300;
    //     rec.timeNum = 3;
    //     rec.partElect = {10, 20, 30};
    //     rec.chargeFee = {100, 200, 300};
    //     rec.serviceFee = {40, 50, 60};
    //     rec.startPoint = 0;
    //     rec.crossPoints = 1;
    //     rec.pointsElect = {99};
    //     rec.cardNumber = "CARD0001";
    //     logger.logTradeRecord(rec);
    // }

    // {
    //     std::cout << "Sending invalid trade_record (mismatch time_num)..." << std::endl;
    //     TradeRecord rec;
    //     rec.gunNo = 1;
    //     rec.tradeNo = std::string("TNO_INVALID_") + std::to_string(time(nullptr));
    //     rec.timeNum = 3; // 声明3段
    //     rec.partElect = {10, 20}; // 实际2段，触发校验失败
    //     rec.chargeFee = {100, 200, 300};
    //     rec.serviceFee = {40, 50, 60};
    //     rec.crossPoints = 1;
    //     rec.pointsElect = {77};
    //     logger.logTradeRecord(rec); // 应被拒绝入库，并产生warn日志
    // }

    // BY ZF: 计费模型测试（合法与非法各一条）
    // {
        // std::cout << "Sending valid fee_model..." << std::endl;
        // FeeModel model;
        // model.feeModelId = std::string("FM_TEST_") + std::to_string(time(nullptr));
        // model.timeNum = 3;
        // model.timeSeg = {"0000", "0630", "1200"};  // 3个时段：0点、6:30、12:00
        // model.segFlag = {1, 2, 3};  // 可选时段标志
        // model.chargeFee = {100, 200, 300};  // 电费（0.001元）
        // model.serviceFee = {50, 100, 150};  // 服务费（0.001元）
        // logger.saveFeeModel(model);
    // }

    // {
    //     std::cout << "Sending invalid fee_model (mismatch time_num)..." << std::endl;
    //     FeeModel model;
    //     model.feeModelId = std::string("FM_INVALID_") + std::to_string(time(nullptr));
    //     model.timeNum = 4; // 声明4段
    //     model.timeSeg = {"0000", "0630", "1200"};  // 实际3段，触发校验失败
    //     model.segFlag = {1, 2, 3, 4};  // 4段，但 timeSeg 只有3段
    //     model.chargeFee = {100, 200, 300, 400};
    //     model.serviceFee = {50, 100, 150, 200};
    //     logger.saveFeeModel(model); // 应被拒绝入库，并产生warn日志
    // }

    // {
    //     std::cout << "Sending invalid fee_model (segFlag mismatch)..." << std::endl;
    //     FeeModel model;
    //     model.feeModelId = std::string("FM_INVALID_SEGFLAG_") + std::to_string(time(nullptr));
    //     model.timeNum = 3; // 声明3段
    //     model.timeSeg = {"0000", "0630", "1200"};  // 3段
    //     model.segFlag = {1, 2};  // 只有2段，触发校验失败
    //     model.chargeFee = {100, 200, 300};
    //     model.serviceFee = {50, 100, 150};
    //     logger.saveFeeModel(model); // 应被拒绝入库，并产生warn日志
    // }

    // {
    //     std::cout << "Sending valid fee_model (max 96 segments)..." << std::endl;
    //     FeeModel model;
    //     model.feeModelId = std::string("FM_MAX_") + std::to_string(time(nullptr));
    //     model.timeNum = 96; // 最大时段数
    //     // 生成96个时段（每15分钟一个时段）
    //     for (int i = 0; i < 96; ++i) {
    //         int hour = (i * 15) / 60;
    //         int minute = (i * 15) % 60;
    //         char seg[5];
    //         snprintf(seg, sizeof(seg), "%02d%02d", hour, minute);
    //         model.timeSeg.push_back(seg);
    //         model.segFlag.push_back(i % 3 + 1);  // BY ZF: segFlag 也必须与 timeNum 一致
    //         model.chargeFee.push_back(100 + i);
    //         model.serviceFee.push_back(50 + i);
    //     }
    //     logger.saveFeeModel(model);
    // }

    // // BY ZF: 等待5秒让 logger 处理并更新共享内存
    // std::cout << "\nWaiting 5 seconds for logger to process fee_model..." << std::endl;
    // sleep(5);

    // // BY ZF: 测试从共享内存读取计费模型
    // {
    //     std::cout << "\n=== Loading fee_model from shared memory ===" << std::endl;
    //     CShm shm;
    //     if (shm.init()) {
    //         _evs_service_issue_feeModel* model = shm.evs_getFeeModel();
    //         if (model && model->timeNum > 0) {
    //             std::cout << "✓ Successfully loaded fee_model from shared memory!" << std::endl;
    //             std::cout << "  FeeModel ID: " << model->feeModelId << std::endl;
    //             std::cout << "  Time Num: " << (int)model->timeNum << std::endl;
                
    //             // 打印时段信息（只显示前10个和最后3个）
    //             std::cout << "  Time Segments (first 10 and last 3):" << std::endl;
    //             for (int i = 0; i < model->timeNum; ++i) {
    //                 if (i < 10 || i >= model->timeNum - 3) {
    //                     std::cout << "    [" << i << "] " << model->timeSeg[i];
    //                     std::cout << " -> Charge: " << model->chargeFee[i];
    //                     std::cout << ", Service: " << model->serviceFee[i];
    //                     std::cout << std::endl;
    //                 } else if (i == 10) {
    //                     std::cout << "    ..." << std::endl;
    //                 }
    //             }
                
    //             std::cout << "  Total segments: " << (int)model->timeNum << std::endl;
    //         } else {
    //             std::cout << "✗ No fee_model found in shared memory" << std::endl;
    //         }
    //     } else {
    //         std::cout << "✗ Failed to initialize shared memory" << std::endl;
    //     }
    // }

    // // BY ZF: 压力测试部分
    // std::cout << "\n=== Stress Test ===" << std::endl;
    // std::cout << "Press Enter to start stress test, or Ctrl+C to exit..." << std::endl;
    // std::cin.get();
    
    // // BY ZF: 测试1 - 大量日志消息发送（控制频率：每100毫秒发送100条，适合嵌入式系统）
    // {
    //     std::cout << "\n[Stress Test 1] Sending 10000 log messages (100 messages per 100ms)..." << std::endl;
    //     auto startTime = std::chrono::high_resolution_clock::now();
        
    //     const int testCount = 10000;
    //     const int batchSize = 100;  // BY ZF: 每批发送100条
    //     const int batchIntervalMs = 200;  // BY ZF: 每批间隔200毫秒（适合嵌入式系统）
        
    //     LogSender stressLogger("StressTest");
    //     auto lastBatchTime = std::chrono::steady_clock::now();
        
    //     for (int i = 0; i < testCount; ++i) {
    //         stressLogger.info("Message " + std::to_string(i), 
    //                    "Detail info for message " + std::to_string(i));
            
    //         // BY ZF: 每发送batchSize条消息后，等待batchIntervalMs毫秒
    //         if ((i + 1) % batchSize == 0) {
    //             auto now = std::chrono::steady_clock::now();
    //             auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
    //                 now - lastBatchTime).count();
                
    //             if (elapsed < batchIntervalMs) {
    //                 std::this_thread::sleep_for(
    //                     std::chrono::milliseconds(batchIntervalMs - elapsed));
    //             }
    //             lastBatchTime = std::chrono::steady_clock::now();
    //         }
            
    //         if ((i + 1) % 1000 == 0) {
    //             std::cout << "  Sent " << (i + 1) << " messages..." << std::endl;
    //         }
    //     }
        
    //     auto endTime = std::chrono::high_resolution_clock::now();
    //     auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
    //         endTime - startTime).count();
        
    //     double msgPerSec = (testCount * 1000.0) / duration;
    //     std::cout << "  ✓ Completed: " << testCount << " messages in " 
    //               << duration << " ms" << std::endl;
    //     std::cout << "  ✓ Throughput: " << std::fixed << std::setprecision(2) 
    //               << msgPerSec << " messages/second" << std::endl;
    //     std::cout << "  ✓ Target rate: " << (batchSize * 1000.0 / batchIntervalMs) 
    //               << " messages/second (100 messages per " << batchIntervalMs << "ms)" << std::endl;
    // }
    
    // // BY ZF: 等待处理完成
    // std::cout << "\nWaiting 3 seconds for logger to process..." << std::endl;
    // sleep(3);
    
    // BY ZF: 烤机测试 - 持续发送日志（每秒500条），直到Ctrl+C中断
    {
        std::cout << "\n[烤机测试] 持续发送日志（70条/秒），按 Ctrl+C 停止..." << std::endl;
        std::cout << "开始时间: " << std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count() << std::endl;
        
        LogSender burnInLogger("BurnInTest");
        const int batchSize = 7;  // BY ZF: 每批发送20条
        const int batchIntervalMs = 100;  // BY ZF: 每批间隔100毫秒（10条/100ms = 100条/秒）
        
        std::atomic<long long> totalSent(0);
        auto testStartTime = std::chrono::high_resolution_clock::now();
        auto lastBatchTime = std::chrono::steady_clock::now();
        auto lastStatsTime = std::chrono::high_resolution_clock::now();
        long long batchCount = 0;
        
        std::cout << "发送速率: 100条/秒 (每200ms发送100条)" << std::endl;
        std::cout << "按 Ctrl+C 停止测试..." << std::endl;
        
        while (g_running) {
            // BY ZF: 发送一批日志
            for (int i = 0; i < batchSize && g_running; ++i) {
                burnInLogger.info("Burn-in test message " + std::to_string(totalSent.load() + i),
                                 "Continuous stress test - batch " + std::to_string(batchCount) + 
                                 ", message " + std::to_string(i));
            }
            
            totalSent += batchSize;
            batchCount++;
            
            // BY ZF: 控制发送频率（每200ms发送一批）
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - lastBatchTime).count();
            
            if (elapsed < batchIntervalMs) {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(batchIntervalMs - elapsed));
            }
            lastBatchTime = std::chrono::steady_clock::now();
            
            // BY ZF: 每5秒显示一次统计信息
            auto currentTime = std::chrono::high_resolution_clock::now();
            auto statsElapsed = std::chrono::duration_cast<std::chrono::seconds>(
                currentTime - lastStatsTime).count();
            
            if (statsElapsed >= 5) {
                auto totalElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    currentTime - testStartTime).count();
                double actualRate = (totalSent.load() * 1000.0) / totalElapsed;
                double runtimeSeconds = totalElapsed / 1000.0;
                
                std::cout << "[统计] 已运行: " << std::fixed << std::setprecision(1) 
                          << runtimeSeconds << "秒, 已发送: " << totalSent.load() 
                          << "条, 实际速率: " << std::setprecision(2) << actualRate 
                          << "条/秒, 批次: " << batchCount << std::endl;
                
                lastStatsTime = currentTime;
            }
        }
        
        // BY ZF: 显示最终统计
        auto endTime = std::chrono::high_resolution_clock::now();
        auto totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
            endTime - testStartTime).count();
        double finalRate = (totalSent.load() * 1000.0) / totalDuration;
        double finalRuntimeSeconds = totalDuration / 1000.0;
        
        std::cout << "\n[烤机测试结束]" << std::endl;
        std::cout << "  ✓ 总运行时间: " << std::fixed << std::setprecision(1) 
                  << finalRuntimeSeconds << " 秒" << std::endl;
        std::cout << "  ✓ 总发送数量: " << totalSent.load() << " 条" << std::endl;
        std::cout << "  ✓ 平均速率: " << std::setprecision(2) << finalRate 
                  << " 条/秒" << std::endl;
        std::cout << "  ✓ 总批次数: " << batchCount << " 批" << std::endl;
    }
    
    // // BY ZF: 测试2 - 多线程并发发送日志
    // {
    //     std::cout << "\n[Stress Test 2] Multi-threaded concurrent log sending..." << std::endl;
    //     const int threadCount = 5;
    //     const int messagesPerThread = 1000;
    //     std::atomic<int> totalSent(0);
    //     std::vector<std::thread> threads;
        
    //     auto startTime = std::chrono::high_resolution_clock::now();
        
    //     for (int t = 0; t < threadCount; ++t) {
    //         threads.emplace_back([t, messagesPerThread, &totalSent]() {
    //             LogSender threadLogger("thread_" + std::to_string(t));
    //             for (int i = 0; i < messagesPerThread; ++i) {
    //                 threadLogger.info("Thread " + std::to_string(t) + " message " + std::to_string(i),
    //                                  "Concurrent stress test");
    //                 totalSent++;
    //             }
    //         });
    //     }
        
    //     // 等待所有线程完成
    //     for (auto& thread : threads) {
    //         thread.join();
    //     }
        
    //     auto endTime = std::chrono::high_resolution_clock::now();
    //     auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
    //         endTime - startTime).count();
        
    //     double msgPerSec = (totalSent.load() * 1000.0) / duration;
    //     std::cout << "  ✓ Completed: " << totalSent.load() << " messages from " 
    //               << threadCount << " threads in " << duration << " ms" << std::endl;
    //     std::cout << "  ✓ Throughput: " << std::fixed << std::setprecision(2) 
    //               << msgPerSec << " messages/second" << std::endl;
    // }
    
    // // BY ZF: 等待处理完成
    // std::cout << "\nWaiting 3 seconds for logger to process..." << std::endl;
    // sleep(3);
    
    // // BY ZF: 测试3 - 大量TradeRecord发送（控制频率：每200毫秒发送10条，适合嵌入式系统）
    // {
    //     std::cout << "\n[Stress Test 3] Sending 1000 TradeRecords (10 records per 200ms)..." << std::endl;
    //     auto startTime = std::chrono::high_resolution_clock::now();
        
    //     const int testCount = 1000;
    //     const int batchSize = 10;  // BY ZF: 每批发送10条
    //     const int batchIntervalMs = 200;  // BY ZF: 每批间隔200毫秒（进一步降低频率，适合嵌入式系统）
    //     auto lastBatchTime = std::chrono::steady_clock::now();
        
    //     for (int i = 0; i < testCount; ++i) {
    //         TradeRecord rec;
    //         rec.gunNo = (i % 4) + 1;  // 4个枪位循环
    //         rec.tradeNo = "STRESS_TNO_" + std::to_string(time(nullptr)) + "_" + std::to_string(i);
    //         rec.vinCode = "VIN_STRESS_" + std::to_string(i);
    //         rec.timeDivType = 1;
    //         rec.startType = 1;
    //         rec.chargeStartTime = 1730265600 + i * 3600;
    //         rec.chargeEndTime = 1730265600 + i * 3600 + 3600;
    //         rec.startSoc = 20 + (i % 60);
    //         rec.endSoc = 80 + (i % 20);
    //         rec.reason = 0;
    //         rec.feeModelId = "FM_STRESS";
    //         rec.sumStart = 1000 + i * 10;
    //         rec.sumEnd = 1250 + i * 10;
    //         rec.totalElect = 2500 + i * 5;
    //         rec.totalPowerCost = 1000 + i * 2;
    //         rec.totalServCost = 300 + i;
    //         rec.totalCost = 1300 + i * 3;
    //         rec.timeNum = 3;
    //         rec.partElect = {static_cast<unsigned int>(10 + i), 
    //                         static_cast<unsigned int>(20 + i), 
    //                         static_cast<unsigned int>(30 + i)};
    //         rec.chargeFee = {static_cast<unsigned int>(100 + i), 
    //                         static_cast<unsigned int>(200 + i), 
    //                         static_cast<unsigned int>(300 + i)};
    //         rec.serviceFee = {static_cast<unsigned int>(40 + i), 
    //                         static_cast<unsigned int>(50 + i), 
    //                         static_cast<unsigned int>(60 + i)};
    //         rec.startPoint = 0;
    //         rec.crossPoints = 1;
    //         rec.pointsElect = {static_cast<unsigned int>(99 + i)};
    //         rec.cardNumber = "CARD_" + std::to_string(i);
            
    //         logger.logTradeRecord(rec);
            
    //         // BY ZF: 每发送batchSize条记录后，等待batchIntervalMs毫秒
    //         if ((i + 1) % batchSize == 0) {
    //             auto now = std::chrono::steady_clock::now();
    //             auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
    //                 now - lastBatchTime).count();
                
    //             if (elapsed < batchIntervalMs) {
    //                 std::this_thread::sleep_for(
    //                     std::chrono::milliseconds(batchIntervalMs - elapsed));
    //             }
    //             lastBatchTime = std::chrono::steady_clock::now();
    //         }
            
    //         if ((i + 1) % 200 == 0) {
    //             std::cout << "  Sent " << (i + 1) << " TradeRecords..." << std::endl;
    //         }
    //     }
        
    //     auto endTime = std::chrono::high_resolution_clock::now();
    //     auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
    //         endTime - startTime).count();
        
    //     double msgPerSec = (testCount * 1000.0) / duration;
    //     std::cout << "  ✓ Completed: " << testCount << " TradeRecords in " 
    //               << duration << " ms" << std::endl;
    //     std::cout << "  ✓ Throughput: " << std::fixed << std::setprecision(2) 
    //               << msgPerSec << " records/second" << std::endl;
    // }
    
    // // BY ZF: 等待处理完成
    // std::cout << "\nWaiting 3 seconds for logger to process..." << std::endl;
    // sleep(3);
    
    // // BY ZF: 测试4 - 大量FeeModel发送（96时段，控制频率：每200毫秒发送10条，适合嵌入式系统）
    // {
    //     std::cout << "\n[Stress Test 4] Sending 100 FeeModels (96 segments each, 10 models per 200ms)..." << std::endl;
    //     auto startTime = std::chrono::high_resolution_clock::now();
        
    //     const int testCount = 100;
    //     const int batchSize = 10;  // BY ZF: 每批发送10条
    //     const int batchIntervalMs = 200;  // BY ZF: 每批间隔200毫秒（FeeModel数据量大，降低频率）
    //     auto lastBatchTime = std::chrono::steady_clock::now();
        
    //     for (int i = 0; i < testCount; ++i) {
    //         FeeModel model;
    //         model.feeModelId = "STRESS_FM_" + std::to_string(time(nullptr)) + "_" + std::to_string(i);
    //         model.timeNum = 96;  // 最大时段数
            
    //         // 生成96个时段
    //         for (int j = 0; j < 96; ++j) {
    //             int hour = (j * 15) / 60;
    //             int minute = (j * 15) % 60;
    //             char seg[5];
    //             snprintf(seg, sizeof(seg), "%02d%02d", hour, minute);
    //             model.timeSeg.push_back(seg);
    //             model.segFlag.push_back((j % 3) + 1);
    //             model.chargeFee.push_back(100 + j + i);
    //             model.serviceFee.push_back(50 + j + i);
    //         }
            
    //         logger.saveFeeModel(model);
            
    //         // BY ZF: 每发送batchSize条记录后，等待batchIntervalMs毫秒
    //         if ((i + 1) % batchSize == 0) {
    //             auto now = std::chrono::steady_clock::now();
    //             auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
    //                 now - lastBatchTime).count();
                
    //             if (elapsed < batchIntervalMs) {
    //                 std::this_thread::sleep_for(
    //                     std::chrono::milliseconds(batchIntervalMs - elapsed));
    //             }
    //             lastBatchTime = std::chrono::steady_clock::now();
    //         }
            
    //         if ((i + 1) % 20 == 0) {
    //             std::cout << "  Sent " << (i + 1) << " FeeModels..." << std::endl;
    //         }
    //     }
        
    //     auto endTime = std::chrono::high_resolution_clock::now();
    //     auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
    //         endTime - startTime).count();
        
    //     double msgPerSec = (testCount * 1000.0) / duration;
    //     std::cout << "  ✓ Completed: " << testCount << " FeeModels in " 
    //               << duration << " ms" << std::endl;
    //     std::cout << "  ✓ Throughput: " << std::fixed << std::setprecision(2) 
    //               << msgPerSec << " models/second" << std::endl;
    // }
    
    // // BY ZF: 等待处理完成
    // std::cout << "\nWaiting 5 seconds for logger to process all messages..." << std::endl;
    // sleep(5);
    
    // // BY ZF: 测试5 - 混合负载测试（同时发送多种类型的消息，控制频率：每200毫秒发送10条，适合嵌入式系统）
    // {
    //     std::cout << "\n[Stress Test 5] Mixed load test (logs + TradeRecords + FeeModels, 10 messages per 200ms)..." << std::endl;
    //     auto startTime = std::chrono::high_resolution_clock::now();
        
    //     LogSender mixedLogger("MixedTest");
    //     const int testCount = 500;
    //     const int batchSize = 10;  // BY ZF: 每批发送10条
    //     const int batchIntervalMs = 200;  // BY ZF: 每批间隔200毫秒（进一步降低频率，适合嵌入式系统）
    //     auto lastBatchTime = std::chrono::steady_clock::now();
        
    //     for (int i = 0; i < testCount; ++i) {
    //         // 发送日志
    //         mixedLogger.info("Mixed load message " + std::to_string(i), 
    //                    "Testing mixed workload");
            
    //         // 每10条消息发送一个TradeRecord
    //         if (i % 10 == 0) {
    //             TradeRecord rec;
    //             rec.gunNo = (i % 4) + 1;
    //             rec.tradeNo = "MIXED_TNO_" + std::to_string(i);
    //             rec.timeNum = 3;
    //             rec.partElect = {10, 20, 30};
    //             rec.chargeFee = {100, 200, 300};
    //             rec.serviceFee = {40, 50, 60};
    //             rec.crossPoints = 0;
    //             rec.pointsElect = {};
    //             logger.logTradeRecord(rec);
    //         }
            
    //         // 每50条消息发送一个FeeModel
    //         if (i % 50 == 0) {
    //             FeeModel model;
    //             model.feeModelId = "MIXED_FM_" + std::to_string(i);
    //             model.timeNum = 3;
    //             model.timeSeg = {"0000", "0630", "1200"};
    //             model.segFlag = {1, 2, 3};
    //             model.chargeFee = {100, 200, 300};
    //             model.serviceFee = {50, 100, 150};
    //             logger.saveFeeModel(model);
    //         }
            
    //         // BY ZF: 每发送batchSize条消息后，等待batchIntervalMs毫秒
    //         if ((i + 1) % batchSize == 0) {
    //             auto now = std::chrono::steady_clock::now();
    //             auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
    //                 now - lastBatchTime).count();
                
    //             if (elapsed < batchIntervalMs) {
    //                 std::this_thread::sleep_for(
    //                     std::chrono::milliseconds(batchIntervalMs - elapsed));
    //             }
    //             lastBatchTime = std::chrono::steady_clock::now();
    //         }
            
    //         if ((i + 1) % 100 == 0) {
    //             std::cout << "  Processed " << (i + 1) << " mixed messages..." << std::endl;
    //         }
    //     }
        
    //     auto endTime = std::chrono::high_resolution_clock::now();
    //     auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
    //         endTime - startTime).count();
        
    //     double msgPerSec = (testCount * 1000.0) / duration;
    //     std::cout << "  ✓ Completed: " << testCount << " mixed messages in " 
    //               << duration << " ms" << std::endl;
    //     std::cout << "  ✓ Throughput: " << std::fixed << std::setprecision(2) 
    //               << msgPerSec << " messages/second" << std::endl;
    // }
    
    // BY ZF: 最终等待
    std::cout << "\nWaiting 5 seconds for logger to process all remaining messages..." << std::endl;
    sleep(5);
    
    std::cout << "\n=== Stress Test Completed ===" << std::endl;
    std::cout << "All stress tests finished. Check database for results." << std::endl;
    std::cout << "Logger test process finished" << std::endl;
    return 0;
}
