// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See LICENSE in the project root for license information.

using System;
using UnityEngine;

namespace Microsoft.MixedReality.WebRTC.Unity
{
    /// <summary>
    /// Abstract base component for a custom video source delivering raw video frames
    /// directly to the WebRTC implementation.
    /// </summary>
    public abstract class CustomVideoSource<T> : VideoSource where T : class, IVideoFrameStorage, new()
    {
        /// <summary>
        /// Peer connection this local video source will add a video track to.
        /// </summary>
        [Header("Video track")]
        [Tooltip("Peer connection this video track is added to.")]
        public PeerConnection PeerConnection;

        /// <summary>
        /// Name of the track.
        /// </summary>
        /// <remarks>
        /// This must comply with the 'msid' attribute rules as defined in
        /// https://tools.ietf.org/html/draft-ietf-mmusic-msid-05#section-2, which in
        /// particular constraints the set of allows characters to those allowed for a
        /// 'token' element as specified in https://tools.ietf.org/html/rfc4566#page-43:
        /// - Symbols [!#$%'*+-.^_`{|}~] and ampersand &amp;
        /// - Alphanumerical [A-Za-z0-9]
        /// </remarks>
        /// <seealso xref="SdpTokenAttribute.ValidateSdpTokenName"/>
        [Tooltip("SDP track name.")]
        [SdpToken(allowEmpty: true)]
        public string TrackName;

        /// <summary>
        /// Automatically start the video track playback when the component is enabled.
        /// </summary>
        [Tooltip("Automatically start local video capture when this component is enabled")]
        public bool AutoStartCapture = true;

        /// <summary>
        /// Video track encapsulated by this component.
        /// </summary>
        public LocalVideoTrack Track { get; private set; }

        /// <summary>
        /// Frame queue holding the pending frames enqueued by the video source itself,
        /// which a video renderer needs to read and display.
        /// </summary>
        public VideoFrameQueue<ARGBVideoFrameStorage> _frameQueue;

        /// <summary>
        /// Add a new track to the peer connection and start the video track playback.
        /// </summary>
        public void StartTrack()
        {
            // Ensure the track has a valid name
            string trackName = TrackName;
            if (trackName.Length == 0)
            {
                trackName = Guid.NewGuid().ToString();
                TrackName = trackName;
            }
            SdpTokenAttribute.Validate(trackName, allowEmpty: false);

            var nativePeer = PeerConnection.Peer;
            //< TODO - Better abstraction
            if (typeof(T) == typeof(I420VideoFrameStorage))
            {
                Track = nativePeer.AddCustomI420LocalVideoTrack(trackName, OnFrameRequested);
            }
            else if (typeof(T) == typeof(ARGBVideoFrameStorage))
            {
                Track = nativePeer.AddCustomArgb32LocalVideoTrack(trackName, OnFrameRequested);
            }
            else
            {
                throw new NotSupportedException("");
            }
            if (Track != null)
            {
                VideoStreamStarted.Invoke();
            }
        }

        /// <summary>
        /// Stop the video track playback and remove the track from the peer connection.
        /// </summary>
        public void StopTrack()
        {
            if (Track != null)
            {
                var nativePeer = PeerConnection.Peer;
                nativePeer.RemoveLocalVideoTrack(Track);
                Track.Dispose();
                Track = null;
                VideoStreamStopped.Invoke();
            }
            _frameQueue.Clear();
        }

        protected void Awake()
        {
            _frameQueue = new VideoFrameQueue<ARGBVideoFrameStorage>(3);
            FrameQueue = _frameQueue;
            PeerConnection.OnInitialized.AddListener(OnPeerInitialized);
            PeerConnection.OnShutdown.AddListener(OnPeerShutdown);
        }

        protected void OnDestroy()
        {
            StopTrack();
            PeerConnection.OnInitialized.RemoveListener(OnPeerInitialized);
            PeerConnection.OnShutdown.RemoveListener(OnPeerShutdown);
        }

        protected void OnEnable()
        {
            if (Track != null)
            {
                Track.Enabled = true;
            }
        }

        protected void OnDisable()
        {
            if (Track != null)
            {
                Track.Enabled = false;
            }
        }

        private void OnPeerInitialized()
        {
            if (AutoStartCapture)
            {
                StartTrack();
            }
        }

        private void OnPeerShutdown()
        {
            StopTrack();
        }

        protected abstract void OnFrameRequested(FrameRequest request);
    }
}
