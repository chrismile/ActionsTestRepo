#include <string>
#include <cstring>
#include <cassert>
#include <vector>
#include <iostream>
#include "Archive.hpp"
#include <archive.h>
#include <archive_entry.h>

int main() {
    uint8_t* buffer = nullptr;
    size_t bufferSize = 0;
    sgl::ArchiveFileLoadReturnType returnCode = sgl::loadFileFromArchive(
            "test.zip", buffer, bufferSize, true);

    std::cout << "TEST" << std::endl;
    std::cout << "TEST2" << std::endl;
    
    return 0;
}
