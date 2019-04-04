// Copyright 2019 Caffeine Inc. All rights reserved.

#include "Interface.hpp"

#include "AudioDevice.hpp"
#include "Stream.hpp"
#include "X264Encoder.hpp"

#include "api/audio_codecs/builtin_audio_decoder_factory.h"
#include "api/audio_codecs/builtin_audio_encoder_factory.h"
#include "api/peerconnectioninterface.h"
#include "api/video_codecs/builtin_video_decoder_factory.h"
#include "api/video_codecs/builtin_video_encoder_factory.h"
#include "api/video_codecs/sdp_video_format.h"

#include "media/base/h264_profile_level_id.h"
#include "media/base/mediaconstants.h"
#include "modules/audio_processing/include/audio_processing.h"

#include "rtc_base/thread.h"

namespace caff {

    // TODO: Use hardware encoding on low powered cpu
    class EncoderFactory : public webrtc::VideoEncoderFactory {
    public:
        virtual ~EncoderFactory() {}

        virtual std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override
        {
            auto profileId = webrtc::H264::ProfileLevelId(
                webrtc::H264::kProfileConstrainedBaseline, webrtc::H264::kLevel3_1);
            auto name = cricket::kH264CodecName;
            auto profile_string = webrtc::H264::ProfileLevelIdToString(profileId);
            std::map<std::string, std::string> parameters = {
                {cricket::kH264FmtpProfileLevelId, profile_string.value()} };
            return { webrtc::SdpVideoFormat(name, parameters) };
        }

        virtual CodecInfo QueryVideoEncoder(const webrtc::SdpVideoFormat& format) const override
        {
            return { false, false };
        }

        virtual std::unique_ptr<webrtc::VideoEncoder> CreateVideoEncoder(
            const webrtc::SdpVideoFormat& format) override
        {
            return std::make_unique<X264Encoder>(cricket::VideoCodec(format));
        }
    };

    Interface::Interface()
    {
        networkThread = rtc::Thread::CreateWithSocketServer();
        networkThread->SetName("caffeine-network", nullptr);
        networkThread->Start();

        workerThread = rtc::Thread::Create();
        workerThread->SetName("caffeine-worker", nullptr);
        workerThread->Start();

        signalingThread = rtc::Thread::Create();
        signalingThread->SetName("caffeine-signaling", nullptr);
        signalingThread->Start();

        audioDevice = workerThread->Invoke<rtc::scoped_refptr<AudioDevice>>(
            RTC_FROM_HERE, [] { return new AudioDevice(); });

        factory = webrtc::CreatePeerConnectionFactory(
            networkThread.get(), workerThread.get(), signalingThread.get(),
            audioDevice, webrtc::CreateBuiltinAudioEncoderFactory(),
            webrtc::CreateBuiltinAudioDecoderFactory(),
            std::make_unique<EncoderFactory>(),
            webrtc::CreateBuiltinVideoDecoderFactory(), nullptr, nullptr);
    }

    Interface::~Interface() {
        factory = nullptr;
    }

    caff_AuthResult Interface::signIn(char const * username, char const * password, char const * otp)
    {
        auto response = caff::signIn(username, password, otp);
        if (response.credentials) {
            refreshToken = response.credentials->refreshToken;
            sharedCredentials.emplace(std::move(*response.credentials));
            return updateUserInfo(response.result);
        }
        return response.result;
    }

    caff_AuthResult Interface::refreshAuth(char const * refreshToken)
    {
        auto response = caff::refreshAuth(refreshToken);
        if (response.credentials) {
            sharedCredentials.emplace(std::move(*response.credentials));
            this->refreshToken = refreshToken;
            return updateUserInfo(response.result);
        }
        return response.result;
    }

    caff_AuthResult Interface::updateUserInfo(caff_AuthResult origResult)
    {
        userInfo = caff::getUserInfo(*sharedCredentials);
        if (userInfo) {
            return origResult;
        }
        sharedCredentials.reset();
        this->refreshToken.reset();
        return caff_AuthResult_RequestFailed;
    }

    void Interface::signOut()
    {
        sharedCredentials.reset();
        refreshToken.reset();
        userInfo.reset();
    }

    bool Interface::isSignedIn() const
    {
        return sharedCredentials.has_value() && userInfo.has_value();
    }

    char const * Interface::getRefreshToken() const
    {
        return refreshToken ? refreshToken->c_str() : nullptr;
    }

    char const * Interface::getUsername() const
    {
        return userInfo ? userInfo->username.c_str() : nullptr;
    }

    char const * Interface::getStageId() const
    {
        return userInfo ? userInfo->stageId.c_str() : nullptr;
    }

    bool Interface::canBroadcast() const
    {
        return userInfo ? userInfo->canBroadcast : false;
    }

    Stream * Interface::startStream(
        std::string title,
        caff_Rating rating,
        std::function<void()> startedCallback,
        std::function<void(caff_Error)> failedCallback)
    {
        if (!isSignedIn()) {
            failedCallback(caff_Error_NotSignedIn);
            return nullptr;
        }
        auto stream = new Stream(*sharedCredentials, userInfo->username, title, rating, audioDevice, factory);
        stream->start(startedCallback, failedCallback);
        return stream;
    }

}  // namespace caff
