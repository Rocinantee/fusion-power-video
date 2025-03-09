#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <limits>
#include <algorithm>
#include <filesystem>
#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>

#include "fusion_power_video.h"
#include "camera_format_handler.h"

using namespace std;
using namespace fpvc;

int main() {
    // Hard-coded input file
    const std::string input_file = "/NFSdata/data1/EastCameraData2024/145425/145425.seq";

    // Open input file
    std::ifstream infile(input_file, std::ios::binary);
    if (!infile) {
        std::cerr << "Failed to open input file: " << input_file << std::endl;
        return 1;
    }

    // Initialize CameraFormatHandler and read header
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

    // Get header info
    const CameraHeader &header_info = handler.GetHeaderInfo();
    const size_t width = header_info.width;
    const size_t height = header_info.height;
    const uint16_t bit_depth = header_info.bit_depth;
    size_t total_frames = header_info.kept_frame_count;
    // For testing purpose, limit to 4000 frames
    total_frames = 4000;

    std::cout << "Input file info:" << std::endl;
    std::cout << "- Dimensions: " << width << "x" << height << std::endl;
    std::cout << "- Bit depth: " << bit_depth << std::endl;
    std::cout << "- Total frames (from header): " << total_frames << std::endl;

    // Get per-frame size from handler
    size_t frame_buffer_size = handler.FrameSize();
    std::cout << "- Frame size: " << frame_buffer_size << " bytes" << std::endl;

    // Read all frames and store (convert 8-bit data to 16-bit)
    std::vector< std::vector<uint16_t> > original_frames;
    std::vector<uint8_t> frame_buffer(frame_buffer_size);
    size_t frame_index = 0;
    while (infile.read(reinterpret_cast<char*>(frame_buffer.data()), frame_buffer_size)) {
        // Extract camera frame
        CameraFrame current_frame;
        size_t pos = 0;
        if (!handler.ExtractFrame(frame_buffer.data(), frame_buffer_size, &pos, &current_frame)) {
            std::cerr << "Error extracting frame " << frame_index << ". Skipping." << std::endl;
            continue;
        }
        if (current_frame.data.size() != width * height) {
            std::cerr << "Frame " << frame_index << " has unexpected size: " 
                      << current_frame.data.size() << " vs expected " << width * height << std::endl;
            continue;
        }
        std::vector<uint16_t> frame_16bit(width * height);
        for (size_t j = 0; j < width * height; j++) {
            // Simply cast 8-bit value to 16-bit (you can modify if a bit-shift is required)
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

    // ENCODING PHASE: Encode all frames using Encoder.
    // For 8-bit data, we use shift=0 and big_endian=false.
    const int shift = 0;
    const bool big_endian = false;
    const int num_threads = 8;
    Encoder encoder(num_threads, shift, big_endian);
    // Encoded data will be stored in this vector.
    std::vector<uint8_t> encoded_data;
    // Write callback: Append generated data to encoded_data.
    auto write_callback = [&encoded_data](const uint8_t* data, size_t size, void* /*payload*/) {
        encoded_data.insert(encoded_data.end(), data, data + size);
    };

    bool initialized = false;
    for (size_t i = 0; i < original_frames.size(); i++) {
        uint16_t* img = original_frames[i].data();
        if (!initialized) {
            // Use the first frame to initialize the encoder (delta frame)
            encoder.Init(img, width, height, write_callback, nullptr);
            initialized = true;
        }
        encoder.CompressFrame(img, write_callback, nullptr);
    }
    encoder.Finish(write_callback, nullptr);
    std::cout << "Encoding complete, encoded data size: " << encoded_data.size() << " bytes" << std::endl;

    // Write the encoded data to disk at the specified path.
    std::string encoded_file = "/home/wukong/Code/fusion-power-video/output/145425-output-encoded.fpv";
    std::ofstream encoded_out(encoded_file, std::ios::binary);
    if (!encoded_out) {
        std::cerr << "Failed to open output file for writing encoded data: " << encoded_file << std::endl;
        return 1;
    }
    encoded_out.write(reinterpret_cast<const char*>(encoded_data.data()), encoded_data.size());
    encoded_out.close();
    std::cout << "Encoded file written to disk." << std::endl;

    // DECODING PHASE: Read the encoded file and use RandomAccessDecoder.
    std::ifstream encoded_in(encoded_file, std::ios::binary);
    if (!encoded_in) {
        std::cerr << "Failed to open encoded file for decoding: " << encoded_file << std::endl;
        return 1;
    }
    encoded_in.seekg(0, std::ios::end);
    size_t encoded_file_size = encoded_in.tellg();
    encoded_in.seekg(0, std::ios::beg);
    std::vector<uint8_t> encoded_file_data(encoded_file_size);
    encoded_in.read(reinterpret_cast<char*>(encoded_file_data.data()), encoded_file_size);
    encoded_in.close();

    RandomAccessDecoder decoder;
    if (!decoder.Init(encoded_file_data.data(), encoded_file_data.size())) {
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
    // Create output folder for decoded frames if it does not exist.
    std::string decoded_output_dir = "/home/wukong/Code/fusion-power-video/output/int-output/";
    if (!std::filesystem::exists(decoded_output_dir)) {
        std::filesystem::create_directories(decoded_output_dir);
    }

    // For each frame, decode and write the output image.
    for (size_t i = 0; i < original_frames.size(); i++) {
        std::vector<uint16_t> decoded_frame(width * height, 0);
        if (!decoder.DecodeFrame(i, decoded_frame.data())) {
            std::cerr << "Failed to decode frame " << i << std::endl;
            all_match = false;
            continue;
        }
        // Convert the 16-bit decoded frame to 8-bit by simply casting; adjust if a shift is needed.
        std::vector<uint8_t> decoded_frame_8bit(width * height);
        for (size_t j = 0; j < width * height; j++) {
            decoded_frame_8bit[j] = static_cast<uint8_t>(decoded_frame[j]);
        }
        cv::Mat frame_Mat = cv::Mat(height, width, CV_8UC1, decoded_frame_8bit.data());
        std::string output_path = decoded_output_dir + "decoded_8bit_" + std::to_string(i) + ".bmp";
        cv::imwrite(output_path, frame_Mat);

        // Compare with original frame (if desired)
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