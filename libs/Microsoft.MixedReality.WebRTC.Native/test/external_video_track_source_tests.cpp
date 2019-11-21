// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See LICENSE in the project root for license
// information.

#include "pch.h"

#include "data_channel.h"
#include "interop/interop_api.h"
#include "interop/external_video_track_source_interop.h"
#include "interop/local_video_track_interop.h"

#include "libyuv.h"

#if !defined(MRSW_EXCLUDE_DEVICE_TESTS)

namespace {

uint32_t FrameBuffer[256];

void FillSquareArgb32(uint32_t* buffer,
                      int x,
                      int y,
                      int w,
                      int h,
                      int stride,
                      uint32_t color) {
  assert(stride % 4 == 0);
  char* row = ((char*)buffer + (y * stride));
  for (int j = 0; j < h; ++j) {
    uint32_t* ptr = (uint32_t*)row + x;
    for (int i = 0; i < w; ++i) {
      *ptr++ = color;
    }
    row += stride;
  }
}

constexpr uint32_t kRed = 0xFF2250F2u;
constexpr uint32_t kGreen = 0xFF00BA7Fu;
constexpr uint32_t kBlue = 0xFFEFA400u;
constexpr uint32_t kYellow = 0xFF00B9FFu;

/// Generate a 16px by 16px test frame.
void MRS_CALL
GenerateQuadTestFrame(void* /*user_data*/,
                      ExternalVideoTrackSourceHandle source_handle,
                      uint32_t request_id,
                      int64_t /*timestamp_ms*/) {
  memset(FrameBuffer, 0, 256 * 4);
  FillSquareArgb32(FrameBuffer, 0, 0, 8, 8, 64, kRed);
  FillSquareArgb32(FrameBuffer, 8, 0, 8, 8, 64, kGreen);
  FillSquareArgb32(FrameBuffer, 0, 8, 8, 8, 64, kBlue);
  FillSquareArgb32(FrameBuffer, 8, 8, 8, 8, 64, kYellow);
  mrsArgb32VideoFrameView frame_view{};
  frame_view.width = 16;
  frame_view.height = 16;
  frame_view.data_argb = FrameBuffer;
  frame_view.row_stride = 16 * 4;
  ASSERT_EQ(MRS_SUCCESS,
            mrsExternalVideoTrackSourceCompleteArgb32VideoFrameRequest(
                source_handle, request_id, &frame_view));
}

inline double ArgbColorError(uint32_t ref, uint32_t val) {
  return ((double)(ref & 0xFFu) - (double)(val & 0xFFu)) +
         ((double)((ref & 0xFF00u) >> 8u) - (double)((val & 0xFF00u) >> 8u)) +
         ((double)((ref & 0xFF0000u) >> 16u) -
          (double)((val & 0xFF0000u) >> 16u)) +
         ((double)((ref & 0xFF000000u) >> 24u) -
          (double)((val & 0xFF000000u) >> 24u));
}

void ValidateQuadTestFrame(const void* data,
                           const int stride,
                           const int frame_width,
                           const int frame_height) {
  ASSERT_NE(nullptr, data);
  ASSERT_EQ(16, frame_width);
  ASSERT_EQ(16, frame_height);
  double err = 0.0;
  const uint8_t* row = (const uint8_t*)data;
  for (int j = 0; j < 8; ++j) {
    const uint32_t* argb = (const uint32_t*)row;
    // Red
    for (int i = 0; i < 8; ++i) {
      err += ArgbColorError(kRed, *argb++);
    }
    // Green
    for (int i = 0; i < 8; ++i) {
      err += ArgbColorError(kGreen, *argb++);
    }
    row += stride;
  }
  for (int j = 0; j < 8; ++j) {
    const uint32_t* argb = (const uint32_t*)row;
    // Blue
    for (int i = 0; i < 8; ++i) {
      err += ArgbColorError(kBlue, *argb++);
    }
    // Yellow
    for (int i = 0; i < 8; ++i) {
      err += ArgbColorError(kYellow, *argb++);
    }
    row += stride;
  }
  ASSERT_LE(std::fabs(err), 768.0);  // +/-1 per component over 256 pixels
}

// PeerConnectionARGBVideoFrameCallback
using ARGBVideoFrameCallback =
    Callback<const void*, const int, const int, const int>;

}  // namespace

TEST(ExternalVideoTrackSource, Simple) {
  LocalPeerPairRaii pair;

  ExternalVideoTrackSourceHandle source_handle = nullptr;
  LocalVideoTrackHandle track_handle = nullptr;
  ASSERT_EQ(MRS_SUCCESS,
            mrsPeerConnectionAddLocalVideoTrackFromExternalArgb32Source(
                pair.pc1(), "gen_track", &GenerateQuadTestFrame, nullptr,
                &source_handle, &track_handle));
  ASSERT_NE(mrsBool::kFalse, mrsLocalVideoTrackIsEnabled(track_handle));
  ASSERT_NE(nullptr, source_handle);

  uint32_t frame_count = 0;
  ARGBVideoFrameCallback argb_cb =
      [&frame_count](const void* data, const int stride, const int frame_width,
                     const int frame_height) {
        ASSERT_NE(nullptr, data);
        ASSERT_LT(0, frame_width);
        ASSERT_LT(0, frame_height);
        ValidateQuadTestFrame(data, stride, frame_width, frame_height);
        ++frame_count;
      };
  mrsPeerConnectionRegisterARGBRemoteVideoFrameCallback(pair.pc2(),
                                                        CB(argb_cb));

  pair.ConnectAndWait();

  // Simple timer
  Event ev;
  ev.WaitFor(5s);
  ASSERT_LT(50u, frame_count);  // at least 10 FPS

  mrsPeerConnectionRegisterARGBRemoteVideoFrameCallback(pair.pc2(), nullptr,
                                                        nullptr);
  mrsPeerConnectionRemoveLocalVideoTracksFromSource(pair.pc1(), source_handle);
  mrsLocalVideoTrackRemoveRef(track_handle);
  mrsExternalVideoTrackSourceShutdown(source_handle);
  mrsExternalVideoTrackSourceRemoveRef(source_handle);
}

#endif  // MRSW_EXCLUDE_DEVICE_TESTS
