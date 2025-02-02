// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "pch.h"

#include "interop/interop_api.h"

#if !defined(MRSW_EXCLUDE_DEVICE_TESTS)

namespace {

// PeerConnectionAudioFrameCallback
using AudioFrameCallback = InteropCallback<const void*,
                                           const uint32_t,
                                           const uint32_t,
                                           const uint32_t,
                                           const uint32_t>;

bool IsSilent_uint8(const uint8_t* data,
                    uint32_t size,
                    uint8_t& min,
                    uint8_t& max) noexcept {
  // 8bpp in [0:255] range, UINT8
  const uint8_t* const s = data;
  const uint8_t* const e = s + size;
  // Currently "mute" on audio does not mute completely, so the frame is
  // not *exactly* zero. So check if it's close enough.
  min = 255;
  max = 0;
  for (const uint8_t* p = s; p < e; ++p) {
    min = std::min(min, *p);
    max = std::max(max, *p);
  }
  const bool is_silent = (min >= 126) && (max <= 129);  // ~1%
  return is_silent;
}

bool IsSilent_int16(const int16_t* data,
                    uint32_t size,
                    int16_t& min,
                    int16_t& max) noexcept {
  // 16bpp in [-32768:32767] range, SINT16
  const int16_t* const s = data;
  const int16_t* const e = s + size;
  // Currently "mute" on audio does not mute completely, so the frame is
  // not *exactly* zero. So check if it's close enough.
  min = 32767;
  max = -32768;
  for (const int16_t* p = s; p < e; ++p) {
    min = std::min(min, *p);
    max = std::max(max, *p);
  }
  const bool is_silent = (min >= -5) && (max <= 5);  // ~1.5e-4 = 0.015%
  return is_silent;
}

}  // namespace

//
// TODO : Those tests are currently partially disabled because
// - when not muted, the audio track needs some non-zero signal from the
// microphone for the test to pass, which requires someone or something to make
// some noise, and cannot be easily automated at that time.
// - when muted, the audio signal is still non-zero, possibly because of the way
// mute is implemented (no bool, only clears the buffer) and some minor rounding
// errors in subsequent processing, or other... in any case it is not exactly
// zero like for video. (NB: voice activation doesn't seem to have much effect).
//
// Note however that using headphones and microphone, we can clearly ear the
// first test (Simple) having the microphone enabled, and audio played back in
// the earphones speakers, while in the second test (Muted) the audio is clearly
// silent from a perceptual point of view.
//

TEST(AudioTrack, Simple) {
  LocalPeerPairRaii pair;

  ASSERT_EQ(MRS_SUCCESS, mrsPeerConnectionAddLocalAudioTrack(pair.pc1()));
  ASSERT_NE(mrsBool::kFalse,
            mrsPeerConnectionIsLocalAudioTrackEnabled(pair.pc1()));

  uint32_t call_count = 0;
  AudioFrameCallback audio_cb = [&call_count](const void* audio_data,
                                              const uint32_t bits_per_sample,
                                              const uint32_t sample_rate,
                                              const uint32_t number_of_channels,
                                              const uint32_t number_of_frames) {
    ASSERT_NE(nullptr, audio_data);
    ASSERT_LT(0u, bits_per_sample);
    ASSERT_LT(0u, sample_rate);
    ASSERT_LT(0u, number_of_channels);
    ASSERT_LT(0u, number_of_frames);
    // TODO - See comment above
    // if (bits_per_sample == 8) {
    //  uint8_t min, max;
    //  const bool is_silent =
    //      IsSilent_uint8((const uint8_t*)audio_data,
    //                     number_of_frames * number_of_channels, min, max);
    //  EXPECT_FALSE(is_silent)
    //      << "uint8 call #" << call_count << " min=" << min << " max=" << max;
    //} else if (bits_per_sample == 16) {
    //  int16_t min, max;
    //  const bool is_silent =
    //      IsSilent_int16((const int16_t*)audio_data,
    //                     number_of_frames * number_of_channels, min, max);
    //  EXPECT_FALSE(is_silent)
    //      << "int16 call #" << call_count << " min=" << min << " max=" << max;
    //} else {
    //  ASSERT_TRUE(false) << "Unkown bits per sample (not 8 nor 16)";
    //}
    ++call_count;
  };
  mrsPeerConnectionRegisterRemoteAudioFrameCallback(pair.pc2(), CB(audio_cb));

  pair.ConnectAndWait();

  // Check several times this, because the audio "mute" is flaky, does not
  // really mute the audio, so check that the reported status is still
  // correct.
  ASSERT_NE(mrsBool::kFalse,
            mrsPeerConnectionIsLocalAudioTrackEnabled(pair.pc1()));

  Event ev;
  ev.WaitFor(5s);
  ASSERT_LT(50u, call_count);  // at least 10 CPS

  // Same as above
  ASSERT_NE(mrsBool::kFalse,
            mrsPeerConnectionIsLocalAudioTrackEnabled(pair.pc1()));

  mrsPeerConnectionRegisterRemoteAudioFrameCallback(pair.pc2(), nullptr,
                                                    nullptr);
}

TEST(AudioTrack, Muted) {
  LocalPeerPairRaii pair;

  ASSERT_EQ(MRS_SUCCESS, mrsPeerConnectionAddLocalAudioTrack(pair.pc1()));

  // Disable the audio track; it should output only silence
  ASSERT_EQ(MRS_SUCCESS, mrsPeerConnectionSetLocalAudioTrackEnabled(
                             pair.pc1(), mrsBool::kFalse));
  ASSERT_EQ(mrsBool::kFalse,
            mrsPeerConnectionIsLocalAudioTrackEnabled(pair.pc1()));

  uint32_t call_count = 0;
  AudioFrameCallback audio_cb =
      [&call_count, &pair](
          const void* audio_data, const uint32_t bits_per_sample,
          const uint32_t sample_rate, const uint32_t number_of_channels,
          const uint32_t number_of_frames) {
        ASSERT_NE(nullptr, audio_data);
        ASSERT_LT(0u, bits_per_sample);
        ASSERT_LT(0u, sample_rate);
        ASSERT_LT(0u, number_of_channels);
        ASSERT_LT(0u, number_of_frames);
        // TODO - See comment above
        // if (bits_per_sample == 8) {
        //  uint8_t min, max;
        //  const bool is_silent =
        //      IsSilent_uint8((const uint8_t*)audio_data,
        //                     number_of_frames * number_of_channels, min, max);
        //  EXPECT_TRUE(is_silent) << "uint8 call #" << call_count
        //                         << " min=" << min << " max=" << max;
        //} else if (bits_per_sample == 16) {
        //  int16_t min, max;
        //  const bool is_silent =
        //      IsSilent_int16((const int16_t*)audio_data,
        //                     number_of_frames * number_of_channels, min, max);
        //  EXPECT_TRUE(is_silent) << "int16 call #" << call_count
        //                         << " min=" << min << " max=" << max;
        //} else {
        //  ASSERT_TRUE(false) << "Unkown bits per sample (not 8 nor 16)";
        //}
        ++call_count;
      };
  mrsPeerConnectionRegisterRemoteAudioFrameCallback(pair.pc2(), CB(audio_cb));

  pair.ConnectAndWait();

  // Check several times this, because the audio "mute" is flaky, does not
  // really mute the audio, so check that the reported status is still
  // correct.
  ASSERT_EQ(mrsBool::kFalse,
            mrsPeerConnectionIsLocalAudioTrackEnabled(pair.pc1()));

  Event ev;
  ev.WaitFor(5s);
  ASSERT_LT(50u, call_count);  // at least 10 CPS

  // Same as above
  ASSERT_EQ(mrsBool::kFalse,
            mrsPeerConnectionIsLocalAudioTrackEnabled(pair.pc1()));

  mrsPeerConnectionRegisterRemoteAudioFrameCallback(pair.pc2(), nullptr,
                                                    nullptr);
}

#endif  // MRSW_EXCLUDE_DEVICE_TESTS
