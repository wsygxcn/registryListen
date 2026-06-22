cmake_minimum_required(VERSION 4.2)
project(registryListen)

set(CMAKE_CXX_STANDARD 20)

# 静态链接所有运行时库，避免运行时缺少 DLL
if(MINGW)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
endif()

add_executable(registryListen
    main.cpp
    registry_enumerator.cpp
    encrypted_file_saver.cpp
    encrypted_file_reader.cpp
    registry_diff.cpp
)
target_link_libraries(registryListen bcrypt)
