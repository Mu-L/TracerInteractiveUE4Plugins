/*
 *  Copyright (c) 2014 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef WEBRTC_MODULES_VIDEO_CODING_UTILITY_QUALITY_SCALER_H_
#define WEBRTC_MODULES_VIDEO_CODING_UTILITY_QUALITY_SCALER_H_

#include "webrtc/common_video/libyuv/include/scaler.h"
#include "webrtc/modules/video_coding/utility/moving_average.h"

namespace webrtc {
class QualityScaler {
 public:
  struct Resolution {
    int width;
    int height;
  };

  QualityScaler();
  void Init(int low_qp_threshold,
            int high_qp_threshold,
            int initial_bitrate_kbps,
            int width,
            int height,
            int fps);
  void ReportFramerate(int framerate);
  void ReportQP(int qp);
  void ReportDroppedFrame();
  void OnEncodeFrame(const VideoFrame& frame);
  Resolution GetScaledResolution() const;
  const VideoFrame& GetScaledFrame(const VideoFrame& frame);
  int downscale_shift() const { return downscale_shift_; }

 private:
  void AdjustScale(bool up);
  void UpdateTargetResolution(int frame_width, int frame_height);
  void ClearSamples();
  void UpdateSampleCounts();

  Scaler scaler_;
  VideoFrame scaled_frame_;

  size_t num_samples_downscale_;
  size_t num_samples_upscale_;
  int measure_seconds_upscale_;
  MovingAverage<int> average_qp_upscale_;
  MovingAverage<int> average_qp_downscale_;

  int framerate_;
  int low_qp_threshold_;
  int high_qp_threshold_;
  MovingAverage<int> framedrop_percent_;
  Resolution res_;

  int downscale_shift_;
};

}  // namespace webrtc

#endif  // WEBRTC_MODULES_VIDEO_CODING_UTILITY_QUALITY_SCALER_H_
