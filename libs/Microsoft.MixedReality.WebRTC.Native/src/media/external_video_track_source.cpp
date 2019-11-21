// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See LICENSE in the project root for license
// information.

#include "pch.h"

#include "interop/global_factory.h"
#include "media/external_video_track_source.h"

namespace {

using namespace Microsoft::MixedReality::WebRTC;

enum {
  /// Request a new video frame from the source.
  MSG_REQUEST_FRAME
};

/// Buffer adapter for an I420 video frame.
class I420BufferAdapter : public BufferAdapter {
 public:
  using RequestCallback =
      Callback<ExternalVideoTrackSourceHandle, uint32_t, int64_t>;
  I420BufferAdapter(RequestCallback callback)
      : callback_(std::move(callback)) {}
  ~I420BufferAdapter() override = default;
  void RequestFrame(ExternalVideoTrackSourceHandle source_handle,
                    uint32_t request_id,
                    int64_t time_ms) noexcept override {
    // Request a single I420 frame
    callback_(source_handle, request_id, time_ms);
  }
  rtc::scoped_refptr<webrtc::VideoFrameBuffer> FillBuffer(
      const mrsI420VideoFrameView& frame_view) override {
    return webrtc::I420Buffer::Copy(frame_view.width, frame_view.height,
                                    frame_view.data_y, frame_view.stride_y,
                                    frame_view.data_u, frame_view.stride_u,
                                    frame_view.data_v, frame_view.stride_v);
  }
  rtc::scoped_refptr<webrtc::VideoFrameBuffer> FillBuffer(
      const mrsArgb32VideoFrameView& /*frame_view*/) override {
    RTC_CHECK(false);
  }

 private:
  RequestCallback callback_;
};

/// Buffer adapter for a 32-bit ARGB video frame.
class Argb32BufferAdapter : public BufferAdapter {
 public:
  using RequestCallback =
      Callback<ExternalVideoTrackSourceHandle, uint32_t, int64_t>;
  Argb32BufferAdapter(RequestCallback callback)
      : callback_(std::move(callback)) {}
  ~Argb32BufferAdapter() override = default;
  void RequestFrame(ExternalVideoTrackSourceHandle source_handle,
                    uint32_t request_id,
                    int64_t time_ms) noexcept override {
    // Request a single ARGB32 frame
    callback_(source_handle, request_id, time_ms);
  }
  rtc::scoped_refptr<webrtc::VideoFrameBuffer> FillBuffer(
      const mrsI420VideoFrameView& /*frame_view*/) override {
    RTC_CHECK(false);
  }
  rtc::scoped_refptr<webrtc::VideoFrameBuffer> FillBuffer(
      const mrsArgb32VideoFrameView& frame_view) override {
    // Check that the input frame fits within the constraints of chroma
    // downsampling (width and height multiple of 2).
    uint32_t width = frame_view.width;
    if (width & 0x1) {
      if (!has_warned_) {
        RTC_LOG(LS_WARNING) << "ARGB32 video frame has width " << width
                            << " which is not a multiple of 2, so cannot be "
                               "chroma-downsampled. "
                               "Truncating to "
                            << (width - 1) << " before I420 conversion.";
        has_warned_ = true;
      }
      --width;
    }
    uint32_t height = frame_view.height;
    if (height & 0x1) {
      if (!has_warned_) {
        RTC_LOG(LS_WARNING) << "ARGB32 video frame has height " << height
                            << " which is not a multiple of 2, so cannot be "
                               "chroma-downsampled. "
                               "Truncating to "
                            << (height - 1) << " before I420 conversion.";
        has_warned_ = true;
      }
      --height;
    }

    // Create I420 buffer
    rtc::scoped_refptr<webrtc::I420Buffer> buffer =
        webrtc::I420Buffer::Create(width, height);

    // Convert to I420 and copy to buffer
    libyuv::ARGBToI420((const uint8_t*)frame_view.data_argb,
                       frame_view.row_stride, buffer->MutableDataY(),
                       buffer->StrideY(), buffer->MutableDataU(),
                       buffer->StrideU(), buffer->MutableDataV(),
                       buffer->StrideV(), width, height);

    return buffer;
  }

 private:
  RequestCallback callback_;
  bool has_warned_ = false;
};

}  // namespace

namespace Microsoft::MixedReality::WebRTC {

rtc::scoped_refptr<ExternalVideoTrackSource>
ExternalVideoTrackSource::createFromI420(
    mrsRequestExternalI420VideoFrameCallback callback,
    void* user_data) {
  return ExternalVideoTrackSource::create(std::make_unique<I420BufferAdapter>(
      I420BufferAdapter::RequestCallback{callback, user_data}));
}

rtc::scoped_refptr<ExternalVideoTrackSource>
ExternalVideoTrackSource::createFromArgb32(
    mrsRequestExternalArgb32VideoFrameCallback callback,
    void* user_data) {
  return ExternalVideoTrackSource::create(std::make_unique<Argb32BufferAdapter>(
      Argb32BufferAdapter::RequestCallback{callback, user_data}));
}

rtc::scoped_refptr<ExternalVideoTrackSource> ExternalVideoTrackSource::create(
    std::unique_ptr<BufferAdapter> adapter) {
  auto source =
      new rtc::RefCountedObject<ExternalVideoTrackSource>(std::move(adapter));

  // Video track sources start already capturing; there is no start/stop
  // mechanism at the track level in WebRTC. A source is either being
  // initialized, or is already live.
  source->StartCapture();

  return source;
}

ExternalVideoTrackSource::ExternalVideoTrackSource(
    std::unique_ptr<BufferAdapter> adapter)
    : adapter_(std::forward<std::unique_ptr<BufferAdapter>>(adapter)),
      capture_thread_(rtc::Thread::Create()) {
  capture_thread_->SetName("ExternalVideoTrackSource capture thread", this);
  GlobalFactory::Instance()->AddObject(this);
}

ExternalVideoTrackSource::~ExternalVideoTrackSource() {
  StopCapture();
  GlobalFactory::Instance()->RemoveObject(this);
}

void ExternalVideoTrackSource::StartCapture() {
  // Check if |Shutdown()| was called, in which case the source cannot restart.
  if (!adapter_) {
    return;
  }

  // Start capture thread
  state_ = SourceState::kLive;
  capture_thread_->Start();

  // Schedule first frame request for 10ms from now
  int64_t now = rtc::TimeMillis();
  capture_thread_->PostAt(RTC_FROM_HERE, now + 10, this, MSG_REQUEST_FRAME);
}

void ExternalVideoTrackSource::StopCapture() {
  if (state_ != SourceState::kEnded) {
    capture_thread_->Stop();
    state_ = SourceState::kEnded;
  }
}

mrsResult ExternalVideoTrackSource::CompleteRequest(
    uint32_t request_id,
    const mrsI420VideoFrameView& frame_view) {
  // Validate pending request ID and retrieve frame timestamp
  int64_t timestamp_ms;
  {
    rtc::CritScope lock(&request_lock_);
    auto it = pending_requests_.find(request_id);
    if (it == pending_requests_.end()) {
      return MRS_E_INVALID_PARAMETER;
    }
    timestamp_ms = it->second;
    pending_requests_.erase(it);
  }

  // Create and dispatch the video frame
  webrtc::VideoFrame frame{
      webrtc::VideoFrame::Builder()
          .set_video_frame_buffer(adapter_->FillBuffer(frame_view))
          .set_timestamp_ms(timestamp_ms)
          .build()};
  OnFrame(frame);
  return MRS_SUCCESS;
}

mrsResult ExternalVideoTrackSource::CompleteRequest(
    uint32_t request_id,
    const mrsArgb32VideoFrameView& frame_view) {
  // Validate pending request ID and retrieve frame timestamp
  int64_t timestamp_ms;
  {
    rtc::CritScope lock(&request_lock_);
    auto it = pending_requests_.find(request_id);
    if (it == pending_requests_.end()) {
      return MRS_E_INVALID_PARAMETER;
    }
    timestamp_ms = it->second;
    pending_requests_.erase(it);
  }

  // Create and dispatch the video frame
  webrtc::VideoFrame frame{
      webrtc::VideoFrame::Builder()
          .set_video_frame_buffer(adapter_->FillBuffer(frame_view))
          .set_timestamp_ms(timestamp_ms)
          .build()};
  OnFrame(frame);
  return MRS_SUCCESS;
}

void ExternalVideoTrackSource::Shutdown() noexcept {
  StopCapture();
  adapter_ = nullptr;
}

// void ExternalVideoTrackSource::Run(rtc::Thread*) {
//  while (capture_thread_->ProcessMessages()) {
//
//  }
//}

// Note - This is called on the capture thread only.
void ExternalVideoTrackSource::OnMessage(rtc::Message* message) {
  switch (message->message_id) {
    case MSG_REQUEST_FRAME:
      const int64_t now = rtc::TimeMillis();

      // Request a frame from the external video source
      const ExternalVideoTrackSourceHandle source_handle = this;
      uint32_t request_id;
      {
        rtc::CritScope lock(&request_lock_);
        request_id = next_request_id_++;
        pending_requests_.emplace(request_id, now);
      }
      adapter_->RequestFrame(source_handle, request_id, now);

      // Schedule a new request for 30ms from now
      //< TODO - this is unreliable and prone to drifting; figure out something
      // better
      capture_thread_->PostAt(RTC_FROM_HERE, now + 30, this, MSG_REQUEST_FRAME);
      break;
  }
}

}  // namespace Microsoft::MixedReality::WebRTC
