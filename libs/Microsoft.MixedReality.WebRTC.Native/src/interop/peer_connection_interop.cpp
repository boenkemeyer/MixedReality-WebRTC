// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

// This is a precompiled header, it must be on its own, followed by a blank
// line, to prevent clang-format from reordering it with other headers.
#include "pch.h"

#include "interop/global_factory.h"
#include "interop/peer_connection_interop.h"
#include "peer_connection.h"

using namespace Microsoft::MixedReality::WebRTC;

extern std::unique_ptr<GlobalFactory> g_factory;

void MRS_CALL mrsPeerConnectionAddRef(PeerConnectionHandle handle) noexcept {
  if (auto peer = static_cast<PeerConnection*>(handle)) {
    peer->AddRef();
  } else {
    RTC_LOG(LS_WARNING)
        << "Trying to add reference to NULL PeerConnection object.";
  }
}

void MRS_CALL mrsPeerConnectionRemoveRef(PeerConnectionHandle handle) noexcept {
  if (auto peer = static_cast<PeerConnection*>(handle)) {
    peer->Release();
  } else {
    RTC_LOG(LS_WARNING)
        << "Trying to remove reference from NULL PeerConnection object.";
  }
}
