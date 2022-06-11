#include "stdafx.h"

#include "ImageUpscale.h"

#if __has_include("AC.hpp")

#include"AC.hpp"
#include"ACCreator.hpp"
#include"ACProcessor.hpp"
#include "Anime4KCPP.hpp"


const int currPlatformID = 0;
const int currDeviceID = 0;

bool CanUpscaleImage()
{
    static const bool ok = LoadLibrary(_T("opencl.dll")) != NULL
        && Anime4KCPP::OpenCL::checkGPUSupport(currPlatformID, currDeviceID);
    return ok;
}

bool EnableImageUpscale()
{
    static Anime4KCPP::ACInitializer initializer;

    static const auto ok = [] {
        const int OpenCLQueueNum = 1;
        const bool OpenCLParallelIO = false;

        initializer.pushManager<Anime4KCPP::OpenCL::Manager<Anime4KCPP::OpenCL::ACNet>>(
            currPlatformID, currDeviceID,
            Anime4KCPP::CNNType::Default,
            OpenCLQueueNum,
            OpenCLParallelIO);

        return initializer.init() == initializer.size();
    }();
    return ok;
}

void ImageUpscale(uint8_t* input, int inputStride, int inputWidth, int inputHeight, std::vector<uint8_t>& output, int& outputWidth, int& outputHeight)
{
    Anime4KCPP::Parameters param{};
    auto ac = Anime4KCPP::ACCreator::createUP(param, Anime4KCPP::Processor::Type::OpenCL_ACNet);

    const cv::Mat y_image(inputHeight, inputWidth, CV_8UC1, input, inputStride);
    const cv::Mat uv_image(inputHeight / 2, inputWidth / 2, CV_8UC2, input + inputHeight * inputStride, inputStride);
    cv::Mat cbcr_channels[2];
    split(uv_image, cbcr_channels);

    ac->loadImage(y_image, cbcr_channels[0], cbcr_channels[1]);

    ac->process();

    cv::Mat out_y_image;
    std::vector <cv::Mat> out_cbcr_channels(2);

    ac->saveImage(out_y_image, out_cbcr_channels[0], out_cbcr_channels[1]);

    outputWidth = out_y_image.cols;
    outputHeight = out_y_image.rows;

    cv::Mat CrCb;
    merge(out_cbcr_channels, CrCb);

    output.assign(out_y_image.datastart, out_y_image.dataend);
    output.insert(output.end(), CrCb.datastart, CrCb.dataend);
}

#else

bool CanUpscaleImage()
{
    return false;
}

bool EnableImageUpscale()
{
    return false;
}

void ImageUpscale(uint8_t*, int, int, int, std::vector<uint8_t>&, int&, int&)
{
}

#endif
