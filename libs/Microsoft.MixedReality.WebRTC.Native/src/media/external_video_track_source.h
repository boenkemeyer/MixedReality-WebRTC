// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See LICENSE in the project root for license
// information.

#pragma once

#include "media/base/adaptedvideotracksource.h"

#include "callback.h"
#include "interop/interop_api.h"

namespace Microsoft::MixedReality::WebRTC {

/// Adapater for the frame buffer of an external video track source,
/// to support various frame encodings in a unified way.
class BufferAdapter {
 public:
  virtual ~BufferAdapter() = default;

  /// Request a new video frame with the specified request ID.
  virtual void RequestFrame(ExternalVideoTrackSourceHandle source_handle,
                            uint32_t request_id,
                            int64_t time_ms) noexcept = 0;

  /// Allocate a new video frame buffer with a video frame received from a
  /// fulfilled frame request.
  virtual rtc::scoped_refptr<webrtc::VideoFrameBuffer> FillBuffer(
      const mrsI420VideoFrameView& frame_view) = 0;
  virtual rtc::scoped_refptr<webrtc::VideoFrameBuffer> FillBuffer(
      const mrsArgb32VideoFrameView& frame_view) = 0;
};

/// Video track source acting as an adapter for an external source of raw
/// frames.
class ExternalVideoTrackSource : public rtc::AdaptedVideoTrackSource,
                                 // public rtc::Runnable,
                                 public rtc::MessageHandler {
 public:
  /// Helper to create an external video track source from a custom I420 video
  /// frame request callback.
  static MRS_API rtc::scoped_refptr<ExternalVideoTrackSource> createFromI420(
      mrsRequestExternalI420VideoFrameCallback callback,
      void* user_data);

  /// Helper to create an external video track source from a custom ARGB32 video
  /// frame request callback.
  static MRS_API rtc::scoped_refptr<ExternalVideoTrackSource> createFromArgb32(
      mrsRequestExternalArgb32VideoFrameCallback callback,
      void* user_data);

  MRS_API ~ExternalVideoTrackSource() override;

  /// Start the video capture. This will begin to produce video frames and start
  /// calling the video frame callback.
  MRS_API void StartCapture();

  /// Stop the video capture. This will stop producing video frames.
  MRS_API void StopCapture();

  //< TODO - This breaks the buffer abstraction, refactor...
  MRS_API mrsResult CompleteRequest(uint32_t request_id,
                                    const mrsI420VideoFrameView& frame_view);
  MRS_API mrsResult CompleteRequest(uint32_t request_id,
                                    const mrsArgb32VideoFrameView& frame_view);

  /// Shutdown the source and release the buffer adapter and its callback.
  MRS_API void Shutdown() noexcept;

  // VideoTrackSourceInterface
  MRS_API bool is_screencast() const override { return false; }
  MRS_API absl::optional<bool> needs_denoising() const override {
    return absl::nullopt;
  }

  // MediaSourceInterface
  MRS_API SourceState state() const override { return state_; }
  MRS_API bool remote() const override { return false; }

 protected:
  static rtc::scoped_refptr<ExternalVideoTrackSource> create(
      std::unique_ptr<BufferAdapter> adapter);
  ExternalVideoTrackSource(std::unique_ptr<BufferAdapter> adapter);
  // void Run(rtc::Thread* thread) override;
  void OnMessage(rtc::Message* message) override;
  std::unique_ptr<BufferAdapter> adapter_;
  std::unique_ptr<rtc::Thread> capture_thread_;
  SourceState state_ = SourceState::kInitializing;

  /// Collection of pending frame requests
  std::map<uint32_t, int64_t> pending_requests_ RTC_GUARDED_BY(request_lock_);

  /// Next available ID for a frame request.
  uint32_t next_request_id_ RTC_GUARDED_BY(request_lock_){};

  /// Lock for frame requests.
  rtc::CriticalSection request_lock_;
};

}  // namespace Microsoft::MixedReality::WebRTC
