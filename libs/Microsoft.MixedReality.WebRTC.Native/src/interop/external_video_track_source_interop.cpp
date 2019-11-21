// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

// This is a precompiled header, it must be on its own, followed by a blank
// line, to prevent clang-format from reordering it with other headers.
#include "pch.h"

#include "interop/external_video_track_source_interop.h"
#include "media/external_video_track_source.h"

using namespace Microsoft::MixedReality::WebRTC;

void MRS_CALL mrsExternalVideoTrackSourceAddRef(
    ExternalVideoTrackSourceHandle handle) noexcept {
  if (auto track = static_cast<ExternalVideoTrackSource*>(handle)) {
    track->AddRef();
  } else {
    RTC_LOG(LS_WARNING)
        << "Trying to add reference to NULL ExternalVideoTrackSource object.";
  }
}

void MRS_CALL mrsExternalVideoTrackSourceRemoveRef(
    ExternalVideoTrackSourceHandle handle) noexcept {
  if (auto track = static_cast<ExternalVideoTrackSource*>(handle)) {
    track->Release();
  } else {
    RTC_LOG(LS_WARNING) << "Trying to remove reference from NULL "
                           "ExternalVideoTrackSource object.";
  }
}

void MRS_CALL mrsExternalVideoTrackSourceShutdown(
    ExternalVideoTrackSourceHandle handle) noexcept {
  if (auto track = static_cast<ExternalVideoTrackSource*>(handle)) {
    track->Shutdown();
  }
}
