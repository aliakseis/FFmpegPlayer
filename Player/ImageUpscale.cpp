#include "stdafx.h"

#include "ImageUpscale.h"

#include"AC.hpp"
#include"ACCreator.hpp"
#include"ACProcessor.hpp"
#include "Anime4KCPP.hpp"


const int currPlatformID = 0;
const int currDeviceID = 0;

bool CanUpscaleImage()
{
    static const bool ok = Anime4KCPP::OpenCL::checkGPUSupport(currPlatformID, currDeviceID);
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

    cv::Mat img(inputHeight, inputWidth, CV_8UC2, input, inputStride);

    cv::Mat ycbcr_channels[2];
    split(img, ycbcr_channels);

    auto cbcr_channel = ycbcr_channels[1].reshape(2);
    std::vector <cv::Mat> cbcr_channels;
    split(cbcr_channel, cbcr_channels);

    ac->loadImage(ycbcr_channels[0], cbcr_channels[0], cbcr_channels[1]);

    ac->process();

    cv::Mat u_img;
    std::vector <cv::Mat> out_cbcr_channels(2);

    ac->saveImage(u_img, out_cbcr_channels[0], out_cbcr_channels[1]);

    outputWidth = u_img.cols;
    outputHeight = u_img.rows;

    cv::Mat CrCb;
    merge(out_cbcr_channels, CrCb);
    auto CrCb_channel = CrCb.reshape(1);

    std::vector <cv::Mat> channels;
    channels.push_back(u_img);
    channels.push_back(CrCb_channel);

    cv::Mat merged_img;
    merge(channels, merged_img);

    output.assign(merged_img.datastart, merged_img.dataend);
}
