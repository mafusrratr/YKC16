/**
 * 守护进程实现
 * BY ZF
 */

#include "daemon_process.h"
#include "../base/common/message_queue.h"
#include <iostream>
#include <sstream>
#include <unistd.h>
#include <sys/wait.h>
#include <fstream>
#include <sys/stat.h>
#include <errno.h>
#include <signal.h>
#include <libgen.h>
#include <string.h>

DaemonProcess::DaemonProcess()
    : m_status(PROC_STATUS_INIT)
    , m_messageQueue(MSG_KEY_WATCHDOG)  // BY ZF: 使用看门狗队列
    , m_systemQueue(MSG_KEY_SYSTEM)     // BY ZF: 系统控制队列（用于接收更新请求）
    , m_running(false)
    , m_monitorInterval(5)      // 默认5秒监控间隔
    , m_watchdogTimeout(30)     // 默认30秒看门狗超时
    , m_maxRestartCount(10)     // 默认最大重启10次
    , m_restartDelay(5)         // 默认重启延迟5秒
    , m_upgradeDir("/usr/app/upgrade")
    , m_deployDir("/usr/app/tcu")
    , m_updateProcessPid(-1)    // BY ZF: 更新进程PID初始化为-1
    , m_isUpdating(false)       // BY ZF: 初始未在更新
    , m_watchdogEnabled(true)   // BY ZF: 默认启用看门狗监控
    , m_rollbackCheckInterval(10)  // BY ZF: 默认回滚检查间隔10秒
    , m_rollbackThresholdTime(300) // BY ZF: 默认回滚阈值5分钟
    , m_rollbackCheckEnabled(false) // BY ZF: 默认禁用回滚检查
    , m_hasTimeoutInWindow(false)  // BY ZF: 初始未在时间窗口内检测到超时
    , m_totalChecks(0)
    , m_failedChecks(0)
    , m_restartCount(0)
{
    std::cout << "DaemonProcess created" << std::endl;
}

DaemonProcess::~DaemonProcess()
{
    stop();
    std::cout << "DaemonProcess destroyed" << std::endl;
}

// BY ZF: 初始化守护进程配置、进程列表并准备监控线程
bool DaemonProcess::initialize(const char* config)
{
    std::cout << "DaemonProcess::initialize config: " << config << std::endl;
    
    // 加载守护进程专用配置
    // 目标设备配置文件路径：/usr/app/config/daemon.ini
    std::string configFile = std::string(config);
    
    if (!getConfig().loadConfig(configFile)) {
        std::cout << "Failed to load daemon config: " << configFile << std::endl;
        return false;
    }
    
    // 调用守护进程特有的初始化
    if (!doInitialize()) {
        std::cout << "Daemon initialization failed" << std::endl;
        return false;
    }
    
    m_status.store(PROC_STATUS_RUNNING);
    std::cout << "DaemonProcess initialized successfully" << std::endl;
    return true;
}

// BY ZF: 执行初始化细节，如加载配置与业务进程清单
bool DaemonProcess::doInitialize()
{
    std::cout << "DaemonProcess::doInitialize" << std::endl;
    
    // BY ZF: 创建看门狗消息队列（业务进程将使用此队列发送心跳）
    std::cout << "Creating watchdog message queue (MSG_KEY_WATCHDOG)..." << std::endl;
    if (!m_messageQueue.create()) {
        std::cout << "Failed to create watchdog message queue" << std::endl;
        return false;
    }
    std::cout << "Watchdog message queue created successfully" << std::endl;
    
    // BY ZF: 创建系统控制消息队列（用于接收更新请求）
    std::cout << "Creating system message queue (MSG_KEY_SYSTEM)..." << std::endl;
    if (!m_systemQueue.create()) {
        std::cout << "Failed to create system message queue" << std::endl;
        return false;
    }
    std::cout << "System message queue created successfully" << std::endl;
    
    // 从配置加载参数
    m_monitorInterval = getConfig().getInt("Daemon", "monitor_interval", 5);
    m_watchdogTimeout = getConfig().getInt("Daemon", "watchdog_timeout", 30);
    m_maxRestartCount = getConfig().getInt("Daemon", "max_restart_count", 10);
    m_upgradeDir = getConfig().getString("Daemon", "upgrade_dir", "/usr/app/upgrade");
    m_deployDir = getConfig().getString("Daemon", "deploy_dir", "/usr/app/tcu");
    
    // BY ZF: 从配置加载更新管理相关参数
    std::string updatePathAndArgs = getConfig().getString("Daemon", "update_process_path", "");
    if (updatePathAndArgs.empty()) {
        // 尝试从ProcessPaths节读取
        updatePathAndArgs = getConfig().getString("ProcessPaths", "update_handler", "");
        if (updatePathAndArgs.empty()) {
            // 使用默认路径
            updatePathAndArgs = m_deployDir + "/update_handler";
        }
    }
    
    // BY ZF: 解析更新器路径和参数（支持在路径后直接添加参数，用空格分隔）
    // 例如: /usr/app/tcu/update_handler -qws
    std::stringstream ss(updatePathAndArgs);
    std::string token;
    bool first = true;
    while (ss >> token) {
        if (first) {
            m_updateProcessPath = token;
            first = false;
        } else {
            m_updateProcessArgs.push_back(token);
        }
    }
    m_updateStatusFile = getConfig().getString("Daemon", "update_status_file", "/usr/app/data/update_status.flag");
    m_rollbackCheckInterval = getConfig().getInt("Daemon", "rollback_check_interval", 10);
    m_rollbackThresholdTime = getConfig().getInt("Daemon", "rollback_threshold_time", 300);
    
    std::cout << "Daemon config - interval:" << m_monitorInterval 
              << "s, watchdog:" << m_watchdogTimeout 
              << "s, max_restart:" << m_maxRestartCount 
              << ", upgrade_dir:" << m_upgradeDir
              << ", deploy_dir:" << m_deployDir << std::endl;
    std::cout << "Update config - process_path:" << m_updateProcessPath;
    if (!m_updateProcessArgs.empty()) {
        std::cout << " with args:";
        for (const auto& arg : m_updateProcessArgs) {
            std::cout << " " << arg;
        }
    }
    std::cout << ", status_file:" << m_updateStatusFile
              << ", rollback_threshold:" << m_rollbackThresholdTime << "s" << std::endl;
    
    // 从配置文件加载进程列表
    if (!loadProcessListFromConfig()) {
        std::cout << "Failed to load process list from config" << std::endl;
        return false;
    }
    
    // BY ZF: 系统启动时检查更新状态
    std::string updateStatus, updatePackagePath;
    if (readUpdateStatus(updateStatus, updatePackagePath)) {
        std::cout << "Found update status file, status: " << updateStatus << std::endl;
        if (updateStatus == "UPDATING") {
            // 更新中断，执行回滚
            std::cout << "Update interrupted, performing rollback..." << std::endl;
            performRollback();
        } else if (updateStatus == "UPDATED") {
            // 文件已更新，需要启动进程并验证
            std::cout << "Update completed, will start processes for verification after initialization..." << std::endl;
            // 注意：这里不启动进程，因为start()方法会调用startAllProcesses()
            // 我们只需要标记需要启用回滚检查
            m_rollbackCheckEnabled = true;
            m_lastNormalTime = std::chrono::steady_clock::now();  // BY ZF: 记录正常时间起点（用于DONE判断）
            m_hasTimeoutInWindow = false;  // BY ZF: 初始化超时标志
        } else if (updateStatus == "DONE") {
            // 更新完成且已验证正常，清除状态文件
            std::cout << "Update verified successfully, clearing status file..." << std::endl;
            remove(m_updateStatusFile.c_str());
        }
    }
    
    return true;
}

// BY ZF: 启动守护进程监控线程并拉起业务子进程
int DaemonProcess::start()
{
    std::cout << "DaemonProcess::start" << std::endl;
    
    if (m_status.load() != PROC_STATUS_RUNNING) {
        std::cout << "Daemon not in running status" << std::endl;
        return -1;
    }
    
    m_running = true;
    
    // BY ZF: 如果状态为UPDATED，需要在启动进程后启用回滚检查
    bool needRollbackCheck = m_rollbackCheckEnabled.load();
    
    // 启动所有业务进程
    if (!startAllProcesses()) {
        std::cout << "Failed to start all business processes" << std::endl;
        return -1;
    }
    
    // BY ZF: 如果状态为UPDATED，恢复看门狗监控并初始化回滚检查
    if (needRollbackCheck) {
        m_watchdogEnabled = true;
        m_lastNormalTime = std::chrono::steady_clock::now();  // BY ZF: 记录正常时间起点（用于DONE判断）
        m_hasTimeoutInWindow = false;  // BY ZF: 初始化超时标志
        std::cout << "Update verification enabled, monitoring processes..." << std::endl;
    }
    
    // 启动监控线程
    m_monitorThread = std::thread(&DaemonProcess::monitorThread, this);
    std::cout << "Monitor thread started" << std::endl;
    
    // 等待监控线程启动
    usleep(100000);  // 100ms
    
    // 启动守护进程主循环
    std::cout << "Starting daemon main loop..." << std::endl;
    run();
    
    return 0;
}

// BY ZF: 停止守护进程，终止监控线程并关闭所有业务进程
void DaemonProcess::stop()
{
    std::cout << "DaemonProcess::stop" << std::endl;
    
    m_running = false;
    std::cout << "m_running set to false" << std::endl;
    
    // 先等待监控线程结束，避免死锁
    if (m_monitorThread.joinable()) {
        std::cout << "Waiting for monitor thread to finish..." << std::endl;
        try {
        m_monitorThread.join();
            std::cout << "Monitor thread joined successfully" << std::endl;
        } catch (const std::exception& e) {
            std::cout << "Monitor thread join failed: " << e.what() << std::endl;
        }
    }
    
    // 然后停止所有业务进程
    stopAllProcesses();
    
    // 调用清理
    doCleanup();
    
    m_status.store(PROC_STATUS_STOPPED);
    std::cout << "DaemonProcess stopped" << std::endl;
}

// BY ZF: 守护进程主循环入口，负责定期喂狗并阻塞等待退出
void DaemonProcess::run()
{
    std::cout << "DaemonProcess::run starting" << std::endl;
    
    while (m_running.load()) {
        try {
            // 守护进程主循环逻辑
            doRun();
            
            // 短暂休眠
            usleep(1000000);  // 1秒
            
        } catch (const std::exception& e) {
            std::cout << "Exception in daemon process: " << e.what() << std::endl;
            m_status.store(PROC_STATUS_ERROR);
            break;
        }
    }
    
    std::cout << "DaemonProcess::run finished" << std::endl;
}

// BY ZF: 预留扩展钩子，可在此加入守护进程自定义循环逻辑
void DaemonProcess::doRun()
{
    // BY ZF: 处理更新请求消息
    handleUpdateRequest();
    
    // 守护进程主循环逻辑
    // 可以在这里添加：
    // - 检查升级文件
    // - 处理系统信号
    // - 定期状态报告
    // - 其他守护进程特有的逻辑
}

// BY ZF: 清理守护进程运行时资源（用于收尾或异常退出）
void DaemonProcess::doCleanup()
{
    std::cout << "DaemonProcess::doCleanup" << std::endl;
    // 守护进程清理逻辑
}

// BY ZF: 将需要守护的业务进程加入监控列表
void DaemonProcess::addBusinessProcess(const std::string& processName, const std::string& executablePath, const std::vector<std::string>& args)
{
    std::lock_guard<std::mutex> lock(m_externalMutex);
    bool isNewProcess = (m_externalProcesses.find(processName) == m_externalProcesses.end());
    ProcessInfo info;
    info.executablePath = executablePath;
    info.args = args;
    m_externalProcesses[processName] = info;
    // BY ZF: 仅在首次添加时记录启动顺序，避免重复项
    if (isNewProcess) {
        m_processStartOrder.push_back(processName);
    }
    std::cout << "Added external process: " << processName << " -> " << executablePath;
    if (!args.empty()) {
        std::cout << " with args:";
        for (const auto& arg : args) {
            std::cout << " " << arg;
        }
    }
    std::cout << std::endl;
}


// BY ZF: 从配置文件加载并解析需要守护的业务进程列表
bool DaemonProcess::loadProcessListFromConfig()
{
    std::cout << "Loading process list from config..." << std::endl;
    
    // 读取进程列表配置
    std::string processListStr = getConfig().getString("Daemon", "process_list", "");
    
    std::cout << "Raw process list from config: '" << processListStr << "'" << std::endl;
    
    if (processListStr.empty()) {
        std::cout << "No process list configured" << std::endl;
        return true;  // 不是错误，只是没有配置进程列表
    }
    
    // 解析进程列表（逗号分隔）
    std::stringstream ss(processListStr);
    std::string processName;
    std::vector<std::string> processNames;
    
    while (std::getline(ss, processName, ',')) {
        // 去除前后空格
        processName.erase(0, processName.find_first_not_of(" \t"));
        processName.erase(processName.find_last_not_of(" \t") + 1);
        
        if (!processName.empty()) {
            processNames.push_back(processName);
        }
    }
    
    std::cout << "Found " << processNames.size() << " processes in config" << std::endl;
    
    // BY ZF: 为每个进程创建可执行文件路径和参数
    for (const auto& name : processNames) {
        // 优先从ProcessPaths节读取路径，否则使用默认路径
        std::string pathAndArgs = getConfig().getString("ProcessPaths", name, "");
        std::string executablePath;
        std::vector<std::string> args;
        
        if (pathAndArgs.empty()) {
            // 使用默认路径
            executablePath = m_deployDir + "/" + name;
        } else {
            // BY ZF: 解析路径和参数（支持在路径后直接添加参数，用空格分隔）
            // 例如: /usr/app/tcu/tcu_display -qws
            std::stringstream ss(pathAndArgs);
            std::string token;
            bool first = true;
            while (ss >> token) {
                if (first) {
                    executablePath = token;
                    first = false;
                } else {
                    args.push_back(token);
                }
            }
        }
        
        addBusinessProcess(name, executablePath, args);
    }
    
    std::cout << "Process list loaded successfully" << std::endl;
    return true;
}

// BY ZF: 启动配置中的全部业务进程
bool DaemonProcess::startAllProcesses()
{
    bool allStarted = true;
    
    // BY ZF: 按配置process_list顺序构建启动列表，避免map按字典序打乱顺序
    std::vector<std::pair<std::string, ProcessInfo> > startList;
    {
        std::lock_guard<std::mutex> lock(m_externalMutex);
        for (size_t i = 0; i < m_processStartOrder.size(); ++i) {
            const std::string& processName = m_processStartOrder[i];
            std::map<std::string, ProcessInfo>::const_iterator it = m_externalProcesses.find(processName);
            if (it != m_externalProcesses.end()) {
                startList.push_back(std::make_pair(processName, it->second));
            }
        }
    }
    
    // BY ZF: 启动外部进程（可执行文件），每个进程间隔2秒
    for (size_t i = 0; i < startList.size(); ++i) {
        const std::string& processName = startList[i].first;
        const ProcessInfo& processInfo = startList[i].second;
        if (!startExternalProcess(processName, processInfo.executablePath, processInfo.args)) {
            std::cout << "Failed to start external process: " << processName << std::endl;
            allStarted = false;
        }
        sleep(2);
    }
    
    if (allStarted) {
        std::cout << "All external processes started successfully" << std::endl;
    } else {
        std::cout << "Some external processes failed to start" << std::endl;
    }
    
    return allStarted;
}

// BY ZF: 停止所有业务进程并清理状态
void DaemonProcess::stopAllProcesses()
{
    // 停止外部进程
    {
        std::lock_guard<std::mutex> lock(m_externalMutex);
        for (const auto& pair : m_externalProcesses) {
            stopExternalProcess(pair.first);
        }
    }
    
    std::cout << "All external processes stopped" << std::endl;
}

// BY ZF: 以外部进程方式启动指定业务模块，并记录 PID（直接exec执行，先切换工作目录）
bool DaemonProcess::startExternalProcess(const std::string& processName, const std::string& executablePath, const std::vector<std::string>& args)
{
    std::cout << "Starting external process: " << processName << " -> " << executablePath;
    if (!args.empty()) {
        std::cout << " with args:";
        for (const auto& arg : args) {
            std::cout << " " << arg;
        }
    }
    std::cout << std::endl;
    
    // BY ZF: 提取可执行文件所在目录和工作目录
    std::string workDir;
    std::string fullExecPath = executablePath;
    
    // 复制路径字符串（因为 dirname 会修改字符串）
    char* pathCopy = strdup(executablePath.c_str());
    if (pathCopy != nullptr) {
        // 获取目录路径
        char* dir = dirname(pathCopy);
        workDir = std::string(dir);
        free(pathCopy);
    } else {
        // 如果分配失败，使用当前目录
        workDir = ".";
    }
    
    std::cout << "Work directory: " << workDir << ", Executable: " << fullExecPath << std::endl;
    
    // BY ZF: 创建子进程，直接exec执行目标程序（不是shell）
    pid_t pid = fork();
    if (pid == 0) {
        // 子进程：先切换到工作目录，然后执行目标程序
        // 切换工作目录
        if (chdir(workDir.c_str()) != 0) {
            std::cout << "Failed to change directory to: " << workDir << " error: " << strerror(errno) << std::endl;
            exit(1);
        }
        
        // BY ZF: 构建参数数组：进程名 + 配置的参数 + nullptr
        std::vector<char*> execArgs;
        // argv[0] 通常是进程名或可执行文件名
        char* nameCopy = strdup(executablePath.c_str());
        if (nameCopy != nullptr) {
            char* name = basename(nameCopy);
            execArgs.push_back(name);  // 使用 basename 作为 argv[0]
        } else {
            execArgs.push_back(const_cast<char*>(processName.c_str()));
        }
        
        // 添加配置的参数
        for (const auto& arg : args) {
            execArgs.push_back(const_cast<char*>(arg.c_str()));
        }
        execArgs.push_back(nullptr);  // execv 需要以 nullptr 结尾
        
        // BY ZF: 使用 execv 执行程序，传递参数
        // 注意：execv 会替换当前进程，所以 nameCopy 不需要 free（进程已被替换）
        execv(fullExecPath.c_str(), execArgs.data());
        
        // 如果execv失败，子进程会执行到这里
        std::cout << "Failed to execute: " << fullExecPath << " error: " << strerror(errno) << std::endl;
        if (nameCopy != nullptr) {
            free(nameCopy);
        }
        exit(1);
    } else if (pid > 0) {
        // 父进程：记录进程ID和初始化看门狗
        // 注意：这里不需要再次获取锁，因为调用者已经持有了锁
        m_externalPids[processName] = pid;
        m_externalWatchdogs[processName] = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch());
        std::cout << "External process started successfully: " << processName << " PID: " << pid << std::endl;
        return true;
    } else {
        // fork失败
        std::cout << "Failed to fork process: " << processName << " error: " << strerror(errno) << std::endl;
        return false;
    }
}

// BY ZF: 停止指定业务进程，优雅退出失败时强制终止
void DaemonProcess::stopExternalProcess(const std::string& processName)
{
    std::cout << "Stopping external process: " << processName << std::endl;
    
    // 注意：这里不需要再次获取锁，因为调用者已经持有了锁
    auto it = m_externalPids.find(processName);
    if (it != m_externalPids.end()) {
        pid_t pid = it->second;
        
        // 发送SIGTERM信号
        if (kill(pid, SIGTERM) == 0) {
            std::cout << "Sent SIGTERM to process: " << processName << " PID: " << pid << std::endl;
            
            // 等待进程结束
            int status;
            pid_t result = waitpid(pid, &status, WNOHANG);
            if (result == 0) {
                // 进程还在运行，等待一段时间
                std::cout << "Process still running, waiting 1 second..." << std::endl;
                sleep(1);  // 减少等待时间
                result = waitpid(pid, &status, WNOHANG);
                if (result == 0) {
                    // 强制杀死进程
                    std::cout << "Force killing process: " << processName << " PID: " << pid << std::endl;
                    kill(pid, SIGKILL);
                    waitpid(pid, &status, 0);
                }
            }
            
            std::cout << "Process stopped: " << processName << " PID: " << pid << std::endl;
        } else {
            std::cout << "Failed to send SIGTERM to process: " << processName << " PID: " << pid << std::endl;
        }
        
        // 从列表中移除
        m_externalPids.erase(it);
    } else {
        std::cout << "Process not found: " << processName << std::endl;
    }
}

// BY ZF: 监控线程入口，周期性检查所有业务进程状态与看门狗
void DaemonProcess::monitorThread()
{
    std::cout << "Monitor thread started" << std::endl;
    std::cout.flush();
    
    while (m_running.load()) {
        try {
            // BY ZF: 处理看门狗喂狗消息（仅在启用时）
            if (m_watchdogEnabled.load()) {
            handleWatchdogMessage();
            }
            
            // BY ZF: 检查更新进程是否完成
            if (m_updateProcessPid > 0) {
                int status;
                pid_t result = waitpid(m_updateProcessPid, &status, WNOHANG);
                if (result == m_updateProcessPid) {
                    // 更新进程已完成
                    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                        // 更新成功，设置状态为 UPDATED
                        recordUpdateStatus("UPDATED", m_updatePackagePath);
                        std::cout << "Update process completed successfully, status set to UPDATED" << std::endl;
                        
                        // BY ZF: 先恢复监控标志，再启动进程
                        m_isUpdating = false;
                        m_watchdogEnabled = true;  // 恢复看门狗监控
                        
                        // 启动业务进程并开始回滚检查
                        startAllProcesses();
                        m_rollbackCheckEnabled = true;
                        m_lastNormalTime = std::chrono::steady_clock::now();  // BY ZF: 记录正常时间起点（用于DONE判断）
                        m_hasTimeoutInWindow = false;  // BY ZF: 初始化超时标志
                    } else {
                        // 更新失败，记录错误
                        std::cout << "Update process failed, exit code: " << WEXITSTATUS(status) << std::endl;
                        // 更新失败时，恢复监控标志和看门狗监控
                        m_isUpdating = false;
                        m_watchdogEnabled = true;
                        // 可选：执行回滚或保留UPDATING状态
                    }
                    m_updateProcessPid = -1;
                }
            }
            
            // BY ZF: 如果状态为UPDATED，检查是否需要设置为DONE或回滚
            // 注意：回滚判断基于心跳超时，如果在阈值时间内检测到心跳超时，则执行回滚
            if (m_rollbackCheckEnabled.load()) {
                auto now = std::chrono::steady_clock::now();
                
                // BY ZF: 检查所有进程是否真正正常运行
                // 判断依据：1) 所有进程PID存在 2) 所有进程在运行（非僵尸进程） 3) 没有看门狗超时
                // 注意：这是"与"关系，必须同时满足所有条件才认为正常
                bool allProcessesNormal = true;
                bool hasWatchdogTimeout = false;  // BY ZF: 是否检测到看门狗超时
            {
                std::lock_guard<std::mutex> lock(m_externalMutex);
                for (const auto& pair : m_externalProcesses) {
                        // 检查PID是否存在
                        auto pidIt = m_externalPids.find(pair.first);
                        if (pidIt == m_externalPids.end()) {
                            // PID不存在，说明进程未运行
                            allProcessesNormal = false;
                            break;
                        }
                        
                        // 检查进程是否真的在运行（非僵尸进程）
                        pid_t pid = pidIt->second;
                        int status;
                        pid_t result = waitpid(pid, &status, WNOHANG);
                        if (result == pid) {
                            // 进程已退出（包括僵尸进程）
                            allProcessesNormal = false;
                            break;
                        }
                        
                        // BY ZF: 检查看门狗是否超时
                        auto watchdogIt = m_externalWatchdogs.find(pair.first);
                        if (watchdogIt != m_externalWatchdogs.end()) {
                            auto currentTime = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::system_clock::now().time_since_epoch());
                            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                                currentTime - watchdogIt->second).count();
                            if (elapsed >= m_watchdogTimeout) {
                                // 看门狗超时
                                allProcessesNormal = false;
                                hasWatchdogTimeout = true;
                                
                                // BY ZF: 如果这是首次在时间窗口内检测到超时，记录时间
                                if (!m_hasTimeoutInWindow) {
                                    m_firstTimeoutTime = now;
                                    m_hasTimeoutInWindow = true;
                                    std::cout << "First watchdog timeout detected in rollback window: " << pair.first << std::endl;
                                }
                                break;
                            }
                        }
                    }
                }
                
                if (allProcessesNormal) {
                    // BY ZF: 所有进程正常，重置超时标志，累计正常时间
                    m_hasTimeoutInWindow = false;  // BY ZF: 重置超时标志（进程恢复正常）
                    
                    auto normalDuration = std::chrono::duration_cast<std::chrono::seconds>(
                        now - m_lastNormalTime).count();
                    
                    // 如果连续5分钟（300秒）正常（所有进程PID存在且在运行，且没有看门狗超时），设置为DONE
                    if (normalDuration >= 300) {
                        recordUpdateStatus("DONE");
                        m_rollbackCheckEnabled = false;
                        std::cout << "Update verified successfully (5 minutes normal, all processes running), status set to DONE" << std::endl;
                        remove(m_updateStatusFile.c_str());
                    }
                } else if (hasWatchdogTimeout && m_hasTimeoutInWindow) {
                    // BY ZF: 检测到心跳超时，且已在时间窗口内，检查是否在阈值时间内
                    auto timeoutDuration = std::chrono::duration_cast<std::chrono::seconds>(
                        now - m_firstTimeoutTime).count();
                    
                    if (timeoutDuration <= m_rollbackThresholdTime) {
                        // BY ZF: 在阈值时间内检测到心跳超时，执行回滚
                        std::cout << "Watchdog timeout detected within threshold time (" << timeoutDuration 
                                  << "s <= " << m_rollbackThresholdTime 
                                  << "s), performing rollback..." << std::endl;
                        m_rollbackCheckEnabled = false;  // 先禁用回滚检查，避免重复触发
                        performRollback();
                    } else {
                        // BY ZF: 超过阈值时间，重置超时标志（重新开始计时）
                        m_hasTimeoutInWindow = false;
                        std::cout << "Watchdog timeout window expired (" << timeoutDuration 
                                  << "s > " << m_rollbackThresholdTime 
                                  << "s), resetting timeout window" << std::endl;
                    }
                } else {
                    // BY ZF: 进程异常但不是看门狗超时（如PID不存在或进程退出），重置正常时间计时器
                    m_lastNormalTime = now;
                }
            }
            
            // BY ZF: 监控外部进程（仅在未更新且启用看门狗监控时）
            // 注意：更新过程中（m_isUpdating=true）必须暂停所有进程监控，避免自动重启
            if (!m_isUpdating.load() && m_watchdogEnabled.load()) {
                {
                    std::lock_guard<std::mutex> lock(m_externalMutex);
                    for (const auto& pair : m_externalProcesses) {
                        checkExternalProcess(pair.first, pair.second.executablePath);
                    checkExternalProcessWatchdog(pair.first);
                    }
                }
            }
            
            m_totalChecks++;
            
        } catch (const std::exception& e) {
            std::cout << "Exception in monitor thread: " << e.what() << std::endl;
        }
        
        // 监控间隔 - 使用更短的睡眠时间以便快速响应退出信号
        for (int i = 0; i < m_monitorInterval && m_running.load(); i++) {
            sleep(1);
        }
        
        // 检查退出条件
        if (!m_running.load()) {
            std::cout << "Monitor thread detected m_running=false, exiting loop" << std::endl;
            break;
        }
    }
    
    std::cout << "Monitor thread finished" << std::endl;
}

// BY ZF: 检查业务进程的看门狗心跳，判定是否超时
void DaemonProcess::checkExternalProcessWatchdog(const std::string& processName)
{
    // 注意：这里不需要再次获取锁，因为调用者已经持有了锁
    auto watchdogIt = m_externalWatchdogs.find(processName);
    if (watchdogIt != m_externalWatchdogs.end()) {
        auto currentTime = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch());
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            currentTime - watchdogIt->second).count();
        
        if (elapsed >= m_watchdogTimeout) {
            std::cout << "External process watchdog timeout: " << processName 
                      << " timeout: " << m_watchdogTimeout << " seconds" << std::endl;
        m_failedChecks++;
        
            // BY ZF: 看门狗超时，说明进程异常，重置正常时间计时器（用于DONE判断）
            // BY ZF: 如果这是首次在时间窗口内检测到超时，记录时间（用于回滚判断）
            if (m_rollbackCheckEnabled.load()) {
                m_lastNormalTime = std::chrono::steady_clock::now();
                if (!m_hasTimeoutInWindow) {
                    m_firstTimeoutTime = std::chrono::steady_clock::now();
                    m_hasTimeoutInWindow = true;
                    std::cout << "First watchdog timeout detected in rollback window: " << processName << std::endl;
                }
            }
            
            // BY ZF: 重启进程
            auto processIt = m_externalProcesses.find(processName);
            if (processIt != m_externalProcesses.end()) {
                restartExternalProcess(processName, processIt->second.executablePath, processIt->second.args);
            }
        }
    }
}

// BY ZF: 在守护策略允许的情况下重启异常退出的业务进程
void DaemonProcess::restartExternalProcess(const std::string& processName, const std::string& executablePath, const std::vector<std::string>& args)
{
    // 检查重启次数限制
    if (m_processRestartCount[processName] >= m_maxRestartCount) {
        std::cout << "Max restart count reached for external process: " << processName << std::endl;
        return;
    }
    
    std::cout << "Restarting external process: " << processName << std::endl;
    
    try {
        // 停止进程
        stopExternalProcess(processName);
        
        // 等待一段时间
        sleep(m_restartDelay);
        
        // BY ZF: 重新启动（传递参数）
        if (startExternalProcess(processName, executablePath, args)) {
            std::cout << "External process restarted successfully: " << processName << std::endl;
                m_restartCount++;
            m_processRestartCount[processName]++;
        } else {
            std::cout << "Failed to restart external process: " << processName << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cout << "Exception during restart of " << processName << ": " << e.what() << std::endl;
    }
}

// BY ZF: 核心健康检查逻辑，按需重启或停止业务进程
void DaemonProcess::checkExternalProcess(const std::string& processName, const std::string& executablePath)
{
    // BY ZF: executablePath 参数保留用于接口兼容性，实际从 m_externalProcesses 获取
    (void)executablePath;  // 避免未使用参数警告
    // 注意：这里不需要再次获取锁，因为调用者已经持有了锁
    auto it = m_externalPids.find(processName);
    if (it != m_externalPids.end()) {
        pid_t pid = it->second;
        
        // 检查进程是否还在运行
        int status;
        pid_t result = waitpid(pid, &status, WNOHANG);
        
        if (result == pid) {
            // 进程已退出（包括僵尸进程）
            std::cout << "External process exited: " << processName << " PID: " << pid << " status: " << status << std::endl;
            m_failedChecks++;
            
            // 清理僵尸进程记录
            m_externalPids.erase(it);
            
            // BY ZF: 如果进程异常（PID不存在或已退出），重置正常时间计时器（用于DONE判断）
            // 注意：回滚判断只基于心跳超时，PID异常不触发回滚
            if (m_rollbackCheckEnabled.load()) {
                m_lastNormalTime = std::chrono::steady_clock::now();
            }
            
            // BY ZF: 检查重启次数限制，获取进程信息（包含参数）
            auto processIt = m_externalProcesses.find(processName);
            if (processIt != m_externalProcesses.end() && m_processRestartCount[processName] < m_maxRestartCount) {
                std::cout << "Restarting external process: " << processName << std::endl;
                restartExternalProcess(processName, processIt->second.executablePath, processIt->second.args);
            } else {
                std::cout << "Max restart count reached for external process: " << processName << std::endl;
            }
        }
    } else {
        // PID记录不存在，可能进程启动失败
        std::cout << "External process PID not found: " << processName << ", attempting to restart" << std::endl;
        
        // BY ZF: 如果进程PID不存在，重置正常时间计时器（用于DONE判断）
        // 注意：回滚判断只基于心跳超时，PID不存在不触发回滚
        if (m_rollbackCheckEnabled.load()) {
            m_lastNormalTime = std::chrono::steady_clock::now();
        }
        
        auto processIt = m_externalProcesses.find(processName);
        if (processIt != m_externalProcesses.end() && m_processRestartCount[processName] < m_maxRestartCount) {
            restartExternalProcess(processName, processIt->second.executablePath, processIt->second.args);
        }
    }
}

// BY ZF: 汇总当前守护和业务进程状态，生成文本报告
std::string DaemonProcess::getProcessStatusReport() const
{
    std::stringstream ss;
    
    ss << "=== Daemon Process Status ===" << std::endl;
    ss << "Running: " << (m_running.load() ? "Yes" : "No") << std::endl;
    ss << "Status: " << m_status.load() << std::endl;
    ss << "External Processes: " << m_externalProcesses.size() << std::endl;
    ss << "Total Checks: " << m_totalChecks.load() << std::endl;
    ss << "Failed Checks: " << m_failedChecks.load() << std::endl;
    ss << "Total Restarts: " << m_restartCount.load() << std::endl;
    ss << "Monitor Interval: " << m_monitorInterval << "s" << std::endl;
    ss << "Watchdog Timeout: " << m_watchdogTimeout << "s" << std::endl;
    
    // 外部进程
    {
        std::lock_guard<std::mutex> lock(m_externalMutex);
        ss << "External Processes:" << std::endl;
        for (const auto& pair : m_externalProcesses) {
            auto pidIt = m_externalPids.find(pair.first);
            pid_t pid = (pidIt != m_externalPids.end()) ? pidIt->second : -1;
            ss << "  - " << pair.first 
               << " (Path: " << pair.second.executablePath;
            if (!pair.second.args.empty()) {
                ss << ", Args:";
                for (const auto& arg : pair.second.args) {
                    ss << " " << arg;
                }
            }
            ss << ", PID: " << pid
               << ", Restarts: " << m_processRestartCount.at(pair.first) << ")" << std::endl;
        }
    }
    
    return ss.str();
}

// BY ZF: 执行固件升级操作（复制并替换新版本应用）
bool DaemonProcess::upgradeFirmware(const std::string& upgradePath)
{
    std::cout << "Starting firmware upgrade from: " << upgradePath << std::endl;
    
    // 这里实现固件升级逻辑
    // 1. 停止所有业务进程
    // 2. 备份当前版本
    // 3. 安装新版本
    // 4. 重启系统
    
    return true;
}

// BY ZF: 重新载入配置文件，更新监控参数和进程列表
bool DaemonProcess::reloadConfig()
{
    std::cout << "Reloading configuration..." << std::endl;
    
    // 重新加载配置
    if (!getConfig().reloadConfig()) {
        std::cout << "Failed to reload config" << std::endl;
        return false;
    }
    
    // 更新配置参数
    m_monitorInterval = getConfig().getInt("Daemon", "monitor_interval", 5);
    m_watchdogTimeout = getConfig().getInt("Daemon", "watchdog_timeout", 30);
    m_maxRestartCount = getConfig().getInt("Daemon", "max_restart_count", 10);
    
    std::cout << "Configuration reloaded successfully" << std::endl;
    return true;
}

// BY ZF: 处理来自业务进程的看门狗心跳消息
void DaemonProcess::handleWatchdogMessage()
{
    // BY ZF: 循环接收所有看门狗消息，直到队列为空
    // 这样可以避免旧消息（来自已退出进程）残留在队列中，导致误判进程状态
    char buffer[256];
    int actualSize;
    int messageCount = 0;
    
    // BY ZF: 循环接收所有消息，直到队列为空
    while (true) {
    int result = m_messageQueue.receive(MSG_WATCHDOG_FEED, buffer, sizeof(buffer), &actualSize);
    
    if (result == 0) {
            // 成功接收消息
        std::string processName(buffer, actualSize);
            
            // BY ZF: 检查进程是否真的存在，如果不存在则忽略该消息（可能是旧消息）
            {
                std::lock_guard<std::mutex> lock(m_externalMutex);
                auto pidIt = m_externalPids.find(processName);
                if (pidIt == m_externalPids.end()) {
                    // PID不存在，说明进程已退出，忽略这条旧消息
                    std::cout << "Ignoring watchdog message from exited process: " << processName << std::endl;
                    messageCount++;
                    continue;  // 继续接收下一条消息
                }
                
                // BY ZF: 检查进程是否真的在运行（非僵尸进程）
                pid_t pid = pidIt->second;
                int status;
                pid_t result = waitpid(pid, &status, WNOHANG);
                if (result == pid) {
                    // 进程已退出（包括僵尸进程），忽略这条旧消息
                    std::cout << "Ignoring watchdog message from exited process: " << processName 
                              << " (PID: " << pid << ")" << std::endl;
                    messageCount++;
                    continue;  // 继续接收下一条消息
                }
            }
            
            // BY ZF: 进程存在且运行正常，更新看门狗时间
        feedExternalProcessWatchdog(processName);
        std::cout << "Watchdog fed: " << processName << std::endl;
            messageCount++;
        } else if (result == -1) {
            // 无消息，队列已空，退出循环
            break;
        } else {
            // 接收失败，退出循环
            break;
        }
    }
    
    // BY ZF: 如果处理了多条消息，输出日志（用于调试）
    if (messageCount > 1) {
        std::cout << "Processed " << messageCount << " watchdog messages in this cycle" << std::endl;
    }
}

// BY ZF: 将心跳写入共享 Map，供守护进程监控超时
void DaemonProcess::feedExternalProcessWatchdog(const std::string& processName)
{
    std::lock_guard<std::mutex> lock(m_externalMutex);
    auto it = m_externalWatchdogs.find(processName);
    if (it != m_externalWatchdogs.end()) {
        it->second = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch());
    }
}

// BY ZF: 处理更新请求消息（在主循环doRun中调用）
void DaemonProcess::handleUpdateRequest()
{
    // 非阻塞接收更新请求消息
    char buffer[256];
    int actualSize;
    
    int result = m_systemQueue.receive(MSG_UPDATE_REQUEST, buffer, sizeof(buffer), &actualSize);
    
    if (result == 0) {
        std::string updatePackagePath(buffer, actualSize);
        std::cout << "Received update request, package path: " << updatePackagePath << std::endl;
        
        // 执行更新流程
        if (!m_isUpdating.load()) {
            performUpdate(updatePackagePath);
        } else {
            std::cout << "Update already in progress, ignoring request" << std::endl;
        }
    }
}

// BY ZF: 执行更新管理流程（停止所有进程并启动更新处理进程）
bool DaemonProcess::performUpdate(const std::string& updatePackagePath)
{
    std::cout << "Starting update process, package path: " << updatePackagePath << std::endl;
    
    // 设置更新标志
    m_isUpdating = true;
    m_updatePackagePath = updatePackagePath;
    
    // 暂停看门狗监控
    m_watchdogEnabled = false;
    
    // 停止所有业务进程
    std::cout << "Stopping all business processes..." << std::endl;
    stopAllProcesses();
    
    // 等待所有进程完全停止（确保安全）
    sleep(15);
    
    // 记录更新状态为UPDATING
    recordUpdateStatus("UPDATING", updatePackagePath);
    
    // 启动更新处理进程
    if (!startUpdateProcess(updatePackagePath)) {
        std::cout << "Failed to start update process" << std::endl;
        m_isUpdating = false;
        m_watchdogEnabled = true;
        return false;
    }
    
    std::cout << "Update process started, PID: " << m_updateProcessPid << std::endl;
    return true;
}

// BY ZF: 启动更新处理进程（执行更新：update_handler -p <path>）
bool DaemonProcess::startUpdateProcess(const std::string& updatePackagePath)
{
    std::cout << "Starting update handler: " << m_updateProcessPath 
              << " -p " << updatePackagePath << std::endl;
    
    // 检查可执行文件是否存在
    struct stat st;
    if (stat(m_updateProcessPath.c_str(), &st) != 0) {
        std::cout << "Update handler not found: " << m_updateProcessPath << std::endl;
        return false;
    }
    
    // 检查文件是否可执行
    if (!(st.st_mode & S_IXUSR)) {
        std::cout << "Update handler is not executable: " << m_updateProcessPath << std::endl;
        return false;
    }
    
    // 创建子进程
    pid_t pid = fork();
    if (pid == 0) {
        // 子进程：执行更新器
        // BY ZF: 构建参数数组：进程路径 + 配置的参数（如-qws） + -p + 更新包路径 + nullptr
        std::vector<char*> execArgs;
        execArgs.push_back(const_cast<char*>(m_updateProcessPath.c_str()));
        
        // BY ZF: 添加配置的参数（如-qws等）
        for (const auto& arg : m_updateProcessArgs) {
            execArgs.push_back(const_cast<char*>(arg.c_str()));
        }
        
        // BY ZF: 添加更新选项和更新包路径
        execArgs.push_back(const_cast<char*>("-p"));  // 更新选项
        execArgs.push_back(const_cast<char*>(updatePackagePath.c_str()));
        execArgs.push_back(nullptr);  // execv 需要以 nullptr 结尾
        
        execv(m_updateProcessPath.c_str(), execArgs.data());
        
        // 如果execv失败，子进程会执行到这里
        std::cout << "Failed to execute update handler: " << m_updateProcessPath 
                  << " error: " << strerror(errno) << std::endl;
        exit(1);
    } else if (pid > 0) {
        // 父进程：记录进程ID
        m_updateProcessPid = pid;
        std::cout << "Update handler started successfully, PID: " << pid << std::endl;
        return true;
    } else {
        // fork失败
        std::cout << "Failed to fork update handler process, error: " << strerror(errno) << std::endl;
        return false;
    }
}

// BY ZF: 启动更新处理进程（执行回滚：update_handler -r）
bool DaemonProcess::startRollbackProcess()
{
    std::cout << "Starting rollback handler: " << m_updateProcessPath;
    if (!m_updateProcessArgs.empty()) {
        std::cout << " with args:";
        for (const auto& arg : m_updateProcessArgs) {
            std::cout << " " << arg;
        }
    }
    std::cout << " -r" << std::endl;
    
    // 检查可执行文件是否存在
    struct stat st;
    if (stat(m_updateProcessPath.c_str(), &st) != 0) {
        std::cout << "Update handler not found: " << m_updateProcessPath << std::endl;
        return false;
    }
    
    // 检查文件是否可执行
    if (!(st.st_mode & S_IXUSR)) {
        std::cout << "Update handler is not executable: " << m_updateProcessPath << std::endl;
        return false;
    }
    
    // 创建子进程
    pid_t pid = fork();
    if (pid == 0) {
        // 子进程：执行更新器回滚
        // BY ZF: 构建参数数组：进程路径 + 配置的参数（如-qws） + -r + nullptr
        std::vector<char*> execArgs;
        execArgs.push_back(const_cast<char*>(m_updateProcessPath.c_str()));
        
        // BY ZF: 添加配置的参数（如-qws等）
        for (const auto& arg : m_updateProcessArgs) {
            execArgs.push_back(const_cast<char*>(arg.c_str()));
        }
        
        // BY ZF: 添加回滚选项
        execArgs.push_back(const_cast<char*>("-r"));  // 回滚选项
        execArgs.push_back(nullptr);  // execv 需要以 nullptr 结尾
        
        execv(m_updateProcessPath.c_str(), execArgs.data());
        
        // 如果execv失败，子进程会执行到这里
        std::cout << "Failed to execute rollback handler: " << m_updateProcessPath 
                  << " error: " << strerror(errno) << std::endl;
        exit(1);
    } else if (pid > 0) {
        // 父进程：记录进程ID
        m_updateProcessPid = pid;
        std::cout << "Rollback handler started successfully, PID: " << pid << std::endl;
        return true;
    } else {
        // fork失败
        std::cout << "Failed to fork rollback handler process, error: " << strerror(errno) << std::endl;
        return false;
    }
}

// BY ZF: 执行回滚操作（调用 update_handler -r）
bool DaemonProcess::performRollback()
{
    std::cout << "Rollback needed, starting rollback process..." << std::endl;
    
    // 暂停看门狗监控
    m_watchdogEnabled = false;
    
    // 停止所有业务进程（如果还在运行）
    stopAllProcesses();
    
    // 等待进程停止
    sleep(2);
    
    // BY ZF: 确保在回滚前，所有进程监控都已停止
    // 清理PID记录，避免监控线程误判
    {
        std::lock_guard<std::mutex> lock(m_externalMutex);
        m_externalPids.clear();
    }
    
    // 启动回滚进程：update_handler -r
    if (!startRollbackProcess()) {
        std::cout << "Failed to start rollback process" << std::endl;
        m_watchdogEnabled = true;  // 恢复看门狗监控
        return false;
    }
    
    // 记录回滚状态
    m_isUpdating = true;
    std::cout << "Rollback process started, PID: " << m_updateProcessPid << std::endl;
    
    return true;
}

// BY ZF: 记录更新状态到持久化文件
void DaemonProcess::recordUpdateStatus(const std::string& status, const std::string& updatePackagePath)
{
    // BY ZF: 确保目录存在
    std::string dirPath = m_updateStatusFile.substr(0, m_updateStatusFile.find_last_of('/'));
    if (!dirPath.empty()) {
        // 创建目录（如果不存在）
        struct stat st;
        if (stat(dirPath.c_str(), &st) != 0) {
            // 目录不存在，尝试创建
            std::string cmd = "mkdir -p " + dirPath;
            system(cmd.c_str());
        }
    }
    
    std::ofstream file(m_updateStatusFile);
    if (file.is_open()) {
        file << "STATUS=" << status << std::endl;
        if (!updatePackagePath.empty()) {
            file << "PACKAGE_PATH=" << updatePackagePath << std::endl;
        }
        auto now = std::chrono::system_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()).count();
        file << "TIMESTAMP=" << timestamp << std::endl;
        file.close();
        std::cout << "Update status recorded: " << status << std::endl;
    } else {
        std::cout << "Failed to write update status file: " << m_updateStatusFile 
                  << " error: " << strerror(errno) << std::endl;
    }
}

// BY ZF: 读取更新状态
bool DaemonProcess::readUpdateStatus(std::string& status, std::string& lastUpdatePackagePath)
{
    std::ifstream file(m_updateStatusFile);
    if (!file.is_open()) {
        return false;  // 文件不存在
    }
    
    status.clear();
    lastUpdatePackagePath.clear();
    
    std::string line;
    while (std::getline(file, line)) {
        if (line.find("STATUS=") == 0) {
            status = line.substr(7);
        } else if (line.find("PACKAGE_PATH=") == 0) {
            lastUpdatePackagePath = line.substr(13);
        }
    }
    
    file.close();
    return !status.empty();  // 至少要有状态字段
}
