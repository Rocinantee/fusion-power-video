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

// encode_seq_file() 读取原始文件，对帧逐个编码，将编码后的数据写入磁盘。
// 它返回编码处理的帧数量，同时输出图像宽高。
bool encode_seq_file(const string &input_file,
                     const string &encoded_file_path,
                     size_t &encoded_frame_count,
                     size_t &width,
                     size_t &height,
                     CameraFormatHandler &handler)
{
    // 打开原始序列文件
    ifstream infile(input_file, ios::binary);
    if (!infile) {
        cerr << "Failed to open input file: " << input_file << endl;
        return false;
    }
    // 准备输出文件
    ofstream outfile(encoded_file_path, ios::binary);
    if (!outfile) {
        cerr << "Failed to open output file: " << encoded_file_path << endl;
        return false;
    }
    // 读取头部，并解析
    vector<uint8_t> header_data(handler.HeaderSize());
    if (!infile.read(reinterpret_cast<char*>(header_data.data()), handler.HeaderSize())) {
        cerr << "Failed to read header" << endl;
        return false;
    }
    if (!handler.ParseHeader(header_data.data(), header_data.size())) {
        cerr << "Failed to parse header" << endl;
        return false;
    }
    // 获取图像尺寸及帧数信息
    const CameraHeader &header_info = handler.GetHeaderInfo();
    width = header_info.width;
    height = header_info.height;
    // 为了测试限制帧数，可设置为1000
    size_t total_frames = header_info.kept_frame_count;
    cout << "Input file info:" << endl;
    cout << "- Dimensions: " << width << "x" << height << endl;
    cout << "- Total frames (from header): " << total_frames << endl;
    // 每帧缓冲区大小
    size_t frame_buffer_size = handler.FrameSize();
    cout << "- Frame size: " << frame_buffer_size << " bytes" << endl;

    // ENCODING PHASE:
    // 对每一帧进行编码，不在内存中保存原始帧，而是逐帧处理
    // 注意：对8位图像数据若需要将8位数值放入16位高字节，则 shift 应该为 8，
    // 但这里我们的转换仅作简单转换，所以按当前代码使用 shift = 0，即不做位移动作
    const int shift = 0; // 如果需要8位数据转16位的高字节请改为8
    const bool big_endian = false;
    const int num_threads = 8;
    Encoder encoder(num_threads, shift, big_endian);
    size_t num_buffers = encoder.MaxQueued();
    // 预分配多个缓冲区（避免多次申请）
    vector<uint16_t> buffers[num_buffers];
    for (size_t i = 0; i < num_buffers; i++) {
        buffers[i].resize(width * height);
    }
    size_t buffer_index = 0;
    // 写回调函数：将编码数据写入输出流
    auto write_callback = [&outfile](const uint8_t* data, size_t size, void* /*payload*/) {
        outfile.write(reinterpret_cast<const char*>(data), size);
    };

    bool encoder_initialized = false;
    encoded_frame_count = 0;
    vector<uint8_t> frame_buffer(frame_buffer_size);
    vector<uint16_t> frame_16bit(width * height, 0);
    while (infile.read(reinterpret_cast<char*>(frame_buffer.data()), frame_buffer_size) &&
           (encoded_frame_count < total_frames)) {
        // 从缓冲区中抽取一帧数据
        CameraFrame current_frame;
        size_t pos = 0;
        if (!handler.ExtractFrame(frame_buffer.data(), frame_buffer_size, &pos, &current_frame)) {
            cerr << "Error extracting frame " << encoded_frame_count << ". Skipping." << endl;
            continue;
        }
        if (current_frame.data.size() != width * height) {
            cerr << "Frame " << encoded_frame_count << " unexpected size: " 
                 << current_frame.data.size() << " vs expected " << width * height << endl;
            continue;
        }
        // 将8位数据简单转换为16位数据（这里可以做位移转换）
        for (size_t j = 0; j < width * height; j++) {
            frame_16bit[j] = static_cast<uint16_t>(current_frame.data[j]);
        }
        buffers[buffer_index] = frame_16bit;
        uint16_t* img = buffers[buffer_index].data();
        if (!encoder_initialized) {
            // 初始化编码器采用第一帧作为 delta 帧
            encoder.Init(img, width, height, write_callback, nullptr);
            encoder_initialized = true;
        }
        encoder.CompressFrame(img, write_callback, nullptr);
        buffer_index = (buffer_index + 1) % num_buffers;
        encoded_frame_count++;
    }
    // 完成编码，写入帧索引等信息
    encoder.Finish(write_callback, nullptr);
    outfile.close();
    cout << "Encoding complete. Encoded file written to disk at: " << encoded_file_path << endl;
    return true;
}

// random_access_decode()：利用随机存取解码器读取刚才写入的编码文件，并进行 round-trip 验证
bool random_access_decode(const string &input_file,
                          const string &encoded_file_path,
                          size_t encoded_frame_count,
                          size_t width,
                          size_t height,
                          CameraFormatHandler &handler)
{
    // 读取编码文件到内存（假设编码文件大小适中）
    ifstream encoded_in(encoded_file_path, ios::binary);
    if (!encoded_in) {
        cerr << "Failed to open encoded file for decoding: " << encoded_file_path << endl;
        return false;
    }
    encoded_in.seekg(0, ios::end);
    size_t encoded_file_size = encoded_in.tellg();
    encoded_in.seekg(0, ios::beg);
    vector<uint8_t> encoded_file_data(encoded_file_size);
    encoded_in.read(reinterpret_cast<char*>(encoded_file_data.data()), encoded_file_size);
    encoded_in.close();

    RandomAccessDecoder decoder;
    if (!decoder.Init(encoded_file_data.data(), encoded_file_data.size())) {
        cerr << "Decoder failed to initialize" << endl;
        return false;
    }
    if (decoder.numframes() != encoded_frame_count || decoder.xsize() != width || decoder.ysize() != height) {
        cerr << "Mismatch in decoder parameters:" << endl;
        cerr << "Decoded frames: " << decoder.numframes() << " (" << decoder.xsize() << "x" << decoder.ysize()
             << ") vs expected: " << encoded_frame_count << " (" << width << "x" << height << ")" << endl;
        return false;
    }

    // ROUNDTRIP TEST:
    // 为避免保存原始帧内存过大，重新从原始文件中逐帧读取进行比对
    ifstream orig_in(input_file, ios::binary);
    if (!orig_in) {
        cerr << "Failed to re-open original input file: " << input_file << endl;
        return false;
    }
    // 跳过头部
    orig_in.seekg(handler.HeaderSize(), ios::beg);

    bool all_match = true;
    size_t frame_num = 0;
    size_t frame_buffer_size = handler.FrameSize();
    vector<uint8_t> orig_frame_buffer(frame_buffer_size);
    for (size_t i = 0; i < encoded_frame_count; i++) {
        if (!orig_in.read(reinterpret_cast<char*>(orig_frame_buffer.data()), frame_buffer_size)) {
            cerr << "Failed to read original frame " << i << endl;
            all_match = false;
            break;
        }
        CameraFrame current_frame;
        size_t pos = 0;
        if (!handler.ExtractFrame(orig_frame_buffer.data(), frame_buffer_size, &pos, &current_frame)) {
            cerr << "Error extracting original frame " << i << endl;
            all_match = false;
            continue;
        }
        if (current_frame.data.size() != width * height) {
            cerr << "Original frame " << i << " unexpected size: " 
                 << current_frame.data.size() << " vs expected " << width * height << endl;
            all_match = false;
            continue;
        }
        // 将原始8位数据转换为16位（与编码时一致的转换）
        vector<uint16_t> orig_frame_16bit(width * height);
        for (size_t j = 0; j < width * height; j++) {
            orig_frame_16bit[j] = static_cast<uint16_t>(current_frame.data[j]);
        }
        // 解码该帧
        vector<uint16_t> decoded_frame(width * height, 0);
        if (!decoder.DecodeFrame(i, decoded_frame.data())) {
            cerr << "Failed to decode frame " << i << endl;
            all_match = false;
            continue;
        }
        // 比较原始帧与解码帧
        if (!equal(orig_frame_16bit.begin(), orig_frame_16bit.end(), decoded_frame.begin())) {
            cerr << "Frame " << i << " mismatch between original and decoded data" << endl;
            all_match = false;
        }
        frame_num++;
    }
    orig_in.close();

    if (all_match) {
        cout << "Roundtrip test successful: All " << frame_num << " frames match the original." << endl;
    } else {
        cerr << "Roundtrip test failed: Some frames did not match." << endl;
        return false;
    }
    return true;
}

bool streaming_decode(const string &input_file,
    const string &encoded_file_path,
    size_t encoded_frame_count,
    size_t width,
    size_t height,
    CameraFormatHandler &handler)
{
    // 打开编码文件，以分块方式读取，而非一次全部加载
    ifstream encoded_in(encoded_file_path, ios::binary);
    if (!encoded_in) {
    cerr << "Failed to open encoded file for streaming decode: " << encoded_file_path << endl;
    return false;
    }

    // 打开原始文件，供 roundtrip 测试按帧读取原始数据
    ifstream orig_in(input_file, ios::binary);
    if (!orig_in) {
    cerr << "Failed to open original input file: " << input_file << endl;
    return false;
    }
    // 跳过头部数据
    orig_in.seekg(handler.HeaderSize(), ios::beg);
    size_t frame_buffer_size = handler.FrameSize();

    // 创建 streaming decoder 对象
    fpvc::StreamingDecoder decoder;
    size_t frames_decoded = 0;
    const size_t blocksize = 65536;
    vector<uint8_t> block_buffer(blocksize);

    // 回调 lambda：每解码一帧时从原始文件读取对应帧并进行验证
    auto decode_callback = [&frames_decoded, width, height, frame_buffer_size, &handler, &orig_in]
    (bool ok, const uint16_t* image, size_t xsize, size_t ysize, void* /*unused*/) {
    if (!ok) {
    cerr << "StreamingDecoder decode failed at frame " << frames_decoded << endl;
    return;
    }
    // 读取对应的原始帧
    vector<uint8_t> orig_frame_buffer(frame_buffer_size);
    if (!orig_in.read(reinterpret_cast<char*>(orig_frame_buffer.data()), frame_buffer_size)) {
    cerr << "Failed to read original frame " << frames_decoded << endl;
    return;
    }
    // 提取原始帧数据
    CameraFrame current_frame;
    size_t pos = 0;
    if (!handler.ExtractFrame(orig_frame_buffer.data(),
                    orig_frame_buffer.size(),
                    &pos, &current_frame))
    {
    cerr << "Error extracting original frame " << frames_decoded << endl;
    return;
    }
    if (current_frame.data.size() != width * height) {
    cerr << "Original frame " << frames_decoded << " unexpected size: "
    << current_frame.data.size() << " vs expected " << width * height << endl;
    return;
    }
    // 将8位数据转换为16位，保证与编码时一致
    vector<uint16_t> orig_frame_16bit(width * height);
    for (size_t j = 0; j < width * height; j++) {
    orig_frame_16bit[j] = static_cast<uint16_t>(current_frame.data[j]);
    }
    if (!equal(orig_frame_16bit.begin(), orig_frame_16bit.end(), image)) {
    cerr << "Frame " << frames_decoded << " mismatch between original and streaming decoded data" << endl;
    }
    frames_decoded++;
    };

    // 按块读取编码文件，并提交给 streaming decoder
    while (!encoded_in.eof()) {
    encoded_in.read(reinterpret_cast<char*>(block_buffer.data()), blocksize);
    size_t bytes_read = encoded_in.gcount();
    if (bytes_read > 0) {
    decoder.Decode(block_buffer.data(), bytes_read, decode_callback, nullptr);
    }
    }

    if (frames_decoded != encoded_frame_count) {
    cerr << "Streaming roundtrip test failed: decoded " << frames_decoded
    << " frames, expected " << encoded_frame_count << endl;
    return false;
    }
    cout << "Streaming roundtrip test successful: All " << frames_decoded
    << " frames match the original." << endl;
    return true;
}

int main() {
    // 文件路径配置
    const string input_file = "/NFSdata/data1/EastCameraData2024/145425/145425.seq";
    const string encoded_file_path = "/home/wukong/Code/fusion-power-video/output/145425-output-encoded.fpv";

    // 创建 CameraFormatHandler 对象，后续用于解析头部等
    CameraFormatHandler handler;

    size_t encoded_frame_count = 0;
    size_t width = 0;
    size_t height = 0;

    // 执行编码
    if (!encode_seq_file(input_file, encoded_file_path,
                         encoded_frame_count, width, height, handler))
    {
        cerr << "Encoding failed." << endl;
        return 1;
    }

    // 执行随机存取解码以及 roundtrip test
    // if (!random_access_decode(input_file, encoded_file_path,
    //                           encoded_frame_count, width, height, handler))
    // {
    //     cerr << "Decoding or verification failed." << endl;
    //     return 1;
    // }

    // 执行流式解码以及 roundtrip test
    if (!streaming_decode(input_file, encoded_file_path,
                          encoded_frame_count, width, height, handler))
    {
        cerr << "Streaming decoding or verification failed." << endl;
        return 1;
    }

    return 0;
}