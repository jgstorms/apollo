/******************************************************************************
 * Copyright 2019 The Apollo Authors. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *****************************************************************************/

#include "modules/tools/decode_video/h265_decoder.h"

#include <iostream>

#include "cyber/common/log.h"

// Using to-be-deprecated avcodec_decode_video2 for now until its replacement
// gets stable
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

bool H265Decoder::Init() {
  avcodec_register_all();
  codec_h265_ = avcodec_find_decoder(AV_CODEC_ID_H265);
  if (codec_h265_ == nullptr) {
    AERROR << "error: codec not found";
    return false;
  }
  codec_ctx_h265_ = avcodec_alloc_context3(codec_h265_);
  if (codec_ctx_h265_ == nullptr) {
    AERROR << "error: codec context alloc fail";
    return false;
  }
  if (avcodec_open2(codec_ctx_h265_, codec_h265_, nullptr) < 0) {
    AERROR << "error: could not open codec";
    return false;
  }
  yuv_frame_ = av_frame_alloc();
  if (yuv_frame_ == nullptr) {
    AERROR << "error: could not alloc yuv frame";
    return false;
  }
  codec_jpeg_ = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
  if (codec_jpeg_ == nullptr) {
    AERROR << "error: jpeg Codec not found";
    return false;
  }
  codec_ctx_jpeg_ = avcodec_alloc_context3(codec_jpeg_);
  if (codec_ctx_jpeg_ == nullptr) {
    AERROR << "error: jpeg ctx allco fail";
    return false;
  }
  // Put sample parameters and open it
  codec_ctx_jpeg_->bit_rate = 400000;
  codec_ctx_jpeg_->codec_type = AVMEDIA_TYPE_VIDEO;
  codec_ctx_jpeg_->codec_id = AV_CODEC_ID_MJPEG;
  codec_ctx_jpeg_->width = 1920;
  codec_ctx_jpeg_->height = 1080;
  codec_ctx_jpeg_->time_base = (AVRational){1, 15};
  codec_ctx_jpeg_->pix_fmt = AV_PIX_FMT_YUVJ422P;
  if (avcodec_open2(codec_ctx_jpeg_, codec_jpeg_, nullptr) < 0) {
    AERROR << "error: could not open jpeg context";
    return false;
  }
  return true;
}

void H265Decoder::Release() {
  if (codec_ctx_h265_ != nullptr) {
    avcodec_free_context(&codec_ctx_h265_);
    codec_ctx_h265_ = nullptr;
  }
  if (yuv_frame_ != nullptr) {
    av_frame_free(&yuv_frame_);
    yuv_frame_ = nullptr;
  }
  if (codec_ctx_jpeg_ != nullptr) {
    avcodec_free_context(&codec_ctx_jpeg_);
    codec_ctx_jpeg_ = nullptr;
  }
  codec_h265_ = nullptr;
  codec_jpeg_ = nullptr;
}

int H265Decoder::Process(const char* indata, const int32_t insize,
                         char* outdata, const int32_t outsize) {
  AVPacket apt;
  av_init_packet(&apt);
  int got_picture = 0;
  apt.data = (uint8_t*)indata;
  apt.size = insize;
  if (insize == 0) {
    apt.data = nullptr;
  }
  int ret =
      avcodec_decode_video2(codec_ctx_h265_, yuv_frame_, &got_picture, &apt);
  if (ret < 0) {
    AERROR << "error: decode failed: input_framesize = " << apt.size;
    return ret;
  }
  if (!got_picture) {
    // Not an error, but just did not read pictures out
    AWARN << "warn: failed to get yuv picture";
    return 0;
  }
  av_packet_unref(&apt);
  got_picture = 0;
  ret = avcodec_encode_video2(codec_ctx_jpeg_, &apt, yuv_frame_, &got_picture);
  if (ret < 0) {
    AERROR << "error: jpeg encode failed";
    return ret;
  }
  if (!got_picture) {
    AERROR << "error: failed to get jpeg picture";
    return -1;
  }
  if (apt.size > outsize) {
    AERROR << "error: output buffer is two small, alloc/framesize " << outsize
           << "/" << apt.size;
    return -1;
  }
  memcpy(outdata, apt.data, apt.size);
  return apt.size;
}