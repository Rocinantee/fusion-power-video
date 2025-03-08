#include <fstream>
#include <iostream>
#include <vector>
#include "fusion_power_video.h"
#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <filesystem>

int main() {
    // 文件设置
    std::string input_file = "/home/wukong/Code/fusion-power-video/output/145425-output-16bit.fpv";
    std::ifstream infile(input_file, std::ios::binary);
    
    if (!infile) {
        std::cerr << "Failed to open input file: " << input_file << std::endl;
        return 1;
    }
    
    // 确保输出目录存在
    std::string output_dir = "/home/wukong/Code/fusion-power-video/output/extracted-frames/";
    if (!std::filesystem::exists(output_dir)) {
        std::filesystem::create_directories(output_dir);
    }
    
    std::cerr << "Starting streaming decoder..." << std::endl;
    fpvc::StreamingDecoder decoder;
    
    // 帧计数器和结果追踪
    size_t frame_count = 0;
    bool has_error = false;
    
    // 使用较小的块大小 (64KB)
    const size_t block_size = 1024*1024*1024; // 64KB chunks
    std::vector<uint8_t> buffer(block_size);
    
    // 定义解码回调函数
    auto decode_callback = [&frame_count, &has_error, &output_dir](
                          bool ok, uint16_t* frame, 
                          size_t width, size_t height, 
                          void* /*payload*/) {
        if (!ok) {
            std::cerr << "Error decoding frame " << frame_count << std::endl;
            has_error = true;
            return;
        }
        
        // 只处理每100帧中的一帧
        if (frame_count % 100 == 0) {
            std::cout << "\rProcessing frame " << frame_count << std::flush;
            
            try {
                // 从16位转换为8位 (提取高字节)
                std::vector<uint8_t> frame_8bit(width * height);
                for (size_t j = 0; j < width * height; j++) {
                    frame_8bit[j] = static_cast<uint8_t>(frame[j]);
                }
                
                // 创建OpenCV Mat并保存图像
                cv::Mat frame_Mat = cv::Mat(height, width, CV_8UC1, frame_8bit.data());
                std::string output_path = output_dir + "decoded_8bit_" + 
                                        std::to_string(frame_count) + ".bmp";
                cv::imwrite(output_path, frame_Mat);
            } 
            catch (const std::exception& e) {
                std::cerr << "Error saving frame " << frame_count << ": " << e.what() << std::endl;
                has_error = true;
            }
        }
        
        frame_count++;
    };
    
    // 按块流式处理文件
    size_t pos = 0;
    size_t total_bytes = 0;
    
    std::cout << "Starting decoding process..." << std::endl;
    while (infile) {
        infile.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
        size_t bytes_read = infile.gcount();
        if (bytes_read == 0) break;
        
        total_bytes += bytes_read;
        
        // 将数据块传递给解码器
        decoder.Decode(buffer.data(), bytes_read, decode_callback);
        
        pos += bytes_read;
        // if (pos % (1024 * 1024) == 0) {
        //     std::cout << "\nProcessed " << (pos / (1024 * 1024)) << " MB" << std::flush;
        // }
    }
    
    std::cout << "\nDecoding complete:" << std::endl;
    std::cout << "Total bytes processed: " << total_bytes << " (" 
              << (total_bytes / (1024.0 * 1024.0)) << " MB)" << std::endl;
    std::cout << "Total frames decoded: " << frame_count << std::endl;
    
    if (has_error) {
        std::cerr << "Some errors occurred during decoding" << std::endl;
        return 1;
    }
    
    std::cout << "All frames decoded successfully" << std::endl;
    return 0;
}