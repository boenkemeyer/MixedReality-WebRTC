// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

using System;
using System.Diagnostics.Tracing;

namespace Microsoft.MixedReality.WebRTC.Tracing
{
    /// <summary>
    /// Global event source for logging and tracing.
    /// 
    /// Usage:
    /// - Select a unique event ID.
    /// - Define an event as a public method with a name related to the type of event to be logged, and
    ///   tagged with the <see xref="System.Diagnostics.Tracing.EventAttribute"/> attribute, specifying
    ///   the event ID as the first argument.
    /// - Use one of the WriteEvent methods to write the event and any associated field.
    /// - In code, use the <see cref="MainEventSource.Log"/> static instance to log some event by calling
    ///   the event public method just defined.
    /// </summary>
    /// <remarks>
    /// On Windows, this logs ETW events to the "Microsoft.MixedReality.WebRTC" event provider
    /// (GUID: 00AEE89E-B531-4F20-A2C5-D02F37CB6AA1) which can be captured with Windows Performance
    /// Recorder (WPR). The <c>tools/tracing/</c> folder contains a WPR profile (*.wprp) which can be
    /// imported in wpr.exe to activate that event provider and record its events.
    /// </remarks>
    [EventSource(Name = "Microsoft.MixedReality.WebRTC", Guid = "00AEE89E-B531-4F20-A2C5-D02F37CB6AA1")]
    internal sealed class MainEventSource : EventSource
    {
        /// <summary>
        /// Global event source instance to use for logging events.
        /// </summary>
        public static MainEventSource Log = new MainEventSource();

        /// <summary>
        /// Event categories.
        /// </summary>
        public static class Keywords
        {
            /// <summary>
            /// Event related to the peer connection.
            /// </summary>
            public const EventKeywords Connection = (EventKeywords)1;

            /// <summary>
            /// Event related to the SDP session management, like sending SDP messages
            /// and ICE candidates, and handling renegotiations.
            /// </summary>
            public const EventKeywords Sdp = (EventKeywords)2;

            /// <summary>
            /// Event related to audio and video (tracks, sources, capture, ...).
            /// </summary>
            public const EventKeywords Media = (EventKeywords)4;

            /// <summary>
            /// Event related to data channels.
            /// </summary>
            public const EventKeywords DataChannel = (EventKeywords)8;
        }

        /// <summary>
        /// Initialize the event source. This must be called once before any use of the event source.
        /// </summary>
        public void Initialize()
        {
            // This is a dummy call to force the instantiation of the static Log instance
            // while on a C# thread early in the program, and not at a random time during
            // an event firing deep inside a PInvoke-ReversePInvoke callstack.
            //
            // Experiments show that when calling the EventSource() constructor inside a
            // reverse PInvoke call (for example when logging from a C# callback invoked
            // by native code) the callstack gets garbled. Unity devs also hint at some
            // possible issue with marshaling in the constructor:
            // https://issuetracker.unity3d.com/issues/il2cpp-etw-events-are-not-triggered-by-eventsource-class
        }

        //[Event(0x10, Level = EventLevel.Error)]
        public void NativeError(uint res) { /*WriteEvent(0x10, res);*/ }

        #region Connection

        [Event(0x1001, Level = EventLevel.Informational, Keywords = Keywords.Connection)]
        public void Connected() { WriteEvent(0x1001); }

        [Event(0x1002, Level = EventLevel.Informational, Keywords = Keywords.Connection)]
        public void IceStateChanged(IceConnectionState newState) { WriteEvent(0x1002, (int)newState); }

        [Event(0x1003, Level = EventLevel.Informational, Keywords = Keywords.Connection)]
        public void RenegotiationNeeded() { WriteEvent(0x1003); }

        [Event(0x1004, Level = EventLevel.Error, Keywords = Keywords.Connection)]
        public void PeerConnectionNotOpenError() { WriteEvent(0x1004); }

        #endregion

        #region SDP

        [Event(0x2001, Level = EventLevel.Informational, Keywords = Keywords.Sdp)]
        public void LocalSdpReady(string type, string sdp) { WriteEvent(0x2001, type, sdp); }

        [Event(0x2002, Level = EventLevel.Informational, Keywords = Keywords.Sdp)]
        public void IceCandidateReady(string sdpMid, int sdpMlineindex, string candidate)
        {
            WriteEvent(0x2002, sdpMid, sdpMlineindex, candidate);
        }

        [Event(0x2003, Level = EventLevel.Informational, Keywords = Keywords.Sdp)]
        public void CreateOffer() { WriteEvent(0x2003); }

        [Event(0x2004, Level = EventLevel.Informational, Keywords = Keywords.Sdp)]
        public void CreateAnswer() { WriteEvent(0x2004); }

        [Event(0x2005, Level = EventLevel.Informational, Keywords = Keywords.Sdp)]
        public void AddIceCandidate(string sdpMid, int sdpMlineindex, string candidate)
        {
            WriteEvent(0x2005, sdpMid, sdpMlineindex, candidate);
        }

        #endregion

        #region Media

        [Event(0x3001, Level = EventLevel.Informational, Keywords = Keywords.Media)]
        public void TrackAdded(PeerConnection.TrackKind trackKind) { WriteEvent(0x3001, (int)trackKind); }

        [Event(0x3002, Level = EventLevel.Informational, Keywords = Keywords.Media)]
        public void TrackRemoved(PeerConnection.TrackKind trackKind) { WriteEvent(0x3002, (int)trackKind); }

        [Event(0x3003, Level = EventLevel.Verbose, Keywords = Keywords.Media)]
        public void I420ALocalVideoFrameReady(uint width, uint height) { WriteEvent(0x3003, (int)width, (int)height); }

        [Event(0x3004, Level = EventLevel.Verbose, Keywords = Keywords.Media)]
        public void Argb32LocalVideoFrameReady(uint width, uint height) { WriteEvent(0x3004, (int)width, (int)height); }

        [Event(0x3005, Level = EventLevel.Verbose, Keywords = Keywords.Media)]
        public void I420ARemoteVideoFrameReady(uint width, uint height) { WriteEvent(0x3005, (int)width, (int)height); }

        [Event(0x3006, Level = EventLevel.Verbose, Keywords = Keywords.Media)]
        public void Argb32RemoteVideoFrameReady(uint width, uint height) { WriteEvent(0x3006, (int)width, (int)height); }

        [Event(0x3007, Level = EventLevel.Verbose, Keywords = Keywords.Media)]
        public void LocalAudioFrameReady(uint bitsPerSample, uint channelCount, uint frameCount)
        {
            WriteEvent(0x3007, (int)bitsPerSample, (int)channelCount, (int)frameCount);
        }

        [Event(0x3008, Level = EventLevel.Verbose, Keywords = Keywords.Media)]
        public void RemoteAudioFrameReady(uint bitsPerSample, uint channelCount, uint frameCount)
        {
            WriteEvent(0x3008, (int)bitsPerSample, (int)channelCount, (int)frameCount);
        }

        #endregion

        #region DataChannel

        [Event(0x4001, Level = EventLevel.Informational, Keywords = Keywords.DataChannel)]
        public void DataChannelAdded(int id, string label) { WriteEvent(0x4001, id, label); }

        [Event(0x4002, Level = EventLevel.Informational, Keywords = Keywords.DataChannel)]
        public void DataChannelRemoved(int id, string label) { WriteEvent(0x4002, id, label); }

        [Event(0x4003, Level = EventLevel.Informational, Keywords = Keywords.DataChannel)]
        public void DataChannelBufferingChanged(int id, ulong previous, ulong current, ulong limit)
        {
            WriteEvent(0x4003, id, previous, current, limit);
        }

        [Event(0x4004, Level = EventLevel.Informational, Keywords = Keywords.DataChannel)]
        public void DataChannelStateChanged(int id, DataChannel.ChannelState state) { WriteEvent(0x4004, id, (int)state); }

        [Event(0x4005, Level = EventLevel.Verbose, Keywords = Keywords.DataChannel)]
        public void DataChannelSendMessage(int id, int byteSize) { WriteEvent(0x4005, id, byteSize); }

        [Event(0x4006, Level = EventLevel.Verbose, Keywords = Keywords.DataChannel)]
        public void DataChannelMessageReceived(int id, int byteSize) { WriteEvent(0x4006, id, byteSize); }

        #endregion

        #region EventWrite overloads

        [NonEvent]
        public unsafe void WriteEvent(int eventId, string arg1, int arg2, string arg3)
        {
            fixed (char* arg1Ptr = arg1)
            {
                fixed (char* arg3Ptr = arg3)
                {
                    EventData* dataDesc = stackalloc EventData[3];
                    dataDesc[0].DataPointer = (IntPtr)arg1Ptr;
                    dataDesc[0].Size = (arg1.Length + 1) * 2; // Size in bytes, including a null terminator.
                    dataDesc[1].DataPointer = (IntPtr)(&arg2);
                    dataDesc[1].Size = 4;
                    dataDesc[2].DataPointer = (IntPtr)arg3Ptr;
                    dataDesc[2].Size = (arg3.Length + 1) * 2; // Size in bytes, including a null terminator.
                    WriteEventCore(eventId, 3, dataDesc);
                }
            }
        }

        [NonEvent]
        public unsafe void WriteEvent(int eventId, int arg1, ulong arg2, ulong arg3, ulong arg4)
        {
            EventData* dataDesc = stackalloc EventData[4];
            dataDesc[0].DataPointer = (IntPtr)(&arg1);
            dataDesc[0].Size = 4;
            dataDesc[1].DataPointer = (IntPtr)(&arg2);
            dataDesc[1].Size = 4;
            dataDesc[2].DataPointer = (IntPtr)(&arg3);
            dataDesc[2].Size = 4;
            dataDesc[3].DataPointer = (IntPtr)(&arg4);
            dataDesc[3].Size = 4;
            WriteEventCore(eventId, 4, dataDesc);
        }

        #endregion
    }
}
