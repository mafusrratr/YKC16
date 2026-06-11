// BY ZF
#include "virtual_plug_backend.h"

#include <csignal>
#include <chrono>
#include <fcntl.h>
#include <iostream>
#include <thread>
#include <unistd.h>

namespace {
volatile std::sig_atomic_t g_stop = 0;

void onSignal(int)
{
    g_stop = 1;
}

bool daemonize()
{
    const pid_t pid = fork();
    if (pid < 0) {
        return false;
    }
    if (pid > 0) {
        return true;
    }
    if (setsid() < 0) {
        _exit(1);
    }
    std::signal(SIGHUP, SIG_IGN);
    const int fd = open("/dev/null", O_RDWR);
    if (fd >= 0) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > STDERR_FILENO) {
            close(fd);
        }
    }
    return false;
}
}

int main(int argc, char* argv[])
{
    std::string configPath = "virtual_plug.ini";
    bool runAsDaemon = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-d" || arg == "--daemon") {
            runAsDaemon = true;
        } else {
            configPath = arg;
        }
    }

    if (runAsDaemon && daemonize()) {
        return 0;
    }

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    VirtualPlugBackend backend;
    if (!backend.loadConfig(configPath)) {
        return 1;
    }
    if (!backend.start()) {
        return 2;
    }

    std::cout << "virtual_plug_backend running" << std::endl;
    while (!g_stop) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    backend.stop();
    return 0;
}
