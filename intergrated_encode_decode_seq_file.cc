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
    // Hard-coded input file and output encoded file path
    const std::string input_file = "/NFSdata/data1/EastCameraData2024/145425/145425.seq";
    const std::string encoded_file_path = "/home/wukong/Code/fusion-power-video/output/145425-output-encoded.fpv";

    // Open original sequence file for encoding
    std::ifstream infile(input_file, std::ios::binary);
    if (!infile) {
        std::cerr << "Failed to open input file: " << input_file << std::endl;
        return 1;
    }

    // Prepare output file for encoded data
    std::ofstream outfile(encoded_file_path, std::ios::binary);
    if (!outfile) {
        std::cerr << "Failed to open output file: " << encoded_file_path << std::endl;
        return 1;
    }

    // Initialize CameraFormatHandler and read header from original file
    CameraFormatHandler handler;
    std::vector<uint8_t> header_data(handler.HeaderSize());
    if (!infile.read(reinterpret_cast<char*>(header_data.data()), handler.HeaderSize())) {
        std::cerr << "Failed to read header" << std::endl;
        return 1;
    }
    if (!handler.ParseHeader(header_data.data(), header_data.size())) {
        std::cerr << "Failed to parse header" << std::endl;
        return 1;
    }

    // Get header info
    const CameraHeader &header_info = handler.GetHeaderInfo();
    const size_t width = header_info.width;
    const size_t height = header_info.height;
    const uint16_t bit_depth = header_info.bit_depth;
    size_t total_frames = header_info.kept_frame_count;
    // For testing, limit to 4000 frames
    total_frames = 1000;

    std::cout << "Input file info:" << std::endl;
    std::cout << "- Dimensions: " << width << "x" << height << std::endl;
    std::cout << "- Bit depth: " << bit_depth << std::endl;
    std::cout << "- Total frames (from header): " << total_frames << std::endl;

    // Get per-frame size from handler
    size_t frame_buffer_size = handler.FrameSize();
    std::cout << "- Frame size: " << frame_buffer_size << " bytes" << std::endl;

    // ENCODING PHASE:
    // Process the sequence file frame by frame and pass the frame to the encoder.
    // Do not store the original frames in memory.
    const int shift = 0;       // For 8-bit data, no shift is needed here
    const bool big_endian = false;
    const int num_threads = 8;
    
    Encoder encoder(num_threads, shift, big_endian);
    size_t num_buffers = encoder.MaxQueued();
    std::vector<uint16_t> buffers[num_buffers];
    for (size_t i = 0; i < num_buffers; i++) {
      buffers[i].resize(width * height);
    }
    size_t buffer_index = 0;

    // Write callback directly writes into the outfile stream.
    auto write_callback = [&outfile](const uint8_t* data, size_t size, void* /*payload*/) {
        outfile.write(reinterpret_cast<const char*>(data), size);
    };

    bool encoder_initialized = false;
    size_t frame_index = 0;
    static std::vector<uint8_t> frame_buffer(frame_buffer_size);
    static std::vector<uint16_t> frame_16bit(width * height,0);
    while (infile.read(reinterpret_cast<char*>(frame_buffer.data()), frame_buffer_size) && frame_index < total_frames) {
        // Extract camera frame from buffer
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

        
        // Convert 8-bit data to 16-bit (if needed, here simply cast each value)

        for (size_t j = 0; j < width * height; j++) {
            frame_16bit[j] = static_cast<uint16_t>(current_frame.data[j]);
        }
        buffers[buffer_index] = frame_16bit;
        uint16_t* img = buffers[buffer_index].data();
        if (!encoder_initialized) {
            // Use the first valid frame to initialize the encoder (delta frame)
            encoder.Init(img, width, height, write_callback, nullptr);
            encoder_initialized = true;
        }

        encoder.CompressFrame(img, write_callback, nullptr);
        buffer_index = (buffer_index + 1) % num_buffers;
        frame_index++;

    }

    // Finalize encoding (this writes the frame index etc.)
    encoder.Finish(write_callback, nullptr);
    outfile.close();
    std::cout << "Encoding complete. Encoded file written to disk at: " << encoded_file_path << std::endl;

    // DECODING PHASE:
    // Read the encoded file from disk (we assume the file size is moderate)
    std::ifstream encoded_in(encoded_file_path, std::ios::binary);
    if (!encoded_in) {
        std::cerr << "Failed to open encoded file for decoding: " << encoded_file_path << std::endl;
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
    if (decoder.numframes() != frame_index || decoder.xsize() != width || decoder.ysize() != height) {
        std::cerr << "Mismatch in decoder parameters:" << std::endl;
        std::cerr << "Decoded frames: " << decoder.numframes() << " (" << decoder.xsize() 
                  << "x" << decoder.ysize() << ") vs. expected: " << frame_index 
                  << " (" << width << "x" << height << ")" << std::endl;
        return 1;
    }

    // ROUNDTRIP TEST:
    // Instead of storing the original frames in memory from the encoding phase,
    // re-read them from the original input file.
    // Re-open the original file and skip the header.
    std::ifstream orig_in(input_file, std::ios::binary);
    if (!orig_in) {
        std::cerr << "Failed to re-open original input file: " << input_file << std::endl;
        return 1;
    }
    // Skip header:
    orig_in.seekg(handler.HeaderSize(), std::ios::beg);

    bool all_match = true;
    std::vector<uint8_t> orig_frame_buffer(frame_buffer_size);
    for (size_t i = 0; i < frame_index; i++) {
        if (!orig_in.read(reinterpret_cast<char*>(orig_frame_buffer.data()), frame_buffer_size)) {
            std::cerr << "Failed to read original frame " << i << std::endl;
            all_match = false;
            break;
        }
        CameraFrame current_frame;
        size_t pos = 0;
        if (!handler.ExtractFrame(orig_frame_buffer.data(), frame_buffer_size, &pos, &current_frame)) {
            std::cerr << "Error extracting original frame " << i << std::endl;
            all_match = false;
            continue;
        }
        if (current_frame.data.size() != width * height) {
            std::cerr << "Original frame " << i << " has unexpected size: " 
                      << current_frame.data.size() << " vs expected " << width * height << std::endl;
            all_match = false;
            continue;
        }
        // Convert original frame (8-bit) to 16-bit for comparison
        std::vector<uint16_t> orig_frame_16bit(width * height);
        for (size_t j = 0; j < width * height; j++) {
            orig_frame_16bit[j] = static_cast<uint16_t>(current_frame.data[j]);
        }

        // Decode frame i from the encoded output
        std::vector<uint16_t> decoded_frame(width * height, 0);
        if (!decoder.DecodeFrame(i, decoded_frame.data())) {
            std::cerr << "Failed to decode frame " << i << std::endl;
            all_match = false;
            continue;
        }
        // Compare the decoded frame with the original frame read from disk.
        if (!std::equal(orig_frame_16bit.begin(), orig_frame_16bit.end(), decoded_frame.begin())) {
            std::cerr << "Frame " << i << " mismatch between original and decoded data" << std::endl;
            all_match = false;
        }
    }
    orig_in.close();

    if (all_match) {
        std::cout << "Roundtrip test successful: All frames match the original." << std::endl;
    } else {
        std::cerr << "Roundtrip test failed: Some frames did not match." << std::endl;
        return 1;
    }

    return 0;
}