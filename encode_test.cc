#include <fstream>
#include <memory>
#include <vector>
#include <string>
#include <iostream>
#include <cstddef>
#include "fusion_power_video.h"
#include "fusion_power_video.cc"
#include "camera_format_handler.h"

using namespace std;
using namespace fpvc;

int encodeCameraFile()
{
  // 文件设置
  const std::string input_file = "/NFSdata/data1/EastCameraData2024/145425/145425.seq";
  const std::string output_file = "/home/wukong/Code/fusion-power-video/output/output-test.fpv";
  std::ifstream infile(input_file, std::ios::binary);
  std::ofstream outfile(output_file, std::ios::binary);

  if (!infile) {
    std::cerr << "Failed to open input file: " << input_file << std::endl;
    return 1;
  }

  if (!outfile) {
    std::cerr << "Failed to open output file: " << output_file << std::endl;
    return 1;
  }

  // 初始化相机格式处理器
  fpvc::CameraFormatHandler handler;

  // 读取原始头部作为参考
  std::vector<uint8_t> original_header(handler.HeaderSize());
  if (!infile.read(reinterpret_cast<char *>(original_header.data()), handler.HeaderSize())) {
    std::cerr << "Failed to read header" << std::endl;
    return 1;
  }

  if (!handler.ParseHeader(original_header.data(), original_header.size())) {
    std::cerr << "Failed to parse header" << std::endl;
    return 1;
  }

  // 获取解析头部的帧尺寸
  const fpvc::CameraHeader &header_info = handler.GetHeaderInfo();
  const size_t width = header_info.width;
  const size_t height = header_info.height;
  const uint16_t bit_depth = header_info.bit_depth;
  size_t total_frames = header_info.kept_frame_count;

  std::cout << "Input file info:" << std::endl;
  std::cout << "- Dimensions: " << width << "x" << height << std::endl;
  std::cout << "- Bit depth: " << bit_depth << std::endl;
  std::cout << "- Total frames: " << total_frames << std::endl;
  
  // 计算单帧大小
  size_t frame_size_bytes = width * height;
  std::cout << "- Frame size: " << frame_size_bytes << " bytes" << std::endl;

  // 创建用于一次读取一帧的帧缓冲区
  std::vector<uint8_t> frame_buffer(handler.FrameSize());
  std::cout << "- Handler frame size: " << handler.FrameSize() << " bytes" << std::endl;

  // 使用shift=8初始化编码器，用于8位数据
  const int shift = 8; // 重要：对于8位数据使用8
  const bool big_endian = false;
  const int num_threads = 8;
  fpvc::Encoder encoder(num_threads, shift, big_endian);

  // 写入回调
  size_t total_bytes_written = 0;
  auto write_callback = [&outfile, &total_bytes_written](const uint8_t *data, size_t size, void *) {
    outfile.write(reinterpret_cast<const char *>(data), size);
    total_bytes_written += size;
    outfile.flush(); // 确保数据立即写入
  };

  // 帧数据缓冲区
  std::vector<uint16_t> buffer_16bit(width * height);
  size_t frames_processed = 0;
  bool first_frame = true;
  
  // 解除1000帧的限制 - 处理所有帧或限制更大的帧数
  const size_t max_frames = total_frames; // 或设置一个更合理的限制，比如10000

  for (size_t i = 0; i < max_frames; i++) {
    if (!infile.read(reinterpret_cast<char*>(frame_buffer.data()), handler.FrameSize())) {
      std::cerr << "Reached end of file after " << i << " frames" << std::endl;
      break;
    }

    // 提取帧
    fpvc::CameraFrame current_frame;
    size_t pos = 0;
    if (!handler.ExtractFrame(frame_buffer.data(), handler.FrameSize(), &pos, &current_frame)) {
      std::cerr << "Error extracting frame " << i << ". Skipping." << std::endl;
      continue;
    }

    // 确认数据数组大小匹配
    if (current_frame.data.size() != width * height) {
      std::cerr << "Error: Frame " << i << " has unexpected size: " 
                << current_frame.data.size() << " vs expected " << width * height << std::endl;
      continue;
    }

    // 修复BUG：在条件中使用j，而不是i
    for (size_t j = 0; j < width * height; j++) {
      buffer_16bit[j] = static_cast<uint16_t>(current_frame.data[j]);
    }

    try {
      if (first_frame) {
        // 使用增量帧初始化编码器
        std::cout << "Initializing encoder with first frame..." << std::endl;
        encoder.Init(buffer_16bit.data(), width, height, write_callback, nullptr);
        first_frame = false;
      } else {
        // 压缩每一帧
        encoder.CompressFrame(buffer_16bit.data(), write_callback, nullptr);
      }
      frames_processed++;
    } catch (const std::exception& e) {
      std::cerr << "Error processing frame " << i << ": " << e.what() << std::endl;
      continue;
    }

    // 每100帧显示进度
    if (i % 100 == 0 || i == max_frames - 1) {
      std::cout << "Processed " << i+1 << "/" << max_frames << " frames. "
                << "Bytes written: " << total_bytes_written << std::endl;
    }
  }
  
  // 完成编码（写入帧索引）
  std::cout << "Finalizing encoding..." << std::endl;
  encoder.Finish(write_callback, nullptr);

  std::cout << "\nEncoding completed:" << std::endl;
  std::cout << "- Total frames processed: " << frames_processed << std::endl;
  std::cout << "- Total bytes written: " << total_bytes_written << " bytes (" 
            << (total_bytes_written / (1024.0 * 1024.0)) << " MB)" << std::endl;
  std::cout << "- Average bytes per frame: " 
            << (frames_processed > 0 ? total_bytes_written / frames_processed : 0) << std::endl;
  
  return 0;
}

int main()
{
  encodeCameraFile();


  return 0;
}