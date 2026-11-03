#pragma once
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unistd.h>
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + " check failed: " #x); } while (false)
struct TempDirectory {
    std::string path;
    TempDirectory() { char name[] = "/tmp/audioplay-test-XXXXXX"; const char* p = ::mkdtemp(name); CHECK(p); path = p; }
};
