#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <limits>
#include <algorithm>
#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>

#include "fusion_power_video.h"
#include "camera_format_handler.h"

using namespace std;
using namespace fpvc;

int main() {
    // 硬编码输入文件
    const std::string input_file = "/NFSdata/data1/EastCameraData2024/145425/145425.seq";

    // 打开输入文件
    std::ifstream infile(input_file, std::ios::binary);
    if (!infile) {
        std::cerr << "Failed to open input file: " << input_file << std::endl;
        return 1;
    }

    // 初始化 CameraFormatHandler 并读取 header
    CameraFormatHandler handler;
    std::vector<uint8_t> original_header(handler.HeaderSize());
    if (!infile.read(reinterpret_cast<char*>(original_header.data()), handler.HeaderSize())) {
        std::cerr << "Failed to read header" << std::endl;
        return 1;
    }
    if (!handler.ParseHeader(original_header.data(), original_header.size())) {
        std::cerr << "Failed to parse header" << std::endl;
        return 1;
    }

    // 获取头部信息
    const CameraHeader &header_info = handler.GetHeaderInfo();
    const size_t width = header_info.width;
    const size_t height = header_info.height;
    const uint16_t bit_depth = header_info.bit_depth;
    size_t total_frames = header_info.kept_frame_count;
    total_frames = 4000;

    std::cout << "Input file info:" << std::endl;
    std::cout << "- Dimensions: " << width << "x" << height << std::endl;
    std::cout << "- Bit depth: " << bit_depth << std::endl;
    std::cout << "- Total frames (from header): " << total_frames << std::endl;

    // 每帧原始数据大小（通过 handler 获取）
    size_t frame_buffer_size = handler.FrameSize();
    std::cout << "- Frame size: " << frame_buffer_size << " bytes" << std::endl;

    // 读取所有帧数据并存入 original_frames
    std::vector< std::vector<uint16_t> > original_frames;
    std::vector<uint8_t> frame_buffer(frame_buffer_size);
    size_t frame_index = 0;
    while (infile.read(reinterpret_cast<char*>(frame_buffer.data()), frame_buffer_size)) {
        // 提取相机帧
        CameraFrame current_frame;
        size_t pos = 0;
        if (!handler.ExtractFrame(frame_buffer.data(), frame_buffer_size, &pos, &current_frame)) {
            std::cerr << "Error extracting frame " << frame_index << ". Skipping." << std::endl;
            continue;
        }
        // 检查提取后的数据大小是否符合预期
        if (current_frame.data.size() != width * height) {
            std::cerr << "Frame " << frame_index << " has unexpected size: " 
                      << current_frame.data.size() << " vs expected " << width * height << std::endl;
            continue;
        }
        // 转换成 16 位数据（此处简单将 8 位数据转换到 16 位，可以根据需要扩展）
        std::vector<uint16_t> frame_16bit(width * height);
        for (size_t j = 0; j < width * height; j++) {
            frame_16bit[j] = static_cast<uint16_t>(current_frame.data[j]);
        }
        original_frames.push_back(std::move(frame_16bit));
        frame_index++;
        if(frame_index >= total_frames) break;
    }
    if (original_frames.empty()) {
        std::cerr << "No valid frames read!" << std::endl;
        return 1;
    }
    std::cout << "Total frames read: " << original_frames.size() << std::endl;

    // 编码阶段：使用 Encoder 对所有帧进行编码
    // hard code: 对于8位数据，此处 shift 设为 0，big_endian 为 false，线程数设为8
    const int shift = 0;
    const bool big_endian = false;
    const int num_threads = 8;
    Encoder encoder(num_threads, shift, big_endian);
    std::vector<uint8_t> encoded_data;
    // 写回调：将生成的编码数据追加到 encoded_data 中
    auto write_callback = [&encoded_data](const uint8_t* data, size_t size, void* /*payload*/) {
        encoded_data.insert(encoded_data.end(), data, data + size);
    };

    bool initialized = false;
    for (size_t i = 0; i < original_frames.size(); i++) {
        uint16_t* img = original_frames[i].data();
        if (!initialized) {
            // 使用第一帧初始化编码器，作为 delta_frame
            encoder.Init(img, width, height, write_callback, nullptr);
            initialized = true;
        }
        encoder.CompressFrame(img, write_callback, nullptr);
    }
    encoder.Finish(write_callback, nullptr);
    std::cout << "Encoding complete, encoded data size: " << encoded_data.size() << " bytes" << std::endl;

    // 解码阶段：使用 RandomAccessDecoder 对编码数据进行解码，并检验每帧数据
    RandomAccessDecoder decoder;
    if (!decoder.Init(encoded_data.data(), encoded_data.size())) {
        std::cerr << "Decoder failed to initialize" << std::endl;
        return 1;
    }
    if (decoder.numframes() != original_frames.size() || decoder.xsize() != width || decoder.ysize() != height) {
        std::cerr << "Mismatch in decoder parameters:" << std::endl;
        std::cerr << "Decoded frames: " << decoder.numframes() << " (" << decoder.xsize() 
                  << "x" << decoder.ysize() << ") vs original frames: " << original_frames.size() 
                  << " (" << width << "x" << height << ")" << std::endl;
        return 1;
    }

    bool all_match = true;
    for (size_t i = 0; i < original_frames.size(); i++) {
        std::vector<uint16_t> decoded_frame(width * height, 0);
        if (!decoder.DecodeFrame(i, decoded_frame.data())) {
            std::cerr << "Failed to decode frame " << i << std::endl;
            all_match = false;
            continue;
        }
        std::vector<uint8_t> decoded_frame_8bit(width * height);
        for (size_t j = 0; j < width * height; j++) {
            decoded_frame_8bit[j] = static_cast<uint8_t>(decoded_frame[j]);
        }
        //write decoded frame
        cv::Mat frame_Mat = cv::Mat(height, width, CV_8UC1, decoded_frame_8bit.data());
        std::string output_path = "/home/wukong/Code/fusion-power-video/output/int-output/decoded_8bit_" + std::to_string(i) + ".bmp";
        cv::imwrite(output_path, frame_Mat);
        

        // 比较解码数据与原始数据是否一致
        if (!std::equal(original_frames[i].begin(), original_frames[i].end(), decoded_frame.begin())) {
            std::cerr << "Frame " << i << " mismatch between original and decoded data" << std::endl;
            all_match = false;
        }
    }

    if (all_match) {
        std::cout << "Roundtrip test successful: All frames match the original." << std::endl;
    } else {
        std::cerr << "Roundtrip test failed: Some frames did not match." << std::endl;
        return 1;
    }

    return 0;
}