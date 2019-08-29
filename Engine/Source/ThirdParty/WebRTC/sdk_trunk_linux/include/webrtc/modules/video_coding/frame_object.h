/*
 *  Copyright (c) 2016 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef WEBRTC_MODULES_VIDEO_CODING_FRAME_OBJECT_H_
#define WEBRTC_MODULES_VIDEO_CODING_FRAME_OBJECT_H_

#include "webrtc/common_types.h"
#include "webrtc/modules/include/module_common_types.h"

namespace webrtc {
namespace video_coding {

class FrameObject {
 public:
  static const uint8_t kMaxFrameReferences = 5;

  FrameObject();

  virtual bool GetBitstream(uint8_t* destination) const = 0;
  virtual ~FrameObject() {}

  // The tuple (|picture_id|, |spatial_layer|) uniquely identifies a frame
  // object. For codec types that don't necessarily have picture ids they
  // have to be constructed from the header data relevant to that codec.
  uint16_t picture_id;
  uint8_t spatial_layer;
  uint32_t timestamp;

  size_t num_references;
  uint16_t references[kMaxFrameReferences];
  bool inter_layer_predicted;
};

class PacketBuffer;

class RtpFrameObject : public FrameObject {
 public:
  RtpFrameObject(PacketBuffer* packet_buffer,
                 uint16_t first_seq_num,
                 uint16_t last_seq_num);

  ~RtpFrameObject();
  uint16_t first_seq_num() const;
  uint16_t last_seq_num() const;
  FrameType frame_type() const;
  VideoCodecType codec_type() const;
  bool GetBitstream(uint8_t* destination) const override;
  RTPVideoTypeHeader* GetCodecHeader() const;

 private:
  PacketBuffer* packet_buffer_;
  FrameType frame_type_;
  VideoCodecType codec_type_;
  uint16_t first_seq_num_;
  uint16_t last_seq_num_;
};

}  // namespace video_coding
}  // namespace webrtc

#endif  // WEBRTC_MODULES_VIDEO_CODING_FRAME_OBJECT_H_
