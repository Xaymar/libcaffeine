// Copyright 2019 Caffeine Inc. All rights reserved.

#define LIBCAFFEINE_LIBRARY

#include "caffeine.h"

#include <vector>

#include "Interface.hpp"
#include "LogSink.hpp"
#include "Stream.hpp"

#include "rtc_base/ssladapter.h"

#ifdef _WIN32
#include "rtc_base/win32socketinit.h"
#endif

using namespace caff;

CAFFEINE_API char const* caff_errorString(caff_Error error)
{
    switch (error) {
    case caff_Error_SdpOffer:
        return "Error making SDP offer";
    case caff_Error_SdpAnswer:
        return "Error reading SDP answer";
    case caff_Error_IceTrickle:
        return "Error during ICE negotiation";
    case caff_Error_Disconnected:
        return "Disconnected from server";
    case caff_Error_Takeover:
        return "Stream takeover";
    case caff_Error_Unknown:
    default:
        return "Unknown error";
    }
}

CAFFEINE_API caff_InterfaceHandle caff_initialize(caff_LogCallback logCallback, caff_LogLevel min_severity)
{
    RTC_DCHECK(logCallback);

    // TODO: make this thread safe
    static bool firstInit = true;
    if (firstInit) {
        // Set up logging
        rtc::LogMessage::LogThreads(true);
        rtc::LogMessage::LogTimestamps(true);

        // Send logs only to our log sink. Not to stderr, windows debugger, etc
        // rtc::LogMessage::LogToDebug(rtc::LoggingSeverity::LS_NONE);
        // rtc::LogMessage::SetLogToStderr(false);

        // TODO: Figure out why this log sink isn't working and uncomment above two
        // lines
        rtc::LogMessage::AddLogToStream(new LogSink(logCallback), static_cast<rtc::LoggingSeverity>(min_severity));

        // Initialize WebRTC

#ifdef _WIN32
        rtc::EnsureWinsockInit();
#endif
        if (!rtc::InitializeSSL()) {
            RTC_LOG(LS_ERROR) << "Caffeine RTC failed to initialize SSL";
            return nullptr;
        }

        RTC_LOG(LS_INFO) << "Caffeine RTC initialized";
        firstInit = false;
    }

    auto interface = new Interface;
    return reinterpret_cast<caff_InterfaceHandle>(interface);
}

CAFFEINE_API bool caff_isSupportedVersion()
{
    return isSupportedVersion();
}

CAFFEINE_API caff_AuthResponse * caff_signin(char const * username, char const * password, char const * otp)
{
    return caffSignin(username, password, otp);
}

CAFFEINE_API caff_CredentialsHandle caff_refreshAuth(char const * refreshToken)
{
    RTC_DCHECK(refreshToken);
    return reinterpret_cast<caff_CredentialsHandle>(refreshAuth(refreshToken));
}

CAFFEINE_API void caff_freeCredentials(caff_CredentialsHandle * credentialsHandle)
{
    // TODO this may do bad things
    auto credentials = reinterpret_cast<Credentials **>(credentialsHandle);
    if (credentials && credentialsHandle) {
        delete *credentials;
        *credentials = nullptr;
    }
}

CAFFEINE_API void caff_freeAuthResponse(caff_AuthResponse ** authResponse)
{
    if (authResponse && *authResponse) {
        caff_freeCredentials(&(*authResponse)->credentials);

        delete[](*authResponse)->next;
        delete[](*authResponse)->mfaOtpMethod;

        delete *authResponse;
        *authResponse = nullptr;
    }
}

CAFFEINE_API char const * caff_getRefreshToken(caff_CredentialsHandle credentialsHandle)
{
    RTC_DCHECK(credentialsHandle);
    auto creds = reinterpret_cast<Credentials *>(credentialsHandle);
    return creds->refreshToken.c_str();
}

CAFFEINE_API caff_UserInfo * caff_getUserInfo(caff_CredentialsHandle credentialsHandle)
{
    RTC_DCHECK(credentialsHandle);
    return getUserInfo(reinterpret_cast<Credentials *>(credentialsHandle));
}

CAFFEINE_API void caff_freeUserInfo(caff_UserInfo ** userInfo)
{
    if (userInfo && *userInfo) {
        delete[](*userInfo)->caid;
        delete[](*userInfo)->username;
        delete[](*userInfo)->stageId;
        delete *userInfo;
        *userInfo = nullptr;
    }
}

CAFFEINE_API caff_GameList * caff_getGameList()
{
    return getSupportedGames();
}

CAFFEINE_API void caff_freeGameInfo(caff_GameInfo ** info)
{
    if (info && *info) {
        for (size_t i = 0; i < (*info)->numProcessNames; ++i) {
            delete[](*info)->processNames[i];
            (*info)->processNames[i] = nullptr;
        }
        delete[](*info)->id;
        delete[](*info)->name;
        delete[](*info)->processNames;
        delete *info;
        *info = nullptr;
    }
}

CAFFEINE_API void caff_freeGameList(caff_GameList ** games)
{
    if (games && *games) {
        for (size_t i = 0; i < (*games)->numGames; ++i) {
            caff_freeGameInfo(&(*games)->gameInfos[i]);
        }
        delete[](*games)->gameInfos;
        delete *games;
        *games = nullptr;
    }
}

CAFFEINE_API caff_StreamHandle caff_startStream(
    caff_InterfaceHandle interfaceHandle,
    void * user_data,
    caff_CredentialsHandle credentialsHandle,
    char const * username,
    char const * title,
    caff_Rating rating,
    caff_StreamStartedCallback startedCallbackPtr,
    caff_StreamFailedCallback failedCallbackPtr)
{
    RTC_DCHECK(interfaceHandle);
    RTC_DCHECK(startedCallbackPtr);
    RTC_DCHECK(failedCallbackPtr);

    // Encapsulate void * inside lambdas, and other C++ -> C translations
    auto startedCallback = [=] { startedCallbackPtr(user_data); };
    auto failedCallback = [=](caff_Error error) {
        failedCallbackPtr(user_data, error);
    };

    auto interface = reinterpret_cast<Interface*>(interfaceHandle);
    auto credentials = reinterpret_cast<Credentials*>(credentialsHandle);
    auto stream = interface->startStream(credentials, username, title, rating, startedCallback, failedCallback);

    return reinterpret_cast<caff_StreamHandle>(stream);
}

CAFFEINE_API void caff_sendAudio(
    caff_StreamHandle streamHandle,
    uint8_t* samples,
    size_t samples_per_channel)
{
    RTC_DCHECK(streamHandle);
    RTC_DCHECK(samples);
    RTC_DCHECK(samples_per_channel);
    auto stream = reinterpret_cast<Stream*>(streamHandle);
    stream->sendAudio(samples, samples_per_channel);
}

CAFFEINE_API void caff_sendVideo(
    caff_StreamHandle streamHandle,
    uint8_t const* frameData,
    size_t frameBytes,
    int32_t width,
    int32_t height,
    caff_VideoFormat format)
{
    RTC_DCHECK(frameData);
    RTC_DCHECK(frameBytes);
    RTC_DCHECK(width);
    RTC_DCHECK(height);
    RTC_DCHECK(format);

    auto stream = reinterpret_cast<Stream*>(streamHandle);
    stream->sendVideo(frameData, frameBytes, width, height, format);
}

CAFFEINE_API caff_ConnectionQuality caff_getConnectionQuality(caff_StreamHandle streamHandle)
{
    RTC_DCHECK(streamHandle);

    auto stream = reinterpret_cast<Stream*>(streamHandle);
    return stream->getConnectionQuality();
}

CAFFEINE_API void caff_endStream(caff_StreamHandle* streamHandle)
{
    RTC_DCHECK(streamHandle);
    RTC_DCHECK(*streamHandle);
    auto stream = reinterpret_cast<Stream*>(*streamHandle);
    delete stream;
    *streamHandle = nullptr;
    RTC_LOG(LS_INFO) << "Caffeine stream ended";
}

CAFFEINE_API void caff_deinitialize(caff_InterfaceHandle* interfaceHandle)
{
    RTC_DCHECK(interfaceHandle);
    RTC_DCHECK(*interfaceHandle);
    auto interface = reinterpret_cast<Interface*>(*interfaceHandle);
    delete interface;
    *interfaceHandle = nullptr;
    RTC_LOG(LS_INFO) << "Caffeine RTC deinitialized";
}