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
    
    // Read entire file into memory for random access decoder
    infile.seekg(0, std::ios::end);
    size_t file_size = infile.tellg();
    infile.seekg(0, std::ios::beg);
    
    std::vector<uint8_t> file_data(file_size);
    infile.read(reinterpret_cast<char*>(file_data.data()), file_size);
    
    // Initialize decoder
    fpvc::RandomAccessDecoder decoder;
    if (!decoder.Init(file_data.data(), file_data.size())) {
        std::cerr << "Failed to initialize decoder" << std::endl;
        return 1;
    }
    
    // Get dimensions and frame count
    size_t width = decoder.xsize();
    size_t height = decoder.ysize();
    size_t frame_count = decoder.numframes();
    
    std::cout << "File contains " << frame_count << " frames of size " 
              << width << "x" << height << std::endl;
    
    // Decode each frame
    std::vector<uint16_t> frame(width * height);
    std::vector<uint8_t> frame_8bit(width * height);
    
    for (size_t i = 0; i < 3000;i+=100)
    {
        if (!decoder.DecodeFrame(i, frame.data())) {
            std::cerr << "Failed to decode frame " << i << std::endl;
            continue;
        }
        for (size_t j = 0; j < width * height; j++) {
            frame_8bit[j] = static_cast<u_int8_t>(frame[j] >> 8 & 0xFF);
        }
        
        

        cv::Mat frame_Mat = cv::Mat(height, width, CV_8UC1,frame_8bit.data());
        cv::imwrite("/home/wukong/Code/fusion-power-video/output/extracted-frames/decoded_8bit_" + std::to_string(i) + ".png", frame_Mat);

    }

    return 0;
}