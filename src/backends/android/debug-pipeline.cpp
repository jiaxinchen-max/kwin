/*
 * Debug utility for Mesa rendering pipeline
 * 调试Mesa渲染管道的工具
 */

#include <iostream>
#include <chrono>
#include <cstdint>

// 模拟渲染管道的调试函数
class RenderingPipelineDebugger {
public:
    static void logFrameStart(int width, int height) {
        std::cout << "=== Frame Start ===" << std::endl;
        std::cout << "Resolution: " << width << "x" << height << std::endl;
        frameStartTime = std::chrono::high_resolution_clock::now();
    }
    
    static void logMesaRendering(const char* driver) {
        auto now = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - frameStartTime);
        std::cout << "Mesa " << driver << " rendering completed in " << elapsed.count() << "μs" << std::endl;
        mesaCompleteTime = now;
    }
    
    static void logPixelCopy(size_t bytes, bool hasContent) {
        auto now = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - mesaCompleteTime);
        
        std::cout << "Pixel copy:" << std::endl;
        std::cout << "  Size: " << bytes << " bytes" << std::endl;
        std::cout << "  Time: " << elapsed.count() << "μs" << std::endl;
        std::cout << "  Has content: " << (hasContent ? "YES" : "NO") << std::endl;
        
        pixelCopyTime = now;
    }
    
    static void logFrameEnd() {
        auto now = std::chrono::high_resolution_clock::now();
        auto totalElapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - frameStartTime);
        
        std::cout << "Frame completed in " << totalElapsed.count() << "μs total" << std::endl;
        std::cout << "=== Frame End ===" << std::endl << std::endl;
    }
    
    static void analyzePixelData(void* data, int width, int height) {
        uint32_t* pixels = (uint32_t*)data;
        int totalPixels = width * height;
        
        int nonZeroPixels = 0;
        uint32_t firstNonZero = 0;
        
        for (int i = 0; i < totalPixels; i++) {
            if (pixels[i] != 0) {
                nonZeroPixels++;
                if (firstNonZero == 0) {
                    firstNonZero = pixels[i];
                }
            }
        }
        
        std::cout << "Pixel Analysis:" << std::endl;
        std::cout << "  Total pixels: " << totalPixels << std::endl;
        std::cout << "  Non-zero pixels: " << nonZeroPixels << " (" 
                  << (100.0 * nonZeroPixels / totalPixels) << "%)" << std::endl;
        std::cout << "  First non-zero pixel: 0x" << std::hex << firstNonZero << std::dec << std::endl;
        
        // 分析颜色通道
        uint8_t r = (firstNonZero >> 16) & 0xFF;
        uint8_t g = (firstNonZero >> 8) & 0xFF;
        uint8_t b = firstNonZero & 0xFF;
        uint8_t a = (firstNonZero >> 24) & 0xFF;
        
        std::cout << "  RGBA: (" << (int)r << ", " << (int)g << ", " << (int)b << ", " << (int)a << ")" << std::endl;
    }
    
private:
    static std::chrono::high_resolution_clock::time_point frameStartTime;
    static std::chrono::high_resolution_clock::time_point mesaCompleteTime;
    static std::chrono::high_resolution_clock::time_point pixelCopyTime;
};

// 静态成员定义
std::chrono::high_resolution_clock::time_point RenderingPipelineDebugger::frameStartTime;
std::chrono::high_resolution_clock::time_point RenderingPipelineDebugger::mesaCompleteTime;
std::chrono::high_resolution_clock::time_point RenderingPipelineDebugger::pixelCopyTime;

/*
 * 在AndroidEglLayer中使用这些调试函数：
 * 
 * std::optional<OutputLayerBeginFrameInfo> AndroidEglLayer::doBeginFrame() {
 *     RenderingPipelineDebugger::logFrameStart(m_width, m_height);
 *     // ... 现有代码 ...
 * }
 * 
 * bool AndroidEglLayer::doEndFrame(...) {
 *     RenderingPipelineDebugger::logMesaRendering("llvmpipe"); // 或 "zink"
 *     
 *     // ... glReadPixels代码 ...
 *     glReadPixels(0, 0, desc->width, desc->height, GL_RGBA, GL_UNSIGNED_BYTE, desc->data);
 *     
 *     RenderingPipelineDebugger::logPixelCopy(desc->size, true);
 *     RenderingPipelineDebugger::analyzePixelData(desc->data, desc->width, desc->height);
 *     RenderingPipelineDebugger::logFrameEnd();
 * }
 */