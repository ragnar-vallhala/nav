#pragma once

#include <string>
#include <iostream>

namespace nav::core::ui {

// Color Codes
const std::string RESET   = "\033[0m";
const std::string BOLD    = "\033[1m";
const std::string GREEN   = "\033[32m";
const std::string RED     = "\033[31m";
const std::string YELLOW  = "\033[33m";
const std::string CYAN    = "\033[36m";
const std::string MAGENTA = "\033[35m";

inline void print_banner() {
    std::cout << BOLD << CYAN;
    std::cout << R"(
███╗   ██╗ █████╗ ██╗   ██╗
████╗  ██║██╔══██╗██║   ██║
██╔██╗ ██║███████║██║   ██║
██║╚██╗██║██╔══██║╚██╗ ██╔╝
██║ ╚████║██║  ██║ ╚████╔╝ 
╚═╝  ╚═══╝╚═╝  ╚═╝  ╚═══╝  

)" << RESET << std::endl;
}

inline void info(const std::string& msg) {
    std::cout << CYAN << "ℹ " << RESET << msg << std::endl;
}

inline void success(const std::string& msg) {
    std::cout << GREEN << BOLD << "✔ " << RESET << GREEN << msg << RESET << std::endl;
}

inline void error(const std::string& msg) {
    std::cout << RED << BOLD << "✖ " << RESET << RED << msg << RESET << std::endl;
}

inline void warning(const std::string& msg) {
    std::cout << YELLOW << BOLD << "⚠ " << RESET << YELLOW << msg << RESET << std::endl;
}

inline void step(const std::string& verb, const std::string& item) {
    std::cout << MAGENTA << BOLD << " => " << RESET << BOLD << verb << " " << RESET << item << std::endl;
}

} // namespace nav::core::ui
