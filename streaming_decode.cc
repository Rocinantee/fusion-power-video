#include <fstream>
#include <iostream>
#include <vector>
#include "fusion_power_video.h"
#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>

int main() {
    // File setup
    std::string input_file = "/home/wukong/Code/fusion-power-video/output/output-test.fpv";
    std::ifstream infile(input_file, std::ios::binary);
    
    if (!infile) {
        std::cerr << "Failed to open input file: " << input_file << std::endl;
        return 1;
    }
    
    // Create a streaming decoder
    fpvc::StreamingDecoder decoder;
    
    // Frame counter and frame selection
    size_t frame_count = 0;
    
    // Process the file in chunks
    const size_t chunk_size = 1024 * 1024; // 1MB chunks
    std::vector<uint8_t> buffer(chunk_size);
    
    // Define decoder callback function
    auto decode_callback = [&frame_count](bool ok, uint16_t* frame, 
                                        size_t width, size_t height, 
                                        void* /*payload*/) {
        if (!ok) {
            std::cerr << "Error decoding frame" << std::endl;
            return;
        }
        
        // Only process every 100th frame to match original behavior
        if (frame_count % 100 == 0 && frame_count < 3000) {
            std::cout << "Processing frame " << frame_count << std::endl;
            
            // Convert 16-bit to 8-bit (extract high byte)
            std::vector<uint8_t> frame_8bit(width * height);
            for (size_t j = 0; j < width * height; j++) {
                frame_8bit[j] = (frame[j] >> 8) & 0xFF;
            }
            
            // Create OpenCV Mat and save image
            cv::Mat frame_Mat = cv::Mat(height, width, CV_8UC1, frame_8bit.data());
            cv::imwrite("/home/wukong/Code/fusion-power-video/output/extracted-frames/decoded_8bit_" + 
                      std::to_string(frame_count) + ".png", frame_Mat);
        }
        
        frame_count++;
    };
    
    // Feed data to the decoder in chunks
    while (infile) {
        infile.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
        size_t bytes_read = infile.gcount();
        if (bytes_read == 0) break;
        
        // Pass the chunk to the decoder
        decoder.Decode(buffer.data(), bytes_read, decode_callback);
    }
    
    std::cout << "Total frames decoded: " << frame_count << std::endl;
    return 0;
}