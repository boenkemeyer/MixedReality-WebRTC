// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

// This is a precompiled header, it must be on its own, followed by a blank
// line, to prevent clang-format from reordering it with other headers.
#include "pch.h"

#include "data_channel.h"
#include "interop/global_factory.h"
#include "interop/interop_api.h"
#include "local_video_track.h"
#include "peer_connection.h"
#include "sdp_utils.h"

using namespace Microsoft::MixedReality::WebRTC;

struct mrsEnumerator {
  virtual ~mrsEnumerator() = default;
  virtual void dispose() = 0;
};

namespace {

inline bool IsStringNullOrEmpty(const char* str) noexcept {
  return ((str == nullptr) || (str[0] == '\0'));
}

mrsResult RTCToAPIError(const webrtc::RTCError& error) {
  if (error.ok()) {
    return MRS_SUCCESS;
  }
  switch (error.type()) {
    case webrtc::RTCErrorType::INVALID_PARAMETER:
    case webrtc::RTCErrorType::INVALID_RANGE:
      return MRS_E_INVALID_PARAMETER;
    case webrtc::RTCErrorType::INVALID_STATE:
      return MRS_E_INVALID_OPERATION;
    case webrtc::RTCErrorType::INTERNAL_ERROR:
    default:
      return MRS_E_UNKNOWN;
  }
}

#if defined(WINUWP)
using WebRtcFactoryPtr =
    std::shared_ptr<wrapper::impl::org::webRtc::WebRtcFactory>;
#endif  // defined(WINUWP)

/// Predefined name of the local audio track.
const std::string kLocalAudioLabel("local_audio");

class SimpleMediaConstraints : public webrtc::MediaConstraintsInterface {
 public:
  using webrtc::MediaConstraintsInterface::Constraint;
  using webrtc::MediaConstraintsInterface::Constraints;
  static Constraint MinWidth(uint32_t min_width) {
    return Constraint(webrtc::MediaConstraintsInterface::kMinWidth,
                      std::to_string(min_width));
  }
  static Constraint MaxWidth(uint32_t max_width) {
    return Constraint(webrtc::MediaConstraintsInterface::kMaxWidth,
                      std::to_string(max_width));
  }
  static Constraint MinHeight(uint32_t min_height) {
    return Constraint(webrtc::MediaConstraintsInterface::kMinHeight,
                      std::to_string(min_height));
  }
  static Constraint MaxHeight(uint32_t max_height) {
    return Constraint(webrtc::MediaConstraintsInterface::kMaxHeight,
                      std::to_string(max_height));
  }
  static Constraint MinFrameRate(double min_framerate) {
    // Note: kMinFrameRate is read back as an int
    const int min_int = (int)std::floor(min_framerate);
    return Constraint(webrtc::MediaConstraintsInterface::kMinFrameRate,
                      std::to_string(min_int));
  }
  static Constraint MaxFrameRate(double max_framerate) {
    // Note: kMinFrameRate is read back as an int
    const int max_int = (int)std::ceil(max_framerate);
    return Constraint(webrtc::MediaConstraintsInterface::kMaxFrameRate,
                      std::to_string(max_int));
  }
  const Constraints& GetMandatory() const override { return mandatory_; }
  const Constraints& GetOptional() const override { return optional_; }
  Constraints mandatory_;
  Constraints optional_;
};

/// Helper to open a video capture device.
mrsResult OpenVideoCaptureDevice(
    const VideoDeviceConfiguration& config,
    std::unique_ptr<cricket::VideoCapturer>& capturer_out) noexcept {
  capturer_out.reset();
#if defined(WINUWP)
  WebRtcFactoryPtr uwp_factory;
  {
    mrsResult res =
        GlobalFactory::Instance()->GetOrCreateWebRtcFactory(uwp_factory);
    if (res != MRS_SUCCESS) {
      RTC_LOG(LS_ERROR) << "Failed to initialize the UWP factory.";
      return res;
    }
  }

  // Check for calls from main UI thread; this is not supported (will deadlock)
  auto mw = winrt::Windows::ApplicationModel::Core::CoreApplication::MainView();
  auto cw = mw.CoreWindow();
  auto dispatcher = cw.Dispatcher();
  if (dispatcher.HasThreadAccess()) {
    return MRS_E_WRONG_THREAD;
  }

  // Get devices synchronously (wait for UI thread to retrieve them for us)
  rtc::Event blockOnDevicesEvent(true, false);
  auto vci = wrapper::impl::org::webRtc::VideoCapturer::getDevices();
  vci->thenClosure([&blockOnDevicesEvent] { blockOnDevicesEvent.Set(); });
  blockOnDevicesEvent.Wait(rtc::Event::kForever);
  auto deviceList = vci->value();

  std::wstring video_device_id_str;
  if (!IsStringNullOrEmpty(config.video_device_id)) {
    video_device_id_str =
        rtc::ToUtf16(config.video_device_id, strlen(config.video_device_id));
  }

  for (auto&& vdi : *deviceList) {
    auto devInfo =
        wrapper::impl::org::webRtc::VideoDeviceInfo::toNative_winrt(vdi);
    const winrt::hstring& id = devInfo.Id();
    if (!video_device_id_str.empty() && (video_device_id_str != id)) {
      continue;
    }

    auto createParams = std::make_shared<
        wrapper::impl::org::webRtc::VideoCapturerCreationParameters>();
    createParams->factory = uwp_factory;
    createParams->name = devInfo.Name().c_str();
    createParams->id = id.c_str();
    if (config.video_profile_id) {
      createParams->videoProfileId = config.video_profile_id;
    }
    createParams->videoProfileKind =
        (wrapper::org::webRtc::VideoProfileKind)config.video_profile_kind;
    createParams->enableMrc = (config.enable_mrc != mrsBool::kFalse);
    createParams->enableMrcRecordingIndicator =
        (config.enable_mrc_recording_indicator != mrsBool::kFalse);
    createParams->width = config.width;
    createParams->height = config.height;
    createParams->framerate = config.framerate;

    auto vcd = wrapper::impl::org::webRtc::VideoCapturer::create(createParams);

    if (vcd != nullptr) {
      auto nativeVcd = wrapper::impl::org::webRtc::VideoCapturer::toNative(vcd);

      RTC_LOG(LS_INFO) << "Using video capture device '"
                       << createParams->name.c_str()
                       << "' (id=" << createParams->id.c_str() << ")";

      if (auto supportedFormats = nativeVcd->GetSupportedFormats()) {
        RTC_LOG(LS_INFO) << "Supported video formats:";
        for (auto&& format : *supportedFormats) {
          auto str = format.ToString();
          RTC_LOG(LS_INFO) << "- " << str.c_str();
        }
      }

      capturer_out = std::move(nativeVcd);
      return MRS_SUCCESS;
    }
  }
#else
  // List all available video capture devices, or match by ID if specified.
  std::vector<std::string> device_names;
  {
    std::unique_ptr<webrtc::VideoCaptureModule::DeviceInfo> info(
        webrtc::VideoCaptureFactory::CreateDeviceInfo());
    if (!info) {
      return MRS_E_UNKNOWN;
    }

    const int num_devices = info->NumberOfDevices();
    constexpr uint32_t kSize = 256;
    if (!IsStringNullOrEmpty(config.video_device_id)) {
      // Look for the one specific device the user asked for
      std::string video_device_id_str = config.video_device_id;
      for (int i = 0; i < num_devices; ++i) {
        char name[kSize] = {};
        char id[kSize] = {};
        if (info->GetDeviceName(i, name, kSize, id, kSize) != -1) {
          if (video_device_id_str == id) {
            // Keep only the device the user selected
            device_names.push_back(name);
            break;
          }
        }
      }
      if (device_names.empty()) {
        RTC_LOG(LS_ERROR)
            << "Could not find video capture device by unique ID: "
            << config.video_device_id;
        return MRS_E_NOTFOUND;
      }
    } else {
      // List all available devices
      for (int i = 0; i < num_devices; ++i) {
        char name[kSize] = {};
        char id[kSize] = {};
        if (info->GetDeviceName(i, name, kSize, id, kSize) != -1) {
          device_names.push_back(name);
        }
      }
      if (device_names.empty()) {
        RTC_LOG(LS_ERROR) << "Could not find any video catpure device.";
        return MRS_E_INVALID_OPERATION;
      }
    }
  }

  // Open the specified capture device, or the first one available if none
  // specified.
  cricket::WebRtcVideoDeviceCapturerFactory factory;
  for (const auto& name : device_names) {
    // cricket::Device identifies devices by (friendly) name, not unique ID
    capturer_out = factory.Create(cricket::Device(name, 0));
    if (capturer_out) {
      return MRS_SUCCESS;
    }
  }

#endif

  return MRS_E_UNKNOWN;
}

webrtc::PeerConnectionInterface::IceTransportsType ICETransportTypeToNative(
    IceTransportType mrsValue) {
  using Native = webrtc::PeerConnectionInterface::IceTransportsType;
  using Impl = IceTransportType;
  static_assert((int)Native::kNone == (int)Impl::kNone);
  static_assert((int)Native::kNoHost == (int)Impl::kNoHost);
  static_assert((int)Native::kRelay == (int)Impl::kRelay);
  static_assert((int)Native::kAll == (int)Impl::kAll);
  return static_cast<Native>(mrsValue);
}

webrtc::PeerConnectionInterface::BundlePolicy BundlePolicyToNative(
    BundlePolicy mrsValue) {
  using Native = webrtc::PeerConnectionInterface::BundlePolicy;
  using Impl = BundlePolicy;
  static_assert((int)Native::kBundlePolicyBalanced == (int)Impl::kBalanced);
  static_assert((int)Native::kBundlePolicyMaxBundle == (int)Impl::kMaxBundle);
  static_assert((int)Native::kBundlePolicyMaxCompat == (int)Impl::kMaxCompat);
  return static_cast<Native>(mrsValue);
}

//< TODO - Unit test / check if RTC has already a utility like this
std::vector<std::string> SplitString(const std::string& str, char sep) {
  std::vector<std::string> ret;
  size_t offset = 0;
  for (size_t idx = str.find_first_of(sep); idx < std::string::npos;
       idx = str.find_first_of(sep, offset)) {
    if (idx > offset) {
      ret.push_back(str.substr(offset, idx - offset));
    }
    offset = idx + 1;
  }
  if (offset < str.size()) {
    ret.push_back(str.substr(offset));
  }
  return ret;
}

/// Convert a WebRTC VideoType format into its FOURCC counterpart.
uint32_t FourCCFromVideoType(webrtc::VideoType videoType) {
  switch (videoType) {
    default:
    case webrtc::VideoType::kUnknown:
      return (uint32_t)libyuv::FOURCC_ANY;
    case webrtc::VideoType::kI420:
      return (uint32_t)libyuv::FOURCC_I420;
    case webrtc::VideoType::kIYUV:
      return (uint32_t)libyuv::FOURCC_IYUV;
    case webrtc::VideoType::kRGB24:
      // this seems unintuitive, but is how defined in the core implementation
      return (uint32_t)libyuv::FOURCC_24BG;
    case webrtc::VideoType::kABGR:
      return (uint32_t)libyuv::FOURCC_ABGR;
    case webrtc::VideoType::kARGB:
      return (uint32_t)libyuv::FOURCC_ARGB;
    case webrtc::VideoType::kARGB4444:
      return (uint32_t)libyuv::FOURCC_R444;
    case webrtc::VideoType::kRGB565:
      return (uint32_t)libyuv::FOURCC_RGBP;
    case webrtc::VideoType::kARGB1555:
      return (uint32_t)libyuv::FOURCC_RGBO;
    case webrtc::VideoType::kYUY2:
      return (uint32_t)libyuv::FOURCC_YUY2;
    case webrtc::VideoType::kYV12:
      return (uint32_t)libyuv::FOURCC_YV12;
    case webrtc::VideoType::kUYVY:
      return (uint32_t)libyuv::FOURCC_UYVY;
    case webrtc::VideoType::kMJPEG:
      return (uint32_t)libyuv::FOURCC_MJPG;
    case webrtc::VideoType::kNV21:
      return (uint32_t)libyuv::FOURCC_NV21;
    case webrtc::VideoType::kNV12:
      return (uint32_t)libyuv::FOURCC_NV12;
    case webrtc::VideoType::kBGRA:
      return (uint32_t)libyuv::FOURCC_BGRA;
  };
}

}  // namespace

inline rtc::Thread* GetWorkerThread() {
  return GlobalFactory::Instance()->GetWorkerThread();
}

void MRS_CALL mrsCloseEnum(mrsEnumHandle* handleRef) noexcept {
  if (handleRef) {
    if (auto& handle = *handleRef) {
      handle->dispose();
      delete handle;
      handle = nullptr;
    }
  }
}

void MRS_CALL mrsEnumVideoCaptureDevicesAsync(
    mrsVideoCaptureDeviceEnumCallback enumCallback,
    void* enumCallbackUserData,
    mrsVideoCaptureDeviceEnumCompletedCallback completedCallback,
    void* completedCallbackUserData) noexcept {
  if (!enumCallback) {
    return;
  }
#if defined(WINUWP)
  // The UWP factory needs to be initialized for getDevices() to work.
  if (!GlobalFactory::Instance()->GetOrCreate()) {
    RTC_LOG(LS_ERROR) << "Failed to initialize the UWP factory.";
    return;
  }

  auto vci = wrapper::impl::org::webRtc::VideoCapturer::getDevices();
  vci->thenClosure([vci, enumCallback, completedCallback, enumCallbackUserData,
                    completedCallbackUserData] {
    auto deviceList = vci->value();
    for (auto&& vdi : *deviceList) {
      auto devInfo =
          wrapper::impl::org::webRtc::VideoDeviceInfo::toNative_winrt(vdi);
      auto id = winrt::to_string(devInfo.Id());
      id.push_back('\0');  // API must ensure null-terminated
      auto name = winrt::to_string(devInfo.Name());
      name.push_back('\0');  // API must ensure null-terminated
      (*enumCallback)(id.c_str(), name.c_str(), enumCallbackUserData);
    }
    if (completedCallback) {
      (*completedCallback)(completedCallbackUserData);
    }
  });
#else
  std::unique_ptr<webrtc::VideoCaptureModule::DeviceInfo> info(
      webrtc::VideoCaptureFactory::CreateDeviceInfo());
  if (!info) {
    if (completedCallback) {
      (*completedCallback)(completedCallbackUserData);
    }
  }
  int num_devices = info->NumberOfDevices();
  for (int i = 0; i < num_devices; ++i) {
    constexpr uint32_t kSize = 256;
    char name[kSize] = {0};
    char id[kSize] = {0};
    if (info->GetDeviceName(i, name, kSize, id, kSize) != -1) {
      (*enumCallback)(id, name, enumCallbackUserData);
    }
  }
  if (completedCallback) {
    (*completedCallback)(completedCallbackUserData);
  }
#endif
}

mrsResult MRS_CALL mrsEnumVideoCaptureFormatsAsync(
    const char* device_id,
    mrsVideoCaptureFormatEnumCallback enumCallback,
    void* enumCallbackUserData,
    mrsVideoCaptureFormatEnumCompletedCallback completedCallback,
    void* completedCallbackUserData) noexcept {
  if (IsStringNullOrEmpty(device_id)) {
    return MRS_E_INVALID_PARAMETER;
  }
  const std::string device_id_str = device_id;

  if (!enumCallback) {
    return MRS_E_INVALID_PARAMETER;
  }

#if defined(WINUWP)
  // The UWP factory needs to be initialized for getDevices() to work.
  WebRtcFactoryPtr uwp_factory;
  {
    mrsResult res =
        GlobalFactory::Instance()->GetOrCreateWebRtcFactory(uwp_factory);
    if (res != MRS_SUCCESS) {
      RTC_LOG(LS_ERROR) << "Failed to initialize the UWP factory.";
      return res;
    }
  }

  // On UWP, MediaCapture is used to open the video capture device and list
  // the available capture formats. This requires the UI thread to be idle,
  // ready to process messages. Because the enumeration is async, and this
  // function can return before the enumeration completed, if called on the
  // main UI thread then defer all of it to a different thread.
  // auto mw =
  // winrt::Windows::ApplicationModel::Core::CoreApplication::MainView(); auto
  // cw = mw.CoreWindow(); auto dispatcher = cw.Dispatcher(); if
  // (dispatcher.HasThreadAccess()) {
  //  if (completedCallback) {
  //    (*completedCallback)(MRS_E_WRONG_THREAD, completedCallbackUserData);
  //  }
  //  return MRS_E_WRONG_THREAD;
  //}

  // Enumerate the video capture devices
  auto asyncResults =
      winrt::Windows::Devices::Enumeration::DeviceInformation::FindAllAsync(
          winrt::Windows::Devices::Enumeration::DeviceClass::VideoCapture);
  asyncResults.Completed([device_id_str, enumCallback, completedCallback,
                          enumCallbackUserData, completedCallbackUserData,
                          uwp_factory = std::move(uwp_factory)](
                             auto&& asyncResults,
                             winrt::Windows::Foundation::AsyncStatus status) {
    // If the OS enumeration failed, terminate our own enumeration
    if (status != winrt::Windows::Foundation::AsyncStatus::Completed) {
      if (completedCallback) {
        (*completedCallback)(MRS_E_UNKNOWN, completedCallbackUserData);
      }
      return;
    }
    winrt::Windows::Devices::Enumeration::DeviceInformationCollection
        devInfoCollection = asyncResults.GetResults();

    // Find the video capture device by unique identifier
    winrt::Windows::Devices::Enumeration::DeviceInformation devInfo(nullptr);
    for (auto curDevInfo : devInfoCollection) {
      auto id = winrt::to_string(curDevInfo.Id());
      if (id != device_id_str) {
        continue;
      }
      devInfo = curDevInfo;
      break;
    }
    if (!devInfo) {
      if (completedCallback) {
        (*completedCallback)(MRS_E_INVALID_PARAMETER,
                             completedCallbackUserData);
      }
      return;
    }

    // Device found, create an instance to enumerate. Most devices require
    // actually opening the device to enumerate its capture formats.
    auto createParams = std::make_shared<
        wrapper::impl::org::webRtc::VideoCapturerCreationParameters>();
    createParams->factory = uwp_factory;
    createParams->name = devInfo.Name().c_str();
    createParams->id = devInfo.Id().c_str();
    auto vcd = wrapper::impl::org::webRtc::VideoCapturer::create(createParams);
    if (vcd == nullptr) {
      if (completedCallback) {
        (*completedCallback)(MRS_E_UNKNOWN, completedCallbackUserData);
      }
      return;
    }

    // Get its supported capture formats
    auto captureFormatList = vcd->getSupportedFormats();
    for (auto&& captureFormat : *captureFormatList) {
      uint32_t width = captureFormat->get_width();
      uint32_t height = captureFormat->get_height();
      double framerate = captureFormat->get_framerateFloat();
      uint32_t fourcc = captureFormat->get_fourcc();

      // When VideoEncodingProperties.Subtype() contains a GUID, the
      // conversion to FOURCC fails and returns FOURCC_ANY. So ignore
      // those formats, as we don't know their encoding.
      if (fourcc != libyuv::FOURCC_ANY) {
        (*enumCallback)(width, height, framerate, fourcc, enumCallbackUserData);
      }
    }

    // Invoke the completed callback at the end of enumeration
    if (completedCallback) {
      (*completedCallback)(MRS_SUCCESS, completedCallbackUserData);
    }
  });
#else   // defined(WINUWP)
  std::unique_ptr<webrtc::VideoCaptureModule::DeviceInfo> info(
      webrtc::VideoCaptureFactory::CreateDeviceInfo());
  if (!info) {
    return MRS_E_UNKNOWN;
  }
  int num_devices = info->NumberOfDevices();
  for (int device_idx = 0; device_idx < num_devices; ++device_idx) {
    // Filter devices by name
    constexpr uint32_t kSize = 256;
    char name[kSize] = {0};
    char id[kSize] = {0};
    if (info->GetDeviceName(device_idx, name, kSize, id, kSize) == -1) {
      continue;
    }
    if (id != device_id_str) {
      continue;
    }

    // Enum video capture formats
    int32_t num_capabilities = info->NumberOfCapabilities(id);
    for (int32_t cap_idx = 0; cap_idx < num_capabilities; ++cap_idx) {
      webrtc::VideoCaptureCapability capability{};
      if (info->GetCapability(id, cap_idx, capability) != -1) {
        uint32_t width = capability.width;
        uint32_t height = capability.height;
        double framerate = capability.maxFPS;
        uint32_t fourcc = FourCCFromVideoType(capability.videoType);
        if (fourcc != libyuv::FOURCC_ANY) {
          (*enumCallback)(width, height, framerate, fourcc,
                          enumCallbackUserData);
        }
      }
    }

    break;
  }

  // Invoke the completed callback at the end of enumeration
  if (completedCallback) {
    (*completedCallback)(MRS_SUCCESS, completedCallbackUserData);
  }
#endif  // defined(WINUWP)

  // If the async operation was successfully queued, return successfully.
  // Note that the enumeration is asynchronous, so not done yet.
  return MRS_SUCCESS;
}
mrsResult MRS_CALL
mrsPeerConnectionCreate(PeerConnectionConfiguration config,
                        mrsPeerConnectionInteropHandle interop_handle,
                        PeerConnectionHandle* peerHandleOut) noexcept {
  if (!peerHandleOut || !interop_handle) {
    return MRS_E_INVALID_PARAMETER;
  }
  *peerHandleOut = nullptr;

  // Ensure the factory exists
  rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> factory;
  {
    mrsResult res = GlobalFactory::Instance()->GetOrCreate(factory);
    if (res != MRS_SUCCESS) {
      RTC_LOG(LS_ERROR) << "Failed to initialize the peer connection factory.";
      return res;
    }
  }
  if (!factory.get()) {
    return MRS_E_UNKNOWN;
  }

  // Setup the connection configuration
  webrtc::PeerConnectionInterface::RTCConfiguration rtc_config;
  if (config.encoded_ice_servers != nullptr) {
    std::string encoded_ice_servers{config.encoded_ice_servers};
    rtc_config.servers = DecodeIceServers(encoded_ice_servers);
  }
  rtc_config.enable_rtp_data_channel = false;  // Always false for security
  rtc_config.enable_dtls_srtp = true;          // Always true for security
  rtc_config.type = ICETransportTypeToNative(config.ice_transport_type);
  rtc_config.bundle_policy = BundlePolicyToNative(config.bundle_policy);
  rtc_config.sdp_semantics = (config.sdp_semantic == SdpSemantic::kUnifiedPlan
                                  ? webrtc::SdpSemantics::kUnifiedPlan
                                  : webrtc::SdpSemantics::kPlanB);

  // Create the new peer connection
  rtc::scoped_refptr<PeerConnection> peer =
      PeerConnection::create(*factory, rtc_config, interop_handle);
  if (!peer) {
    return MRS_E_UNKNOWN;
  }
  const PeerConnectionHandle handle =
      GlobalFactory::Instance()->AddPeerConnection(std::move(peer));

  *peerHandleOut = handle;
  return MRS_SUCCESS;
}

mrsResult MRS_CALL mrsPeerConnectionRegisterInteropCallbacks(
    PeerConnectionHandle peerHandle,
    mrsPeerConnectionInteropCallbacks* callbacks) noexcept {
  if (!callbacks) {
    return MRS_E_INVALID_PARAMETER;
  }
  if (auto peer = static_cast<PeerConnection*>(peerHandle)) {
    return peer->RegisterInteropCallbacks(*callbacks);
  }
  return MRS_E_INVALID_PEER_HANDLE;
}

void MRS_CALL mrsPeerConnectionRegisterConnectedCallback(
    PeerConnectionHandle peerHandle,
    PeerConnectionConnectedCallback callback,
    void* user_data) noexcept {
  if (auto peer = static_cast<PeerConnection*>(peerHandle)) {
    peer->RegisterConnectedCallback(Callback<>{callback, user_data});
  }
}

void MRS_CALL mrsPeerConnectionRegisterLocalSdpReadytoSendCallback(
    PeerConnectionHandle peerHandle,
    PeerConnectionLocalSdpReadytoSendCallback callback,
    void* user_data) noexcept {
  if (auto peer = static_cast<PeerConnection*>(peerHandle)) {
    peer->RegisterLocalSdpReadytoSendCallback(
        Callback<const char*, const char*>{callback, user_data});
  }
}

void MRS_CALL mrsPeerConnectionRegisterIceCandidateReadytoSendCallback(
    PeerConnectionHandle peerHandle,
    PeerConnectionIceCandidateReadytoSendCallback callback,
    void* user_data) noexcept {
  if (auto peer = static_cast<PeerConnection*>(peerHandle)) {
    peer->RegisterIceCandidateReadytoSendCallback(
        Callback<const char*, int, const char*>{callback, user_data});
  }
}

void MRS_CALL mrsPeerConnectionRegisterIceStateChangedCallback(
    PeerConnectionHandle peerHandle,
    PeerConnectionIceStateChangedCallback callback,
    void* user_data) noexcept {
  if (auto peer = static_cast<PeerConnection*>(peerHandle)) {
    peer->RegisterIceStateChangedCallback(
        Callback<IceConnectionState>{callback, user_data});
  }
}

void MRS_CALL mrsPeerConnectionRegisterRenegotiationNeededCallback(
    PeerConnectionHandle peerHandle,
    PeerConnectionRenegotiationNeededCallback callback,
    void* user_data) noexcept {
  if (auto peer = static_cast<PeerConnection*>(peerHandle)) {
    peer->RegisterRenegotiationNeededCallback(Callback<>{callback, user_data});
  }
}

void MRS_CALL mrsPeerConnectionRegisterTrackAddedCallback(
    PeerConnectionHandle peerHandle,
    PeerConnectionTrackAddedCallback callback,
    void* user_data) noexcept {
  if (auto peer = static_cast<PeerConnection*>(peerHandle)) {
    peer->RegisterTrackAddedCallback(Callback<TrackKind>{callback, user_data});
  }
}

void MRS_CALL mrsPeerConnectionRegisterTrackRemovedCallback(
    PeerConnectionHandle peerHandle,
    PeerConnectionTrackRemovedCallback callback,
    void* user_data) noexcept {
  if (auto peer = static_cast<PeerConnection*>(peerHandle)) {
    peer->RegisterTrackRemovedCallback(
        Callback<TrackKind>{callback, user_data});
  }
}

void MRS_CALL mrsPeerConnectionRegisterDataChannelAddedCallback(
    PeerConnectionHandle peerHandle,
    PeerConnectionDataChannelAddedCallback callback,
    void* user_data) noexcept {
  if (auto peer = static_cast<PeerConnection*>(peerHandle)) {
    peer->RegisterDataChannelAddedCallback(
        Callback<mrsDataChannelInteropHandle, DataChannelHandle>{callback,
                                                                 user_data});
  }
}

void MRS_CALL mrsPeerConnectionRegisterDataChannelRemovedCallback(
    PeerConnectionHandle peerHandle,
    PeerConnectionDataChannelRemovedCallback callback,
    void* user_data) noexcept {
  if (auto peer = static_cast<PeerConnection*>(peerHandle)) {
    peer->RegisterDataChannelRemovedCallback(
        Callback<mrsDataChannelInteropHandle, DataChannelHandle>{callback,
                                                                 user_data});
  }
}

void MRS_CALL mrsPeerConnectionRegisterI420ARemoteVideoFrameCallback(
    PeerConnectionHandle peerHandle,
    PeerConnectionI420AVideoFrameCallback callback,
    void* user_data) noexcept {
  if (auto peer = static_cast<PeerConnection*>(peerHandle)) {
    peer->RegisterRemoteVideoFrameCallback(
        I420AFrameReadyCallback{callback, user_data});
  }
}

void MRS_CALL mrsPeerConnectionRegisterARGBRemoteVideoFrameCallback(
    PeerConnectionHandle peerHandle,
    PeerConnectionARGBVideoFrameCallback callback,
    void* user_data) noexcept {
  if (auto peer = static_cast<PeerConnection*>(peerHandle)) {
    peer->RegisterRemoteVideoFrameCallback(
        ARGBFrameReadyCallback{callback, user_data});
  }
}

MRS_API void MRS_CALL mrsPeerConnectionRegisterLocalAudioFrameCallback(
    PeerConnectionHandle peerHandle,
    PeerConnectionAudioFrameCallback callback,
    void* user_data) noexcept {
  if (auto peer = static_cast<PeerConnection*>(peerHandle)) {
    peer->RegisterLocalAudioFrameCallback(
        AudioFrameReadyCallback{callback, user_data});
  }
}

MRS_API void MRS_CALL mrsPeerConnectionRegisterRemoteAudioFrameCallback(
    PeerConnectionHandle peerHandle,
    PeerConnectionAudioFrameCallback callback,
    void* user_data) noexcept {
  if (auto peer = static_cast<PeerConnection*>(peerHandle)) {
    peer->RegisterRemoteAudioFrameCallback(
        AudioFrameReadyCallback{callback, user_data});
  }
}

mrsResult MRS_CALL mrsPeerConnectionAddLocalVideoTrack(
    PeerConnectionHandle peerHandle,
    const char* track_name,
    VideoDeviceConfiguration config,
    LocalVideoTrackHandle* trackHandle) noexcept {
  if (IsStringNullOrEmpty(track_name)) {
    return MRS_E_INVALID_PARAMETER;
  }
  if (!trackHandle) {
    return MRS_E_INVALID_PARAMETER;
  }
  *trackHandle = nullptr;

  auto peer = static_cast<PeerConnection*>(peerHandle);
  if (!peer) {
    return MRS_E_INVALID_PEER_HANDLE;
  }
  auto pc_factory = GlobalFactory::Instance()->GetExisting();
  if (!pc_factory) {
    return MRS_E_INVALID_OPERATION;
  }

  // Open the video capture device
  std::unique_ptr<cricket::VideoCapturer> video_capturer;
  auto res = OpenVideoCaptureDevice(config, video_capturer);
  if (res != MRS_SUCCESS) {
    return res;
  }
  RTC_CHECK(video_capturer.get());

  // Apply the same constraints used for opening the video capturer
  auto videoConstraints = std::make_unique<SimpleMediaConstraints>();
  if (config.width > 0) {
    videoConstraints->mandatory_.push_back(
        SimpleMediaConstraints::MinWidth(config.width));
    videoConstraints->mandatory_.push_back(
        SimpleMediaConstraints::MaxWidth(config.width));
  }
  if (config.height > 0) {
    videoConstraints->mandatory_.push_back(
        SimpleMediaConstraints::MinHeight(config.height));
    videoConstraints->mandatory_.push_back(
        SimpleMediaConstraints::MaxHeight(config.height));
  }
  if (config.framerate > 0) {
    videoConstraints->mandatory_.push_back(
        SimpleMediaConstraints::MinFrameRate(config.framerate));
    videoConstraints->mandatory_.push_back(
        SimpleMediaConstraints::MaxFrameRate(config.framerate));
  }

  rtc::scoped_refptr<webrtc::VideoTrackSourceInterface> video_source =
      pc_factory->CreateVideoSource(std::move(video_capturer),
                                    videoConstraints.get());
  if (!video_source) {
    return MRS_E_UNKNOWN;
  }
  rtc::scoped_refptr<webrtc::VideoTrackInterface> video_track =
      pc_factory->CreateVideoTrack(track_name, video_source);
  if (!video_track) {
    return MRS_E_UNKNOWN;
  }
  auto result = peer->AddLocalVideoTrack(std::move(video_track));
  if (result.ok()) {
    rtc::scoped_refptr<LocalVideoTrack>& video_track_wrapper = result.value();
    video_track_wrapper->AddRef();  // for the handle
    *trackHandle = video_track_wrapper.get();
    return MRS_SUCCESS;
  }
  return MRS_E_UNKNOWN;
}

mrsResult MRS_CALL
mrsPeerConnectionAddLocalAudioTrack(PeerConnectionHandle peerHandle) noexcept {
  if (auto peer = static_cast<PeerConnection*>(peerHandle)) {
    auto pc_factory = GlobalFactory::Instance()->GetExisting();
    if (!pc_factory) {
      return MRS_E_INVALID_OPERATION;
    }
    rtc::scoped_refptr<webrtc::AudioSourceInterface> audio_source =
        pc_factory->CreateAudioSource(cricket::AudioOptions());
    if (!audio_source) {
      return MRS_E_UNKNOWN;
    }
    rtc::scoped_refptr<webrtc::AudioTrackInterface> audio_track =
        pc_factory->CreateAudioTrack(kLocalAudioLabel, audio_source);
    if (!audio_track) {
      return MRS_E_UNKNOWN;
    }
    return (peer->AddLocalAudioTrack(std::move(audio_track)) ? MRS_SUCCESS
                                                             : MRS_E_UNKNOWN);
  }
  return MRS_E_UNKNOWN;
}

mrsResult MRS_CALL mrsPeerConnectionAddDataChannel(
    PeerConnectionHandle peerHandle,
    mrsDataChannelInteropHandle dataChannelInteropHandle,
    mrsDataChannelConfig config,
    mrsDataChannelCallbacks callbacks,
    DataChannelHandle* dataChannelHandleOut) noexcept

{
  if (!dataChannelHandleOut || !dataChannelInteropHandle) {
    return MRS_E_INVALID_PARAMETER;
  }
  *dataChannelHandleOut = nullptr;

  auto peer = static_cast<PeerConnection*>(peerHandle);
  if (!peer) {
    return MRS_E_INVALID_PEER_HANDLE;
  }

  const bool ordered = (config.flags & mrsDataChannelConfigFlags::kOrdered);
  const bool reliable = (config.flags & mrsDataChannelConfigFlags::kReliable);
  const std::string_view label = (config.label ? config.label : "");
  webrtc::RTCErrorOr<std::shared_ptr<DataChannel>> data_channel =
      peer->AddDataChannel(config.id, label, ordered, reliable,
                           dataChannelInteropHandle);
  if (data_channel.ok()) {
    data_channel.value()->SetMessageCallback(DataChannel::MessageCallback{
        callbacks.message_callback, callbacks.message_user_data});
    data_channel.value()->SetBufferingCallback(DataChannel::BufferingCallback{
        callbacks.buffering_callback, callbacks.buffering_user_data});
    data_channel.value()->SetStateCallback(DataChannel::StateCallback{
        callbacks.state_callback, callbacks.state_user_data});
    *dataChannelHandleOut = data_channel.value().operator->();
    return MRS_SUCCESS;
  }
  return RTCToAPIError(data_channel.error());
}

mrsResult MRS_CALL mrsPeerConnectionRemoveLocalVideoTrack(
    PeerConnectionHandle peer_handle,
    LocalVideoTrackHandle track_handle) noexcept {
  auto peer = static_cast<PeerConnection*>(peer_handle);
  if (!peer) {
    return MRS_E_INVALID_PEER_HANDLE;
  }
  auto track = static_cast<LocalVideoTrack*>(track_handle);
  if (!track) {
    return MRS_E_INVALID_PEER_HANDLE;
  }
  const mrsResult res =
      (peer->RemoveLocalVideoTrack(*track).ok() ? MRS_SUCCESS : MRS_E_UNKNOWN);
  return res;
}

void MRS_CALL mrsPeerConnectionRemoveLocalAudioTrack(
    PeerConnectionHandle peerHandle) noexcept {
  if (auto peer = static_cast<PeerConnection*>(peerHandle)) {
    peer->RemoveLocalAudioTrack();
  }
}

mrsResult MRS_CALL mrsPeerConnectionRemoveDataChannel(
    PeerConnectionHandle peerHandle,
    DataChannelHandle dataChannelHandle) noexcept {
  auto peer = static_cast<PeerConnection*>(peerHandle);
  if (!peer) {
    return MRS_E_INVALID_PEER_HANDLE;
  }
  auto data_channel = static_cast<DataChannel*>(dataChannelHandle);
  if (!data_channel) {
    return MRS_E_INVALID_PEER_HANDLE;
  }
  peer->RemoveDataChannel(*data_channel);
  return MRS_SUCCESS;
}

mrsResult MRS_CALL
mrsPeerConnectionSetLocalAudioTrackEnabled(PeerConnectionHandle peerHandle,
                                           mrsBool enabled) noexcept {
  auto peer = static_cast<PeerConnection*>(peerHandle);
  if (!peer) {
    return MRS_E_INVALID_PEER_HANDLE;
  }
  peer->SetLocalAudioTrackEnabled(enabled != mrsBool::kFalse);
  return MRS_SUCCESS;
}

mrsBool MRS_CALL mrsPeerConnectionIsLocalAudioTrackEnabled(
    PeerConnectionHandle peerHandle) noexcept {
  auto peer = static_cast<PeerConnection*>(peerHandle);
  if (!peer) {
    return mrsBool::kFalse;
  }
  return (peer->IsLocalAudioTrackEnabled() ? mrsBool::kTrue : mrsBool::kFalse);
}

mrsResult MRS_CALL
mrsDataChannelSendMessage(DataChannelHandle dataChannelHandle,
                          const void* data,
                          uint64_t size) noexcept {
  auto data_channel = static_cast<DataChannel*>(dataChannelHandle);
  if (!data_channel) {
    return MRS_E_INVALID_PEER_HANDLE;
  }
  return (data_channel->Send(data, (size_t)size) ? MRS_SUCCESS : MRS_E_UNKNOWN);
}

mrsResult MRS_CALL
mrsPeerConnectionAddIceCandidate(PeerConnectionHandle peerHandle,
                                 const char* sdp,
                                 const int sdp_mline_index,
                                 const char* sdp_mid) noexcept {
  if (auto peer = static_cast<PeerConnection*>(peerHandle)) {
    return (peer->AddIceCandidate(sdp, sdp_mline_index, sdp_mid)
                ? MRS_SUCCESS
                : MRS_E_UNKNOWN);
  }
  return MRS_E_INVALID_PEER_HANDLE;
}

mrsResult MRS_CALL
mrsPeerConnectionCreateOffer(PeerConnectionHandle peerHandle) noexcept {
  if (auto peer = static_cast<PeerConnection*>(peerHandle)) {
    return (peer->CreateOffer() ? MRS_SUCCESS : MRS_E_UNKNOWN);
  }
  return MRS_E_INVALID_PEER_HANDLE;
}

mrsResult MRS_CALL
mrsPeerConnectionCreateAnswer(PeerConnectionHandle peerHandle) noexcept {
  if (auto peer = static_cast<PeerConnection*>(peerHandle)) {
    return (peer->CreateAnswer() ? MRS_SUCCESS : MRS_E_UNKNOWN);
  }
  return MRS_E_INVALID_PEER_HANDLE;
}

MRS_API mrsResult MRS_CALL
mrsPeerConnectionSetBitrate(PeerConnectionHandle peer_handle,
                            int min_bitrate_bps,
                            int start_bitrate_bps,
                            int max_bitrate_bps) noexcept {
  if (auto peer = static_cast<PeerConnection*>(peer_handle)) {
    webrtc::BitrateSettings settings;
    if (min_bitrate_bps >= 0) {
      settings.min_bitrate_bps = min_bitrate_bps;
    }
    if (start_bitrate_bps >= 0) {
      settings.start_bitrate_bps = start_bitrate_bps;
    }
    if (max_bitrate_bps >= 0) {
      settings.max_bitrate_bps = max_bitrate_bps;
    }
    return peer->GetImpl()->SetBitrate(settings).ok() ? MRS_SUCCESS
                                                      : MRS_E_UNKNOWN;
  }
  return MRS_E_INVALID_PEER_HANDLE;
}

mrsResult MRS_CALL
mrsPeerConnectionSetRemoteDescription(PeerConnectionHandle peerHandle,
                                      const char* type,
                                      const char* sdp) noexcept {
  if (auto peer = static_cast<PeerConnection*>(peerHandle)) {
    return (peer->SetRemoteDescription(type, sdp) ? MRS_SUCCESS
                                                  : MRS_E_UNKNOWN);
  }
  return MRS_E_INVALID_PEER_HANDLE;
}

void MRS_CALL mrsPeerConnectionClose(PeerConnectionHandle peerHandle) noexcept {
  GlobalFactory::Instance()->RemovePeerConnection(peerHandle);
}

mrsResult MRS_CALL mrsSdpForceCodecs(const char* message,
                                     SdpFilter audio_filter,
                                     SdpFilter video_filter,
                                     char* buffer,
                                     uint64_t* buffer_size) noexcept {
  RTC_CHECK(message);
  RTC_CHECK(buffer);
  RTC_CHECK(buffer_size);
  std::string message_str(message);
  std::string audio_codec_name_str;
  std::string video_codec_name_str;
  std::map<std::string, std::string> extra_audio_params;
  std::map<std::string, std::string> extra_video_params;
  if (audio_filter.codec_name) {
    audio_codec_name_str.assign(audio_filter.codec_name);
  }
  if (video_filter.codec_name) {
    video_codec_name_str.assign(video_filter.codec_name);
  }
  // Only assign extra parameters if codec name is not empty
  if (!audio_codec_name_str.empty() && audio_filter.params) {
    SdpParseCodecParameters(audio_filter.params, extra_audio_params);
  }
  if (!video_codec_name_str.empty() && video_filter.params) {
    SdpParseCodecParameters(video_filter.params, extra_video_params);
  }
  std::string out_message =
      SdpForceCodecs(message_str, audio_codec_name_str, extra_audio_params,
                     video_codec_name_str, extra_video_params);
  const size_t capacity = static_cast<size_t>(*buffer_size);
  const size_t size = out_message.size();
  *buffer_size = size + 1;
  if (capacity < size + 1) {
    return MRS_E_INVALID_PARAMETER;
  }
  memcpy(buffer, out_message.c_str(), size);
  buffer[size] = '\0';
  return MRS_SUCCESS;
}

void MRS_CALL mrsMemCpy(void* dst, const void* src, uint64_t size) noexcept {
  memcpy(dst, src, static_cast<size_t>(size));
}

void MRS_CALL mrsMemCpyStride(void* dst,
                              int32_t dst_stride,
                              const void* src,
                              int32_t src_stride,
                              int32_t elem_size,
                              int32_t elem_count) noexcept {
  RTC_CHECK(dst);
  RTC_CHECK(dst_stride >= elem_size);
  RTC_CHECK(src);
  RTC_CHECK(src_stride >= elem_size);
  if ((dst_stride == elem_size) && (src_stride == elem_size)) {
    // If tightly packed, do a single memcpy() for performance
    const size_t total_size = (size_t)elem_size * elem_count;
    memcpy(dst, src, total_size);
  } else {
    // Otherwise, copy row by row
    for (int i = 0; i < elem_count; ++i) {
      memcpy(dst, src, elem_size);
      dst = (char*)dst + dst_stride;
      src = (const char*)src + src_stride;
    }
  }
}
