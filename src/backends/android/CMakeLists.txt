# Android Backend for KWin

set(ANDROID_BACKEND_SOURCES
    android_backend.cpp
    android_output.cpp
    android_egl_backend.cpp
)

# Compile Android backend directly into kwin (not as separate module)
# This ensures proper initialization order and symbol resolution
target_sources(kwin PRIVATE ${ANDROID_BACKEND_SOURCES})

# Link termux-render library to kwin for Android backend functionality
target_link_libraries(kwin PRIVATE termux-render)
