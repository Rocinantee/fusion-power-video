#include <fstream>
#include <iostream>
#include <vector>
#include "fusion_power_video.h"
#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <filesystem>

using namespace std;


int randomDecode() {
    // File setup
    std::string input_file = "/home/wukong/Code/fusion-power-video/output/145425-output-pp.fpv";

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
    
    size_t shift = 8;
    bool big_endian = false;
    // Get dimensions and frame count
    size_t width = decoder.xsize();
    size_t height = decoder.ysize();
    size_t frame_count = decoder.numframes();
    
    std::cout << "File contains " << frame_count << " frames of size " 
              << width << "x" << height << std::endl;
    
    // Decode each frame
    
    std::vector<uint16_t> frame(width * height);
    std::vector<uint8_t> after(width * height);

    string verified_frames_path = "/home/wukong/Code/fusion-power-video/output/145425-all/";

    for(size_t i = 0; i < frame_count;i+=100)
    {
        string frame_path_original = verified_frames_path + std::to_string(i) + ".bmp";

        if(!decoder.DecodeFrame(i, frame.data()))
        {
            std::cerr << "Failed to decode frame " << i << std::endl;
            continue;
        }
        size_t numpixels = height * width;
        for (size_t j = 0; j < numpixels; j++) {
          uint16_t u = frame[i];
          u >>= shift;
          u = u & 0xff;
          after[j] = u;
        }

        cv::Mat frame_Mat_original = cv::imread(frame_path_original, cv::IMREAD_GRAYSCALE);
        
        cv::Mat frame_Mat = cv::Mat(height, width, CV_8UC1,after.data());

        //frame_mat original rotate 90 degree
        cv::rotate(frame_Mat_original, frame_Mat_original, cv::ROTATE_90_COUNTERCLOCKWISE);
        //caculate the difference
        cv::Mat diff;
        cv::absdiff(frame_Mat, frame_Mat_original, diff);
        // add the difference gray value
        long long int diff_sum = cv::sum(diff)[0];
        if(diff_sum > 0)
        {                
            cout << "Frame " << i << " difference: " << diff_sum << endl;
            string frame_path_diff = "/home/wukong/Code/fusion-power-video/output/diff/decoded_8bit_" + std::to_string(i) + "_diff.bmp";
            cv::imwrite(frame_path_diff ,diff);
            //输出解码后的图像
            string frame_path = "/home/wukong/Code/fusion-power-video/output/extracted-frames/decoded_8bit_" + std::to_string(i) + ".bmp";
            cv::imwrite(frame_path, frame_Mat);
        }
        else
            cout << "Frame " << i << " is the same" << endl;

            
        }


    // for (size_t i = 0; i < 1;i++)
    // {
    //     string frame_path_original = verified_frames_path + std::to_string(i) + ".bmp";
    //     if (!decoder.DecodeFrame(i, frame.data())) {
    //         std::cerr << "Failed to decode frame " << i << std::endl;
    //         continue;
    //     }
    //     // for (size_t j = 0; j < width * height; j++) {
    //     //     frame_8bit[j] = static_cast<u_int8_t>((frame[j] >> 8)&0xff);
    //     // }
        
        
    //     fpvc::UnextractFrame(frame.data(), width,height, shift, big_endian, after.data());

    //     // cv::Mat frame_Mat = cv::Mat(height, width, CV_8UC1);

    //     // for(int i = 0; i < height; i++)
    //     // {
    //     //     for(int j = 0; j < width; j++)
    //     //     {
    //     //         frame_Mat.at<uchar>(i,j) = static_cast<uchar>(after[i*width + j]);
    //     //     }
    //     // }


    //     // cv::Mat grayImage(height, width, CV_8UC1);
    //     // // 将 std::vector 中的数据复制到 cv::Mat 中
    //     // std::memcpy(grayImage.data, data.data(), data.size());
        
    //     string frame_path = "/home/wukong/Code/fusion-power-video/output/extracted-frames/decoded_8bit_" + std::to_string(i) + ".bmp";

    //     //cv::imwrite(frame_path, frame_Mat);
        


    //     // if (!std::filesystem::exists(frame_path_original)) {
    //     //     std::cerr << "File not found: " << frame_path_original << std::endl;
    //     //     continue;
    //     // }
        
    //     // // 2. 以灰度模式加载图像 (确保格式匹配)
    //     // cv::Mat frame_Mat_original = cv::imread(frame_path_original, cv::IMREAD_GRAYSCALE);
        
        
    //     // //frame_mat original rotate 90 degree
    //     // cv::rotate(frame_Mat_original, frame_Mat_original, cv::ROTATE_90_COUNTERCLOCKWISE);
    //     // //caculate the difference
    //     // cv::Mat diff;
    //     // cv::absdiff(frame_Mat, frame_Mat_original, diff);
    //     // // add the difference gray value
    //     // long long int diff_sum = cv::sum(diff)[0];
    //     // if(diff_sum > 0)
    //     // {                
    //     //     cout << "Frame " << i << " difference: " << diff_sum << endl;
    //     //     string frame_path_diff = "/home/wukong/Code/fusion-power-video/output/diff/decoded_8bit_" + std::to_string(i) + "_diff.bmp";
    //     //     cv::imwrite(frame_path_diff ,diff);
    //     // }
    //     // else
    //     //     cout << "Frame " << i << " is the same" << endl;


    // }
    
    return 0;
}

void decodeFrame()
{



    
}

void inverseFrame()
{
    string frame_path = "/home/wukong/Code/fusion-power-video/output/diff/";
    string new_frame_path = "/home/wukong/Code/fusion-power-video/output/diff_inverse/";
    // for all files in frame_path
    for (const auto & entry : std::filesystem::directory_iterator(frame_path))
    {
        string frame_path = entry.path();
        cv::Mat frame_Mat = cv::imread(frame_path);
        cv::Mat frame_Mat_inverse;
        cv::bitwise_not(frame_Mat, frame_Mat_inverse);
        string frame_path_inverse = new_frame_path + entry.path().filename().string();
        cv::rotate(frame_Mat_inverse, frame_Mat_inverse, cv::ROTATE_90_CLOCKWISE);
        cv::imwrite(frame_path_inverse, frame_Mat_inverse);
    }


}

int test_output_equall()
{

    string verified_frames_path = "/home/wukong/Code/fusion-power-video/output/145425-all/";
    vector<int> verified_frames;
    for(int i =0 ; i < 4000;i+=1)
    {
        verified_frames.emplace_back(i);
    }
    
    for(auto i : verified_frames)
    {
        string frame_path = "/home/wukong/Code/fusion-power-video/output/int-output/decoded_8bit_" + std::to_string(i) + ".bmp";
        string frame_path_original = verified_frames_path + std::to_string(i) + ".bmp";

        if (!std::filesystem::exists(frame_path_original)) {
            std::cerr << "File not found: " << frame_path_original << std::endl;
            continue;
        }

        cv::Mat frame_Mat = cv::imread(frame_path,cv::IMREAD_GRAYSCALE);
        cv::Mat frame_Mat_original = cv::imread(frame_path_original,cv::IMREAD_GRAYSCALE);
        
        cv::rotate(frame_Mat, frame_Mat, cv::ROTATE_90_CLOCKWISE);

        //caculate the difference
        cv::Mat diff;
        cv::absdiff(frame_Mat, frame_Mat_original, diff);
        // add the difference gray value
        long long int diff_sum = cv::sum(diff)[0];

        
        if(diff_sum > 0)
        {                
            cout << "Frame " << i << " difference: " << diff_sum << endl;
            cv::bitwise_not(diff,diff);
            string frame_path_diff = "/home/wukong/Code/fusion-power-video/output/diff_inverse/decoded_8bit_" + std::to_string(i) + "_diff.bmp";
            cv::imwrite(frame_path_diff ,diff);
        }
        else
            cout << "Frame " << i << " is the same" << endl;

    }
}

int main()
{

    test_output_equall();
    return 0;
}