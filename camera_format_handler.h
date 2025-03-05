#ifndef CAMERA_FORMAT_HANDLER_H_
#define CAMERA_FORMAT_HANDLER_H_

#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include "fusion_power_video.h"

namespace fpvc {

// Structure to hold camera frame data
struct CameraFrame {
  int64_t timestamp;
  std::vector<uint8_t> data;
};

// Structure to hold camera header information - simplified to only essential fields
struct CameraHeader {
  uint32_t width;
  uint32_t height;
  uint16_t bit_depth;
  uint32_t kept_frame_count;
};

class CameraFormatHandler {
 public:
  CameraFormatHandler();
  ~CameraFormatHandler();

  // Parse header from the camera format file
  bool ParseHeader(const uint8_t* data, size_t size);
  
  // Create a header with specified parameters
  std::vector<uint8_t> CreateHeader(uint32_t width, uint32_t height, uint16_t bit_depth, uint32_t kept_frame_count);
  
  // Get header bytes to write when saving camera format file
  std::vector<uint8_t> GetHeaderBytes() const { return header_; }
  
  // Get parsed header information
  const CameraHeader& GetHeaderInfo() const { return header_info_; }
  
  // Extract a single frame from the data
  bool ExtractFrame(const uint8_t* data, size_t size, size_t* pos, CameraFrame* frame);
  
  // Create a Frame object from CameraFrame (for encoding)
  Frame CreateFrameFromCameraFrame(const CameraFrame& camera_frame);
  
  // Convert fusion-power-video frame back to camera format (for decoding)
  CameraFrame ConvertToCameraFrame(const uint16_t* frame_data, size_t xsize, size_t ysize, int64_t timestamp);
  
  // Helper function to write a camera frame to buffer
  void WriteCameraFrame(const CameraFrame& frame, std::vector<uint8_t>* buffer);
  
  // Returns the fixed dimensions of this camera format
  size_t Width() const { return header_info_.width; }
  size_t Height() const { return header_info_.height; }
  static constexpr size_t HeaderSize() { return 14; }  // Updated size with simplified fields
  static constexpr size_t TimestampSize() { return 8; }
  
  // Frame data size based on header dimensions
  size_t FrameDataSize() const { return header_info_.width * header_info_.height; }
  size_t FrameSize() const { return TimestampSize() + FrameDataSize(); }
  
 private:
  std::vector<uint8_t> header_;
  CameraHeader header_info_;
};

} // namespace fpvc

#endif // CAMERA_FORMAT_HANDLER_H_
