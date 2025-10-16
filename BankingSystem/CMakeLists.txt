cmake_minimum_required(VERSION 3.20)
project(OnlineBanking)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MySQL include and lib
include_directories(/opt/homebrew/include/mysql)
link_directories(/opt/homebrew/lib)

# ✅ Only use main.cpp for now
add_executable(OnlineBanking
        main.cpp
)

target_link_libraries(OnlineBanking mysqlclient)
