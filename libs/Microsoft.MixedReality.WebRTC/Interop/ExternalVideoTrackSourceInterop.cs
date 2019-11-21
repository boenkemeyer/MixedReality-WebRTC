// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

using System;
using System.Runtime.InteropServices;

namespace Microsoft.MixedReality.WebRTC.Interop
{
    internal class ExternalVideoTrackSourceInterop
    {
        #region Native functions

        [DllImport(Utils.dllPath, CallingConvention = CallingConvention.StdCall, CharSet = CharSet.Ansi,
            EntryPoint = "mrsExternalVideoTrackSourceAddRef")]
        public static unsafe extern void ExternalVideoTrackSource_AddRef(IntPtr handle);

        [DllImport(Utils.dllPath, CallingConvention = CallingConvention.StdCall, CharSet = CharSet.Ansi,
            EntryPoint = "mrsExternalVideoTrackSourceRemoveRef")]
        public static unsafe extern void ExternalVideoTrackSource_RemoveRef(IntPtr handle);

        [DllImport(Utils.dllPath, CallingConvention = CallingConvention.StdCall, CharSet = CharSet.Ansi,
            EntryPoint = "mrsExternalVideoTrackSourceShutdown")]
        public static extern void ExternalVideoTrackSource_Shutdown(IntPtr handle);

        [DllImport(Utils.dllPath, CallingConvention = CallingConvention.StdCall, CharSet = CharSet.Ansi,
            EntryPoint = "mrsExternalVideoTrackSourceCompleteI420AVideoFrameRequest")]
        public static extern uint ExternalVideoTrackSource_CompleteI420AVideoFrameRequest(IntPtr sourceHandle,
            uint requestId, in I420AVideoFrame frame);

        [DllImport(Utils.dllPath, CallingConvention = CallingConvention.StdCall, CharSet = CharSet.Ansi,
            EntryPoint = "mrsExternalVideoTrackSourceCompleteArgb32VideoFrameRequest")]
        public static extern uint ExternalVideoTrackSource_CompleteArgb32VideoFrameRequest(IntPtr sourceHandle,
            uint requestId, in ARGBVideoFrame frame);

        #endregion


        #region Helpers

        public static void CompleteExternalI420AVideoFrameRequest(IntPtr sourceHandle, uint requestId, I420AVideoFrame frame)
        {
            uint res = ExternalVideoTrackSource_CompleteI420AVideoFrameRequest(sourceHandle, requestId, frame);
            Utils.ThrowOnErrorCode(res);
        }

        public static void CompleteExternalArgb32VideoFrameRequest(IntPtr sourceHandle, uint requestId, ARGBVideoFrame frame)
        {
            uint res = ExternalVideoTrackSource_CompleteArgb32VideoFrameRequest(sourceHandle, requestId, frame);
            Utils.ThrowOnErrorCode(res);
        }

        #endregion
    }
}
