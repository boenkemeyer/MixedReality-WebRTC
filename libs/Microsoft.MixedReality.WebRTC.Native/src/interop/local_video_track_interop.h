// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include "export.h"
#include "interop/interop_api.h"

extern "C" {

//
// Wrapper
//

/// Add a reference to the native object associated with the given handle.
MRS_API void MRS_CALL
mrsLocalVideoTrackAddRef(LocalVideoTrackHandle handle) noexcept;

/// Remove a reference from the native object associated with the given handle.
MRS_API void MRS_CALL
mrsLocalVideoTrackRemoveRef(LocalVideoTrackHandle handle) noexcept;

/// Register a custom callback to be called when the local video track captured
/// a frame. The captured frames is passed to the registered callback in I420
/// encoding.
MRS_API void MRS_CALL mrsLocalVideoTrackRegisterI420AFrameCallback(
    LocalVideoTrackHandle trackHandle,
    PeerConnectionI420AVideoFrameCallback callback,
    void* user_data) noexcept;

/// Register a custom callback to be called when the local video track captured
/// a frame. The captured frames is passed to the registered callback in ARGB32
/// encoding.
MRS_API void MRS_CALL mrsLocalVideoTrackRegisterARGBFrameCallback(
    LocalVideoTrackHandle trackHandle,
    PeerConnectionARGBVideoFrameCallback callback,
    void* user_data) noexcept;

/// Enable or disable a local video track. Enabled tracks output their media
/// content as usual. Disabled track output some void media content (black video
/// frames, silent audio frames). Enabling/disabling a track is a lightweight
/// concept similar to "mute", which does not require an SDP renegotiation.
MRS_API mrsResult MRS_CALL
mrsLocalVideoTrackSetEnabled(LocalVideoTrackHandle track_handle,
                             mrsBool enabled) noexcept;

/// Query a local video track for its enabled status. Return |mrsBool::kTrue| if the track is enabled, or 
MRS_API mrsBool MRS_CALL
mrsLocalVideoTrackIsEnabled(LocalVideoTrackHandle track_handle) noexcept;

}  // extern "C"
