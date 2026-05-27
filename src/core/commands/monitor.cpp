#include "nav/core/command.hpp"
#include "nav/core/serial.hpp"
#include "nav/core/ui.hpp"

#include <atomic>
#include <csignal>
#include <iostream>
#include <map>
#include <string>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

namespace {

std::atomic<bool> g_running_monitor{true};

void handle_sigint(int /*s*/) {
    g_running_monitor = false;
}

} // namespace

namespace nav::core {

int MonitorCommand::run(IExecutionContext& /*ctx*/, const std::vector<std::string>& args) {
    std::string target_port;
    std::string baud_str = "9600";

    for (size_t i = 0; i < args.size(); ++i) {
        if ((args[i] == "--port" || args[i] == "-p") && (i + 1 < args.size())) {
            target_port = args[++i];
        } else if ((args[i] == "--baud" || args[i] == "-b") && (i + 1 < args.size())) {
            baud_str = args[++i];
        }
    }

    if (target_port.empty()) {
        ui::step("Scanning", "Autodetecting active serial hardware interfaces...");
        auto candidates = find_serial_ports();
        if (candidates.empty()) {
            ui::error("Auto-detection failed. No hardware detected. Specify manually with --port <path>.");
            return 1;
        }
        if (candidates.size() == 1) {
            target_port = candidates.front();
            ui::info("Using port: " + target_port);
        } else {
            ui::error("Multiple serial devices detected. Specify one with --port <path>:");
            for (const auto& c : candidates) ui::info("  " + c);
            return 1;
        }
    }

    speed_t baud_rate = B9600;
    static const std::map<std::string, speed_t> baud_map = {
        {"9600", B9600}, {"19200", B19200}, {"38400", B38400},
        {"57600", B57600}, {"115200", B115200}
    };
    if (auto it = baud_map.find(baud_str); it != baud_map.end()) {
        baud_rate = it->second;
    } else {
        ui::warning("Unknown baud requested (" + baud_str + "). Falling back to 9600.");
    }

    ui::step("Monitoring", "Opening direct native pipe: [" + target_port + "] at " + baud_str + " baud...");

    int fd = open(target_port.c_str(), O_RDWR | O_NOCTTY | O_SYNC | O_NONBLOCK);
    if (fd < 0) {
        ui::error("Kernel Access Denied! Permission denied or device already locked.");
        return 1;
    }

    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) {
        ui::error("Hardware API Fault: Could not read termios properties.");
        close(fd);
        return 1;
    }

    cfsetospeed(&tty, baud_rate);
    cfsetispeed(&tty, baud_rate);

    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_iflag &= ~IGNBRK;
    tty.c_lflag = 0;
    tty.c_oflag = 0;
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 5;
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~(PARENB | PARODD);
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        ui::error("Hardware API Fault: Could not lock target baud configuration.");
        close(fd);
        return 1;
    }

    ui::success("Successfully latched interface. Capturing raw telemetry stream now...");
    ui::info("System notice: [Press Ctrl+C to cleanly detach]");
    std::cout << "-------------------------------------------------------------\n" << std::endl;

    g_running_monitor = true;
    auto old_handler = std::signal(SIGINT, handle_sigint);

    char read_buffer[1024];
    while (g_running_monitor) {
        struct pollfd pfd{fd, POLLIN, 0};
        int prc = ::poll(&pfd, 1, 100);
        if (prc < 0) {
            if (errno == EINTR) continue;
            ui::error("\nCritical Link Failure: poll() failed.");
            break;
        }
        if (prc == 0) continue; // timeout — loop and re-check g_running_monitor

        if (pfd.revents & (POLLIN | POLLHUP)) {
            ssize_t bytes_read = ::read(fd, read_buffer, sizeof(read_buffer));
            if (bytes_read > 0) {
                std::cout.write(read_buffer, bytes_read);
                std::cout.flush();
            } else if (bytes_read < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                ui::error("\nCritical Link Failure: Connection severed by host kernel.");
                break;
            }
        }
        if (pfd.revents & POLLERR) {
            ui::error("\nCritical Link Failure: device reported error.");
            break;
        }
    }

    std::signal(SIGINT, old_handler);
    close(fd);

    std::cout << "\n\n";
    ui::success("Link detached gracefully. Channel closed.");
    return 0;
}

} // namespace nav::core
