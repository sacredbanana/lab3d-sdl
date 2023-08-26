# switch.cmake - Toolchain file for Nintendo Switch devkitPro

# Set system name
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Set the compilers (adjust paths if necessary)
find_program(SWITCH_CC aarch64-none-elf-gcc
  PATHS $ENV{DEVKITPRO}/devkitA64/bin
)
find_program(SWITCH_CXX aarch64-none-elf-g++
  PATHS $ENV{DEVKITPRO}/devkitA64/bin
)
find_program(SWITCH_AR aarch64-none-elf-ar
  PATHS $ENV{DEVKITPRO}/devkitA64/bin
)
find_program(SWITCH_AS aarch64-none-elf-as
  PATHS $ENV{DEVKITPRO}/devkitA64/bin
)

# Set compiler and linker flags
set(CMAKE_C_FLAGS_INIT "-march=armv8-a -mtune=cortex-a57 -mtp=soft -fPIC -ftls-model=local-exec")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-specs=$ENV{DEVKITPRO}/libnx/switch.specs")

# Make sure we use the correct compilers
if(SWITCH_CC)
  set(CMAKE_C_COMPILER ${SWITCH_CC})
endif()
if(SWITCH_CXX)
  set(CMAKE_CXX_COMPILER ${SWITCH_CXX})
endif()
if(SWITCH_AR)
  set(CMAKE_AR ${SWITCH_AR})
endif()
if(SWITCH_AS)
  set(CMAKE_ASM_COMPILER ${SWITCH_AS})
endif()

# Set the find root path (adjust paths if necessary)
list(APPEND CMAKE_FIND_ROOT_PATH
  $ENV{DEVKITPRO}/devkitA64/aarch64-none-elf
  $ENV{DEVKITPRO}/libnx
)

# Search for programs, libraries, and headers in the target directories only
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
