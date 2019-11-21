// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See LICENSE in the project root for license
// information.

#include "pch.h"

#include "interop/external_video_track_source_interop.h"
#include "interop/interop_api.h"
#include "interop/local_video_track_interop.h"

#if !defined(MRSW_EXCLUDE_DEVICE_TESTS)

namespace {

// PeerConnectionI420VideoFrameCallback
using I420VideoFrameCallback = Callback<const void*,
                                        const void*,
                                        const void*,
                                        const void*,
                                        const int,
                                        const int,
                                        const int,
                                        const int,
                                        const int,
                                        const int>;

/// Generate a test frame to simualte an external video track source.
void MRS_CALL MakeTestFrame(void* /*user_data*/,
                            ExternalVideoTrackSourceHandle handle,
                            uint32_t request_id,
                            int64_t /*timestamp_ms*/) {
  // Generate a frame
  uint8_t buffer_y[256];
  uint8_t buffer_u[64];
  uint8_t buffer_v[64];
  memset(buffer_y, 0x7F, 256);
  memset(buffer_u, 0x7F, 64);
  memset(buffer_v, 0x7F, 64);

  // Complete the frame request with the generated frame
  mrsI420VideoFrameView frame;
  frame.width = 16;
  frame.height = 16;
  frame.data_y = buffer_y;
  frame.data_u = buffer_u;
  frame.data_v = buffer_v;
  frame.stride_y = 16;
  frame.stride_u = 8;
  frame.stride_v = 8;
  mrsExternalVideoTrackSourceCompleteI420VideoFrameRequest(handle, request_id,
                                                           &frame);
}

void CheckIsTestFrame(const void* yptr,
                      const void* uptr,
                      const void* vptr,
                      const int ystride,
                      const int ustride,
                      const int vstride,
                      const int frame_width,
                      const int frame_height) {
  ASSERT_EQ(16, frame_width);
  ASSERT_EQ(16, frame_height);
  ASSERT_NE(nullptr, yptr);
  ASSERT_NE(nullptr, uptr);
  ASSERT_NE(nullptr, vptr);
  ASSERT_EQ(16, ystride);
  ASSERT_EQ(8, ustride);
  ASSERT_EQ(8, vstride);
  {
    const uint8_t* s = (const uint8_t*)yptr;
    const uint8_t* e = s + (ystride * frame_height);
    bool all_7f = true;
    for (const uint8_t* p = s; p < e; ++p) {
      all_7f = all_7f && (*p == 0x7F);
    }
    ASSERT_TRUE(all_7f);
  }
  {
    const uint8_t* s = (const uint8_t*)uptr;
    const uint8_t* e = s + (ustride * frame_height / 2);
    bool all_7f = true;
    for (const uint8_t* p = s; p < e; ++p) {
      all_7f = all_7f && (*p == 0x7F);
    }
    ASSERT_TRUE(all_7f);
  }
  {
    const uint8_t* s = (const uint8_t*)vptr;
    const uint8_t* e = s + (vstride * frame_height / 2);
    bool all_7f = true;
    for (const uint8_t* p = s; p < e; ++p) {
      all_7f = all_7f && (*p == 0x7F);
    }
    ASSERT_TRUE(all_7f);
  }
}

}  // namespace

TEST(VideoTrack, Simple) {
  LocalPeerPairRaii pair;

  VideoDeviceConfiguration config{};
  LocalVideoTrackHandle track_handle{};
  ASSERT_EQ(MRS_SUCCESS,
            mrsPeerConnectionAddLocalVideoTrack(pair.pc1(), "local_video_track",
                                                config, &track_handle));
  ASSERT_NE(mrsBool::kFalse, mrsLocalVideoTrackIsEnabled(track_handle));

  uint32_t frame_count = 0;
  I420VideoFrameCallback i420cb =
      [&frame_count](const void* yptr, const void* uptr, const void* vptr,
                     const void* /*aptr*/, const int /*ystride*/,
                     const int /*ustride*/, const int /*vstride*/,
                     const int /*astride*/, const int frame_width,
                     const int frame_height) {
        ASSERT_NE(nullptr, yptr);
        ASSERT_NE(nullptr, uptr);
        ASSERT_NE(nullptr, vptr);
        ASSERT_LT(0, frame_width);
        ASSERT_LT(0, frame_height);
        ++frame_count;
      };
  mrsPeerConnectionRegisterI420ARemoteVideoFrameCallback(pair.pc2(),
                                                         CB(i420cb));

  pair.ConnectAndWait();

  Event ev;
  ev.WaitFor(5s);
  ASSERT_LT(50u, frame_count);  // at least 10 FPS

  mrsPeerConnectionRegisterI420ARemoteVideoFrameCallback(pair.pc2(), nullptr,
                                                         nullptr);
  mrsLocalVideoTrackRemoveRef(track_handle);
}

TEST(VideoTrack, Muted) {
  LocalPeerPairRaii pair;

  VideoDeviceConfiguration config{};
  LocalVideoTrackHandle track_handle{};
  ASSERT_EQ(MRS_SUCCESS,
            mrsPeerConnectionAddLocalVideoTrack(pair.pc1(), "local_video_track",
                                                config, &track_handle));

  // New tracks are enabled by default
  ASSERT_NE(mrsBool::kFalse, mrsLocalVideoTrackIsEnabled(track_handle));

  // Disable the video track; it should output only black frames
  ASSERT_EQ(MRS_SUCCESS,
            mrsLocalVideoTrackSetEnabled(track_handle, mrsBool::kFalse));
  ASSERT_EQ(mrsBool::kFalse, mrsLocalVideoTrackIsEnabled(track_handle));

  uint32_t frame_count = 0;
  I420VideoFrameCallback i420cb =
      [&frame_count](const void* yptr, const void* uptr, const void* vptr,
                     const void* /*aptr*/, const int ystride,
                     const int /*ustride*/, const int /*vstride*/,
                     const int /*astride*/, const int frame_width,
                     const int frame_height) {
        ASSERT_NE(nullptr, yptr);
        ASSERT_NE(nullptr, uptr);
        ASSERT_NE(nullptr, vptr);
        ASSERT_LT(0, frame_width);
        ASSERT_LT(0, frame_height);
        const uint8_t* s = (const uint8_t*)yptr;
        const uint8_t* e = s + ((size_t)ystride * frame_height);
        bool all_black = true;
        for (const uint8_t* p = s; p < e; ++p) {
          all_black = all_black && (*p == 0);
        }
        // Note: U and V can be anything, so don't test them.
        ASSERT_TRUE(all_black);
        ++frame_count;
      };
  mrsPeerConnectionRegisterI420ARemoteVideoFrameCallback(pair.pc2(),
                                                         CB(i420cb));

  pair.ConnectAndWait();

  Event ev;
  ev.WaitFor(5s);
  ASSERT_LT(50u, frame_count);  // at least 10 FPS

  mrsPeerConnectionRegisterI420ARemoteVideoFrameCallback(pair.pc2(), nullptr,
                                                         nullptr);
  mrsLocalVideoTrackRemoveRef(track_handle);
}

void MRS_CALL enumDeviceCallback(const char* id,
                                 const char* /*name*/,
                                 void* user_data) {
  auto device_ids = (std::vector<std::string>*)user_data;
  device_ids->push_back(id);
}
void MRS_CALL enumDeviceCallbackCompleted(void* user_data) {
  auto ev = (Event*)user_data;
  ev->Set();
}

// FIXME - PeerConnection currently doesn't support multiple local video tracks
// TEST(VideoTrack, DeviceIdAll) {
//  LocalPeerPairRaii pair;
//
//  Event ev;
//  std::vector<std::string> device_ids;
//  mrsEnumVideoCaptureDevicesAsync(enumDeviceCallback, &device_ids,
//                                  enumDeviceCallbackCompleted, &ev);
//  ev.Wait();
//
//  for (auto&& id : device_ids) {
//    VideoDeviceConfiguration config{};
//    config.video_device_id = id.c_str();
//    ASSERT_EQ(MRS_SUCCESS,
//              mrsPeerConnectionAddLocalVideoTrack(pair.pc1(), config));
//  }
//}

TEST(VideoTrack, DeviceIdInvalid) {
  LocalPeerPairRaii pair;

  VideoDeviceConfiguration config{};
  LocalVideoTrackHandle track_handle{};
  config.video_device_id = "[[INVALID DEVICE ID]]";
  ASSERT_EQ(MRS_E_NOTFOUND,
            mrsPeerConnectionAddLocalVideoTrack(pair.pc1(), "invalid_track",
                                                config, &track_handle));
  ASSERT_EQ(nullptr, track_handle);
}

TEST(VideoTrack, ExternalI420) {
  LocalPeerPairRaii pair;

  ExternalVideoTrackSourceHandle source_handle = nullptr;
  LocalVideoTrackHandle track_handle = nullptr;
  ASSERT_EQ(MRS_SUCCESS,
            mrsPeerConnectionAddLocalVideoTrackFromExternalI420Source(
                pair.pc1(), "simulated_video_track", &MakeTestFrame, nullptr,
                &source_handle, &track_handle));
  ASSERT_NE(mrsBool::kFalse, mrsLocalVideoTrackIsEnabled(track_handle));

  uint32_t frame_count = 0;
  I420VideoFrameCallback i420cb =
      [&frame_count](const void* yptr, const void* uptr, const void* vptr,
                     const void* /*aptr*/, const int ystride, const int ustride,
                     const int vstride, const int /*astride*/,
                     const int frame_width, const int frame_height) {
        CheckIsTestFrame(yptr, uptr, vptr, ystride, ustride, vstride,
                         frame_width, frame_height);
        ++frame_count;
      };
  mrsPeerConnectionRegisterI420ARemoteVideoFrameCallback(pair.pc2(),
                                                         CB(i420cb));

  pair.ConnectAndWait();

  Event ev;
  ev.WaitFor(5s);
  ASSERT_LT(50u, frame_count);  // at least 10 FPS

  mrsPeerConnectionRegisterI420ARemoteVideoFrameCallback(pair.pc2(), nullptr,
                                                         nullptr);
  mrsPeerConnectionRemoveLocalVideoTrack(pair.pc1(), track_handle);
  mrsLocalVideoTrackRemoveRef(track_handle);
  mrsExternalVideoTrackSourceShutdown(source_handle);
  mrsExternalVideoTrackSourceRemoveRef(source_handle);
}

#endif  // MRSW_EXCLUDE_DEVICE_TESTS
