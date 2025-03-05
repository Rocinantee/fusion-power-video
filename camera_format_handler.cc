#include "camera_format_handler.h"
#include <cstring>
#include <iostream>
#include <algorithm>

namespace fpvc {

CameraFormatHandler::CameraFormatHandler() {
  // Default initialization
  header_info_.width = 1920;
  header_info_.height = 1080;
  header_info_.bit_depth = 8;
  header_info_.kept_frame_count = 0;
}

CameraFormatHandler::~CameraFormatHandler() {}

bool CameraFormatHandler::ParseHeader(const uint8_t* data, size_t size) {
  if (size < HeaderSize()) {
    std::cerr << "Data too small to contain header" << std::endl;
    return false;
  }
  
  // Store original header bytes
  header_.assign(data, data + HeaderSize());
  
  // Parse header fields - simplified structure
  header_info_.width = *reinterpret_cast<const uint32_t*>(data);
  header_info_.height = *reinterpret_cast<const uint32_t*>(data + 4);
  header_info_.bit_depth = *reinterpret_cast<const uint16_t*>(data + 8);
  header_info_.kept_frame_count = *reinterpret_cast<const uint32_t*>(data + 10);
  
  return true;
}

std::vector<uint8_t> CameraFormatHandler::CreateHeader(uint32_t width, uint32_t height, 
                                                     uint16_t bit_depth, uint32_t kept_frame_count) {
  std::vector<uint8_t> header(HeaderSize());
  
  // Write header fields
  *reinterpret_cast<uint32_t*>(header.data()) = width;
  *reinterpret_cast<uint32_t*>(header.data() + 4) = height;
  *reinterpret_cast<uint16_t*>(header.data() + 8) = bit_depth;
  *reinterpret_cast<uint32_t*>(header.data() + 10) = kept_frame_count;
  
  // Update header info
  header_info_.width = width;
  header_info_.height = height;
  header_info_.bit_depth = bit_depth;
  header_info_.kept_frame_count = kept_frame_count;
  
  header_ = header;
  return header;
}


bool CameraFormatHandler::ExtractFrame(const uint8_t* data, size_t size, size_t* pos, CameraFrame* frame) {
  if (size - *pos < FrameSize()) {
    return false;
  }
  
  // Extract timestamp (8 bytes) using direct memory access for better performance
  frame->timestamp = *reinterpret_cast<const int64_t*>(data + *pos);
  
  // Pre-allocate frame data if needed
  if (frame->data.size() != FrameDataSize()) {
    frame->data.resize(FrameDataSize());
  }
  
  // Use direct memory copy for better performance
  std::memcpy(frame->data.data(), data + *pos + TimestampSize(), FrameDataSize());
  
  // Update position
  *pos += FrameSize();
  
  return true;
}

Frame CameraFormatHandler::CreateFrameFromCameraFrame(const CameraFrame& camera_frame) {
  // Create a Frame object from the 8-bit grayscale data
  // For 8-bit data, we use the constructor that takes uint8_t*
  return Frame(header_info_.width, header_info_.height, camera_frame.data.data(), camera_frame.timestamp);
}

CameraFrame CameraFormatHandler::ConvertToCameraFrame(const uint16_t* frame_data, 
                                                     size_t xsize, size_t ysize, 
                                                     int64_t timestamp) {
  CameraFrame frame;
  frame.timestamp = timestamp;
  
  // Pre-allocate with exact size
  const size_t total_pixels = xsize * ysize;
  frame.data.resize(total_pixels);
  
  // Use aligned loads/stores when possible for better memory performance
  #if defined(__AVX2__)
    // AVX2 implementation would go here for processing 16 or 32 pixels at once
    // This is a placeholder for actual SIMD implementation
  #else
    // Fallback to optimized scalar implementation with loop unrolling
    const size_t chunk_size = 16;
    size_t i = 0;
    
    // Process in chunks for better cache performance and instruction pipelining
    for (; i + chunk_size <= total_pixels; i += chunk_size) {
      frame.data[i]      = (frame_data[i]      >> 8) & 0xFF;
      frame.data[i + 1]  = (frame_data[i + 1]  >> 8) & 0xFF;
      frame.data[i + 2]  = (frame_data[i + 2]  >> 8) & 0xFF;
      frame.data[i + 3]  = (frame_data[i + 3]  >> 8) & 0xFF;
      frame.data[i + 4]  = (frame_data[i + 4]  >> 8) & 0xFF;
      frame.data[i + 5]  = (frame_data[i + 5]  >> 8) & 0xFF;
      frame.data[i + 6]  = (frame_data[i + 6]  >> 8) & 0xFF;
      frame.data[i + 7]  = (frame_data[i + 7]  >> 8) & 0xFF;
      frame.data[i + 8]  = (frame_data[i + 8]  >> 8) & 0xFF;
      frame.data[i + 9]  = (frame_data[i + 9]  >> 8) & 0xFF;
      frame.data[i + 10] = (frame_data[i + 10] >> 8) & 0xFF;
      frame.data[i + 11] = (frame_data[i + 11] >> 8) & 0xFF;
      frame.data[i + 12] = (frame_data[i + 12] >> 8) & 0xFF;
      frame.data[i + 13] = (frame_data[i + 13] >> 8) & 0xFF;
      frame.data[i + 14] = (frame_data[i + 14] >> 8) & 0xFF;
      frame.data[i + 15] = (frame_data[i + 15] >> 8) & 0xFF;
    }
    
    // Process remaining items
    for (; i < total_pixels; ++i) {
      frame.data[i] = (frame_data[i] >> 8) & 0xFF;
    }
  #endif
  
  return frame;
}

void CameraFormatHandler::WriteCameraFrame(const CameraFrame& frame, std::vector<uint8_t>* buffer) {
  // Pre-reserve with exact size needed to prevent reallocations
  buffer->reserve(buffer->size() + TimestampSize() + frame.data.size());
  
  // More efficient direct memory write for timestamp
  const size_t original_size = buffer->size();
  buffer->resize(original_size + TimestampSize() + frame.data.size());
  
  // Write timestamp using direct memory access
  *reinterpret_cast<int64_t*>(buffer->data() + original_size) = frame.timestamp;
  
  // Use direct memcpy for frame data
  std::memcpy(buffer->data() + original_size + TimestampSize(), 
             frame.data.data(), frame.data.size());
}

} // namespace fpvc
