/*
 *  Copyright (c) 2015 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 *
 */

#include "mmal_h264_encoder.h"

#include <limits>
#include <string>

#include "third_party/libyuv/include/libyuv/convert.h"
#include "third_party/libyuv/include/libyuv/convert_from.h"
#include "third_party/libyuv/include/libyuv/video_common.h"

#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "common_video/libyuv/include/webrtc_libyuv.h"
#include "system_wrappers/include/metrics.h"

#include "rtc/native_buffer.h"

#define H264HWENC_HEADER_DEBUG 0
#define ROUND_UP_4(num) (((num) + 3) & ~3)

namespace
{
struct nal_entry
{
  size_t offset;
  size_t size;
};

const int kLowH264QpThreshold = 34;
const int kHighH264QpThreshold = 40;

int I420DataSize(const webrtc::I420BufferInterface &frame_buffer)
{
  return frame_buffer.StrideY() * frame_buffer.height() + (frame_buffer.StrideU() + frame_buffer.StrideV()) * ((frame_buffer.height() + 1) / 2);
}

} // namespace

MMALH264Encoder::MMALH264Encoder(const cricket::VideoCodec &codec)
    : callback_(nullptr),
      decoder_(nullptr),
      resizer_(nullptr),
      encoder_(nullptr),
      conn1_(nullptr),
      conn2_(nullptr),
      queue_(nullptr),
      pool_out_(nullptr),
      bitrate_adjuster_(.5, .95),
      configured_framerate_(30),
      configured_width_(0),
      configured_height_(0),
      use_mjpeg_(false),
      encoded_buffer_length_(0)
{
}

MMALH264Encoder::~MMALH264Encoder()
{
  Release();
}

int32_t MMALH264Encoder::InitEncode(const webrtc::VideoCodec *codec_settings,
                                  int32_t number_of_cores,
                                  size_t max_payload_size)
{
  RTC_DCHECK(codec_settings);
  RTC_DCHECK_EQ(codec_settings->codecType, webrtc::kVideoCodecH264);

  int32_t release_ret = Release();
  if (release_ret != WEBRTC_VIDEO_CODEC_OK)
  {
    return release_ret;
  }

  bcm_host_init();

  width_ = codec_settings->width;
  height_ = codec_settings->height;
  target_bitrate_bps_ = codec_settings->startBitrate * 1000;
  bitrate_adjuster_.SetTargetBitrateBps(target_bitrate_bps_);
  framerate_ = codec_settings->maxFramerate;
  if (framerate_ > 30)
  {
    framerate_ = 30;
  }

  RTC_LOG(LS_INFO) << "InitEncode " << framerate_ << "fps "
                    << target_bitrate_bps_ << "bit/sec";

  // Initialize encoded image. Default buffer size: size of unencoded data.
  encoded_image_._completeFrame = true;
  encoded_image_._encodedWidth = 0;
  encoded_image_._encodedHeight = 0;
  encoded_image_.set_size(0);
  encoded_image_.timing_.flags = webrtc::VideoSendTiming::TimingFrameFlags::kInvalid;
  encoded_image_.content_type_ = (codec_settings->mode == webrtc::VideoCodecMode::kScreensharing)
                                     ? webrtc::VideoContentType::SCREENSHARE
                                     : webrtc::VideoContentType::UNSPECIFIED;

  return WEBRTC_VIDEO_CODEC_OK;
}

int32_t MMALH264Encoder::Release()
{
  return WEBRTC_VIDEO_CODEC_OK;
}

int32_t MMALH264Encoder::MMALConfigure()
{
  RTC_LOG(LS_ERROR) << __FUNCTION__ << " Start";
  int32_t stride_width = VCOS_ALIGN_UP(width_, 32);
  int32_t stride_height = VCOS_ALIGN_UP(height_, 16);

  if (mmal_component_create(MMAL_COMPONENT_DEFAULT_VIDEO_ENCODER, &encoder_) != MMAL_SUCCESS)
  {
    RTC_LOG(LS_ERROR) << "Failed to create mmal encoder";
    Release();
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  MMAL_COMPONENT_T* component_in;
  MMAL_ES_FORMAT_T *format_in;
  if (use_mjpeg_)
  {
    if (mmal_component_create(MMAL_COMPONENT_DEFAULT_VIDEO_DECODER, &decoder_) != MMAL_SUCCESS)
    {
      RTC_LOG(LS_ERROR) << "Failed to create mmal decoder";
      Release();
      return WEBRTC_VIDEO_CODEC_ERROR;
    }
    format_in = decoder_->input[0]->format;
    format_in->type = MMAL_ES_TYPE_VIDEO;
    format_in->encoding = MMAL_ENCODING_MJPEG;
    format_in->es->video.width = raw_width_;
    format_in->es->video.height = raw_height_;
    component_in = decoder_;

    if (mmal_component_create("vc.ril.resize", &resizer_) != MMAL_SUCCESS)
    {
      RTC_LOG(LS_ERROR) << "Failed to create mmal resizer";
      Release();
      return WEBRTC_VIDEO_CODEC_ERROR;
    }

    MMAL_ES_FORMAT_T *format_resize;
    format_resize = resizer_->output[0]->format;
    mmal_format_copy(format_resize, resizer_->input[0]->format);
    format_resize->es->video.width = stride_width;
    format_resize->es->video.height = stride_height;
    format_resize->es->video.crop.x = 0;
    format_resize->es->video.crop.y = 0;
    format_resize->es->video.crop.width = width_;
    format_resize->es->video.crop.height = height_;
    format_resize->es->video.frame_rate.num = framerate_;
    format_resize->es->video.frame_rate.den = 1;
    if (mmal_port_format_commit(resizer_->output[0]) != MMAL_SUCCESS)
    {
      RTC_LOG(LS_ERROR) << "Failed to commit output port format";
      return WEBRTC_VIDEO_CODEC_ERROR;
    }
  }
  else
  {
    format_in = encoder_->input[0]->format;
    format_in->type = MMAL_ES_TYPE_VIDEO;
    format_in->encoding = MMAL_ENCODING_I420;
    format_in->es->video.width = stride_width;
    format_in->es->video.height = stride_height;
    format_in->es->video.crop.x = 0;
    format_in->es->video.crop.y = 0;
    format_in->es->video.crop.width = width_;
    format_in->es->video.crop.height = height_;
    component_in = encoder_;
  }
  
  format_in->es->video.frame_rate.num = framerate_;
  format_in->es->video.frame_rate.den = 1;

  if (mmal_port_format_commit(component_in->input[0]) != MMAL_SUCCESS)
  {
    RTC_LOG(LS_ERROR) << "Failed to commit input port format";
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  /* Output port configure for H264 */
  MMAL_ES_FORMAT_T *format_out = encoder_->output[0]->format;
  mmal_format_copy(format_out, format_in);
  encoder_->output[0]->format->type = MMAL_ES_TYPE_VIDEO;
  encoder_->output[0]->format->encoding = MMAL_ENCODING_H264;
  encoder_->output[0]->format->es->video.frame_rate.num = framerate_;
  encoder_->output[0]->format->es->video.frame_rate.den = 1;
  encoder_->output[0]->format->bitrate = bitrate_adjuster_.GetAdjustedBitrateBps();

  if (mmal_port_format_commit(encoder_->output[0]) != MMAL_SUCCESS)
  {
    RTC_LOG(LS_ERROR) << "Failed to commit output port format";
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  if (mmal_port_parameter_set_boolean(component_in->input[0], MMAL_PARAMETER_ZERO_COPY, MMAL_TRUE) != MMAL_SUCCESS)
  {
    RTC_LOG(LS_ERROR) << "Failed to set input zero copy";
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  if (mmal_port_parameter_set_boolean(encoder_->output[0], MMAL_PARAMETER_ZERO_COPY, MMAL_TRUE) != MMAL_SUCCESS)
  {
    RTC_LOG(LS_ERROR) << "Failed to set output zero copy";
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  MMAL_PARAMETER_VIDEO_PROFILE_T video_profile;
  video_profile.hdr.id = MMAL_PARAMETER_PROFILE;
  video_profile.hdr.size = sizeof(video_profile);

  video_profile.profile[0].profile = MMAL_VIDEO_PROFILE_H264_HIGH;
  video_profile.profile[0].level = MMAL_VIDEO_LEVEL_H264_42;

  if (mmal_port_parameter_set(encoder_->output[0], &video_profile.hdr) != MMAL_SUCCESS)
  {
    RTC_LOG(LS_ERROR) << "Failed to set H264 profile";
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  if (mmal_port_parameter_set_uint32(encoder_->output[0], MMAL_PARAMETER_INTRAPERIOD, 0) != MMAL_SUCCESS)
  {
    RTC_LOG(LS_ERROR) << "Failed to set intra period";
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  if (mmal_port_parameter_set_boolean(encoder_->output[0], MMAL_PARAMETER_VIDEO_ENCODE_INLINE_HEADER, MMAL_TRUE) != MMAL_SUCCESS)
  {
    RTC_LOG(LS_ERROR) << "Failed to set enable inline header";
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  queue_ = mmal_queue_create();

  component_in->input[0]->buffer_size = component_in->input[0]->buffer_size_recommended;
  if (component_in->input[0]->buffer_size < component_in->input[0]->buffer_size_min)
    component_in->input[0]->buffer_size = component_in->input[0]->buffer_size_min;
  if (use_mjpeg_)
    component_in->input[0]->buffer_size = component_in->input[0]->buffer_size_recommended * 8;
  component_in->input[0]->buffer_num = 1;
  component_in->input[0]->userdata = (MMAL_PORT_USERDATA_T *)this;

  RTC_LOG(LS_ERROR) << __FUNCTION__ << " input buffer_size:" << component_in->input[0]->buffer_size;

  if (mmal_port_enable(component_in->input[0], MMALInputCallbackFunction) != MMAL_SUCCESS)
  {
    RTC_LOG(LS_ERROR) << "Failed to enable input port";
    return WEBRTC_VIDEO_CODEC_ERROR;
  }
  pool_in_ = mmal_port_pool_create(component_in->input[0], component_in->input[0]->buffer_num, component_in->input[0]->buffer_size);

  if (use_mjpeg_)
  {
    if (mmal_connection_create(&conn1_, decoder_->output[0], resizer_->input[0], MMAL_CONNECTION_FLAG_TUNNELLING | MMAL_CONNECTION_FLAG_ALLOCATION_ON_INPUT) != MMAL_SUCCESS)
    {
      RTC_LOG(LS_ERROR) << "Failed to connect decoder to resizer";
      return WEBRTC_VIDEO_CODEC_ERROR;
    }

    if (mmal_connection_create(&conn2_, resizer_->output[0], encoder_->input[0], MMAL_CONNECTION_FLAG_TUNNELLING) != MMAL_SUCCESS)
    {
      RTC_LOG(LS_ERROR) << "Failed to connect resizer to encoder";
      return WEBRTC_VIDEO_CODEC_ERROR;
    }

    if (mmal_component_enable(resizer_) != MMAL_SUCCESS)
    {
      RTC_LOG(LS_ERROR) << "Failed to enable component";
      return WEBRTC_VIDEO_CODEC_ERROR;
    }

    if (mmal_component_enable(decoder_) != MMAL_SUCCESS)
    {
      RTC_LOG(LS_ERROR) << "Failed to enable component";
      return WEBRTC_VIDEO_CODEC_ERROR;
    }

    if (mmal_connection_enable(conn1_) != MMAL_SUCCESS)
    {
      RTC_LOG(LS_ERROR) << "Failed to enable connection decoder to resizer";
      return WEBRTC_VIDEO_CODEC_ERROR;
    }

    if (mmal_connection_enable(conn2_) != MMAL_SUCCESS)
    {
      RTC_LOG(LS_ERROR) << "Failed to enable connection resizer to encoder";
      return WEBRTC_VIDEO_CODEC_ERROR;
    }
  }

  encoder_->output[0]->buffer_size = encoder_->output[0]->buffer_size_recommended * 4;
  if (encoder_->output[0]->buffer_size < encoder_->output[0]->buffer_size_min)
    encoder_->output[0]->buffer_size = encoder_->output[0]->buffer_size_min;
  encoder_->output[0]->buffer_num = 4;
  encoder_->output[0]->userdata = (MMAL_PORT_USERDATA_T *)this;

  encoded_image_buffer_.reset(new uint8_t[encoder_->output[0]->buffer_size]);

  if (mmal_port_enable(encoder_->output[0], MMALOutputCallbackFunction) != MMAL_SUCCESS)
  {
    RTC_LOG(LS_ERROR) << "Failed to enable output port";
    return WEBRTC_VIDEO_CODEC_ERROR;
  }
  pool_out_ = mmal_port_pool_create(encoder_->output[0], encoder_->output[0]->buffer_num, encoder_->output[0]->buffer_size);

  if (mmal_component_enable(encoder_) != MMAL_SUCCESS)
  {
    RTC_LOG(LS_ERROR) << "Failed to enable component";
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  configured_framerate_ = framerate_;
  configured_width_ = width_;
  configured_height_ = height_;
  stride_width_ = stride_width;
  stride_height_ = stride_height;

  RTC_LOG(LS_ERROR) << __FUNCTION__ << " End";
  return WEBRTC_VIDEO_CODEC_OK;
}

void MMALH264Encoder::MMALRelease()
{
  RTC_LOG(LS_ERROR) << __FUNCTION__ << " Start";
  if (!encoder_)
    return;
  if (decoder_ && decoder_->input[0]->is_enabled)
  {
    if (mmal_port_disable(decoder_->input[0]) != MMAL_SUCCESS)
    {
      RTC_LOG(LS_ERROR) << "Failed to disable input port";
    }
    mmal_port_flush(decoder_->input[0]);
    mmal_port_flush(decoder_->output[0]);
    mmal_port_flush(decoder_->control); 
    mmal_port_flush(resizer_->input[0]);
    mmal_port_flush(resizer_->output[0]);
    mmal_port_flush(resizer_->control); 
  }
  if (encoder_->input[0]->is_enabled)
  {
    if (mmal_port_disable(encoder_->input[0]) != MMAL_SUCCESS)
    {
      RTC_LOG(LS_ERROR) << "Failed to disable input port";
    }
    mmal_port_flush(encoder_->input[0]);
    mmal_port_flush(encoder_->control); 
  }
  if (encoder_->output[0]->is_enabled)
  {
    if (mmal_port_disable(encoder_->output[0]) != MMAL_SUCCESS)
    {
      RTC_LOG(LS_ERROR) << "Failed to disable output port";
    }
    mmal_port_flush(encoder_->output[0]);
  }
  if (conn1_)
  {
    RTC_LOG(LS_ERROR) << "mmal_connection_disable Start";
    mmal_connection_disable(conn1_);
    mmal_connection_destroy(conn1_);
    conn1_ = nullptr;
    RTC_LOG(LS_ERROR) << "mmal_connection_disable End";
  }
  if (conn2_)
  {
    RTC_LOG(LS_ERROR) << "mmal_connection_disable Start";
    mmal_connection_disable(conn2_);
    mmal_connection_destroy(conn2_);
    conn2_ = nullptr;
    RTC_LOG(LS_ERROR) << "mmal_connection_disable End";
  }
  if (encoder_)
  {
    mmal_component_destroy(encoder_);
    encoder_ = nullptr;
  }
  if (decoder_)
  {
    mmal_component_destroy(resizer_);
    resizer_ = nullptr;
    mmal_component_destroy(decoder_);
    decoder_ = nullptr;
  }
  if (queue_ != nullptr)
  {
    MMAL_BUFFER_HEADER_T *buffer;
    while ((buffer = mmal_queue_get(queue_)) != nullptr)
    {
      mmal_buffer_header_release(buffer);
    }
    if (pool_in_ != nullptr)
    {
      mmal_pool_destroy(pool_in_);
      pool_in_ = nullptr;
    }
    if (pool_out_ != nullptr)
    {
      mmal_pool_destroy(pool_out_);
      pool_out_ = nullptr;
    }
    queue_ = nullptr;
  }
  RTC_LOG(LS_ERROR) << __FUNCTION__ << " End";
}

void MMALH264Encoder::MMALInputCallbackFunction(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer)
{
  ((MMALH264Encoder *)port->userdata)->MMALInputCallback(port, buffer);
}

void MMALH264Encoder::MMALInputCallback(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer)
{
  mmal_buffer_header_release(buffer);
}

void MMALH264Encoder::MMALOutputCallbackFunction(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer)
{
  ((MMALH264Encoder *)port->userdata)->MMALOutputCallback(port, buffer);
}

void MMALH264Encoder::MMALOutputCallback(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer)
{
  if (buffer->length == 0) {
    mmal_buffer_header_release(buffer);
    return;
  }

  if (buffer->flags & MMAL_BUFFER_HEADER_FLAG_CONFIG) {
    memcpy(encoded_image_buffer_.get(), buffer->data, buffer->length);
    encoded_buffer_length_ = buffer->length;
    mmal_buffer_header_release(buffer);
    RTC_LOG(LS_INFO) << "MMAL_BUFFER_HEADER_FLAG_CONFIG";
    return;
  }

  RTC_LOG(LS_INFO) << "pts:" << buffer->pts
                   << " flags:" << buffer->flags
                   << " planes:" << buffer->type->video.planes
                   << " length:" << buffer->length;

  std::unique_ptr<FrameParams> params;
  {
    rtc::CritScope lock(&frame_params_lock_);
    do {
      if (frame_params_.empty())
      {
        RTC_LOG(LS_WARNING) << __FUNCTION__ 
                            << "Frame parameter is not found. SkipFrame pts:"
                            << buffer->pts;
        return;
      }
      params = std::move(frame_params_.front());
      frame_params_.pop();
    } while (params->timestamp < buffer->pts);
    if (params->timestamp != buffer->pts)
    {
      RTC_LOG(LS_WARNING) << __FUNCTION__ 
                          << "Frame parameter is not found. SkipFrame pts:"
                          << buffer->pts;
      return;
    }
  }
  
  encoded_image_._encodedWidth = params->width;
  encoded_image_._encodedHeight = params->height;
  encoded_image_.capture_time_ms_ = params->render_time_ms;
  encoded_image_.ntp_time_ms_ = params->ntp_time_ms;
  encoded_image_.SetTimestamp(buffer->pts);
  encoded_image_.rotation_ = params->rotation;
  encoded_image_.SetColorSpace(params->color_space);

  if (encoded_buffer_length_ == 0)
  {
    SendFrame(buffer->data, buffer->length);
  }
  else
  {
    memcpy(encoded_image_buffer_.get() + encoded_buffer_length_, buffer->data, buffer->length);
    encoded_buffer_length_ += buffer->length;
    SendFrame(encoded_image_buffer_.get(), encoded_buffer_length_);
    encoded_buffer_length_ = 0;
  }
  
  mmal_buffer_header_release(buffer);
}

int32_t MMALH264Encoder::RegisterEncodeCompleteCallback(
    webrtc::EncodedImageCallback *callback)
{
  callback_ = callback;
  return WEBRTC_VIDEO_CODEC_OK;
}

void MMALH264Encoder::SetRates(const RateControlParameters &parameters)
{
  if (encoder_ == nullptr)
    return;
  if (parameters.bitrate.get_sum_bps() <= 0 || parameters.framerate_fps <= 0)
    return;

  RTC_LOG(LS_INFO) << __FUNCTION__ 
                   << " framerate:" << parameters.framerate_fps
                   << " bitrate:" << parameters.bitrate.get_sum_bps();
  framerate_ = parameters.framerate_fps;
  if (framerate_ > 30)
  {
    framerate_ = 30;
  }
  target_bitrate_bps_ = parameters.bitrate.get_sum_bps();
  bitrate_adjuster_.SetTargetBitrateBps(target_bitrate_bps_);
  return;
}

void MMALH264Encoder::SetBitrateBps(uint32_t bitrate_bps)
{
  if (bitrate_bps < 300000 || configured_bitrate_bps_ == bitrate_bps)
  {
    return;
  }
  RTC_LOG(LS_INFO) << "SetBitrateBps " << bitrate_bps << "bit/sec";
  if (mmal_port_parameter_set_uint32(encoder_->output[0], MMAL_PARAMETER_VIDEO_BIT_RATE, bitrate_bps) != MMAL_SUCCESS)
  {
    RTC_LOG(LS_ERROR) << "Failed to set bitrate";
    return;
  }
  configured_bitrate_bps_ = bitrate_bps;
}

webrtc::VideoEncoder::EncoderInfo MMALH264Encoder::GetEncoderInfo() const
{
  EncoderInfo info;
  info.supports_native_handle = true;
  info.implementation_name = "MMAL H264";
  info.scaling_settings =
      VideoEncoder::ScalingSettings(kLowH264QpThreshold, kHighH264QpThreshold);
  info.is_hardware_accelerated = true;
  info.has_internal_source = false;
  return info;
}

int32_t MMALH264Encoder::Encode(
    const webrtc::VideoFrame &input_frame,
    const std::vector<webrtc::VideoFrameType> *frame_types)
{
  if (!callback_)
  {
    RTC_LOG(LS_WARNING) << "InitEncode() has been called, but a callback function "
                        << "has not been set with RegisterEncodeCompleteCallback()";
    return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
  }

  rtc::scoped_refptr<webrtc::VideoFrameBuffer> frame_buffer = input_frame.video_frame_buffer();
  if (frame_buffer->type() == webrtc::VideoFrameBuffer::Type::kNative)
  {
    use_mjpeg_ = true;
  }
  else
  {
    use_mjpeg_ = false;
  }
  

  if (frame_buffer->width() != configured_width_ ||
      frame_buffer->height() != configured_height_ ||
      framerate_ != configured_framerate_)
  {
    RTC_LOG(LS_INFO) << "Encoder reinitialized from " << configured_width_
                     << "x" << configured_height_ << " to "
                     << frame_buffer->width() << "x" << frame_buffer->height()
                     << " framerate:" << framerate_;
    MMALRelease();
    if (use_mjpeg_)
    {
      NativeBuffer* native_buffer = dynamic_cast<NativeBuffer*>(frame_buffer.get());
      raw_width_ = native_buffer->raw_width();
      raw_height_ = native_buffer->raw_height();
    }
    if (MMALConfigure() != WEBRTC_VIDEO_CODEC_OK)
    {
      RTC_LOG(LS_ERROR) << "Failed to MMALConfigure";
      return WEBRTC_VIDEO_CODEC_ERROR;
    }
  }

  bool force_key_frame = false;
  if (frame_types != nullptr)
  {
    RTC_DCHECK_EQ(frame_types->size(), static_cast<size_t>(1));
    if ((*frame_types)[0] == webrtc::VideoFrameType::kEmptyFrame)
    {
      return WEBRTC_VIDEO_CODEC_OK;
    }
    force_key_frame = (*frame_types)[0] == webrtc::VideoFrameType::kVideoFrameKey;
  }

  if (force_key_frame)
  {
    if (mmal_port_parameter_set_boolean(encoder_->output[0], MMAL_PARAMETER_VIDEO_REQUEST_I_FRAME, MMAL_TRUE) != MMAL_SUCCESS)
    {
      RTC_LOG(LS_ERROR) << "Failed to request I frame";
    }
  }

  SetBitrateBps(bitrate_adjuster_.GetAdjustedBitrateBps());
  {
    rtc::CritScope lock(&frame_params_lock_);
    frame_params_.push(
      absl::make_unique<FrameParams>(frame_buffer->width(),
                                    frame_buffer->height(),
                                    input_frame.render_time_ms(),
                                    input_frame.ntp_time_ms(),
                                    input_frame.timestamp(),
                                    input_frame.rotation(),
                                    input_frame.color_space()));
  }

  MMAL_BUFFER_HEADER_T *buffer;
  while ((buffer = mmal_queue_get(pool_out_->queue)) != nullptr)
  {
    if (mmal_port_send_buffer(encoder_->output[0], buffer) != MMAL_SUCCESS)
    {
      RTC_LOG(LS_ERROR) << "Failed to send output buffer";
      return WEBRTC_VIDEO_CODEC_ERROR;
    }
  }

  while ((buffer = mmal_queue_get(pool_in_->queue)) != nullptr)
  {
    buffer->pts = buffer->dts = input_frame.timestamp();
    buffer->offset = 0;
    buffer->flags = MMAL_BUFFER_HEADER_FLAG_FRAME;
    if (use_mjpeg_)
    {
      NativeBuffer* native_buffer = dynamic_cast<NativeBuffer*>(frame_buffer.get());
      RTC_LOG(LS_ERROR) << __FUNCTION__ << " Set NativeBuffer Start length:" << native_buffer->length();
      memcpy(buffer->data,
             native_buffer->Data(),
             native_buffer->length());
      buffer->length = buffer->alloc_size = native_buffer->length();
      if (mmal_port_send_buffer(decoder_->input[0], buffer) != MMAL_SUCCESS)
      {
        RTC_LOG(LS_ERROR) << "Failed to send input native buffer";
        return WEBRTC_VIDEO_CODEC_ERROR;
      }
      RTC_LOG(LS_ERROR) << __FUNCTION__ << " Set NativeBuffer End";
    }
    else
    {
      rtc::scoped_refptr<const webrtc::I420BufferInterface> i420_buffer = frame_buffer->ToI420();
      size_t offset = 0;
      for (size_t i = 0; i < i420_buffer->height(); i++)
      {
        memcpy(buffer->data + offset,
               i420_buffer->DataY() + (i420_buffer->width() * i),
               i420_buffer->width());
        offset += stride_width_;
      }
      offset = 0;
      size_t offset_y = stride_width_ * stride_height_;
      size_t width_uv = stride_width_ / 2;
      size_t offset_v = (stride_height_ / 2) * width_uv;
      for (size_t i = 0; i < ((i420_buffer->height() + 1) / 2); i++)
      {
        memcpy(buffer->data + offset_y + offset,
               i420_buffer->DataU() + (i420_buffer->StrideU() * i),
               width_uv);
        memcpy(buffer->data + offset_y + offset_v + offset,
               i420_buffer->DataV() + (i420_buffer->StrideV() * i),
               width_uv);
        offset += width_uv;
      }
      buffer->length = buffer->alloc_size = stride_width_ * stride_height_ * 3 / 2;
      if (mmal_port_send_buffer(encoder_->input[0], buffer) != MMAL_SUCCESS)
      {
        RTC_LOG(LS_ERROR) << "Failed to send input i420 buffer";
        return WEBRTC_VIDEO_CODEC_ERROR;
      }
    }
  }

  return WEBRTC_VIDEO_CODEC_OK;
}

int32_t MMALH264Encoder::SendFrame(unsigned char *buffer, size_t size)
{
  encoded_image_.set_buffer(buffer, size);
  encoded_image_.set_size(size);
  encoded_image_._frameType = webrtc::VideoFrameType::kVideoFrameDelta;

  uint8_t zero_count = 0;
  size_t nal_start_idx = 0;
  std::vector<nal_entry> nals;
  for (size_t i = 0; i < size; i++)
  {
    uint8_t data = buffer[i];
    if ((i != 0) && (i == nal_start_idx))
    {
      if ((data & 0x1F) == 0x05)
      {
        encoded_image_._frameType = webrtc::VideoFrameType::kVideoFrameKey;
      }
    }
    if (data == 0x01 && zero_count == 3)
    {
      if (nal_start_idx != 0)
      {
        nals.push_back({nal_start_idx, i - nal_start_idx + 1 - 4});
      }
      nal_start_idx = i + 1;
    }
    if (data == 0x00)
    {
      zero_count++;
    }
    else
    {
      zero_count = 0;
    }
  }
  if (nal_start_idx != 0)
  {
    nals.push_back({nal_start_idx, size - nal_start_idx});
  }

  webrtc::RTPFragmentationHeader frag_header;
  frag_header.VerifyAndAllocateFragmentationHeader(nals.size());
  for (size_t i = 0; i < nals.size(); i++)
  {
    frag_header.fragmentationOffset[i] = nals[i].offset;
    frag_header.fragmentationLength[i] = nals[i].size;
    frag_header.fragmentationPlType[i] = 0;
    frag_header.fragmentationTimeDiff[i] = 0;
  }

  webrtc::CodecSpecificInfo codec_specific;
  codec_specific.codecType = webrtc::kVideoCodecH264;
  codec_specific.codecSpecific.H264.packetization_mode = webrtc::H264PacketizationMode::NonInterleaved;

  h264_bitstream_parser_.ParseBitstream(buffer, size);
  h264_bitstream_parser_.GetLastSliceQp(&encoded_image_.qp_);
  RTC_LOG(LS_ERROR) << __FUNCTION__ 
                    << " last slice qp:" << encoded_image_.qp_;

  webrtc::EncodedImageCallback::Result result = callback_->OnEncodedImage(encoded_image_, &codec_specific, &frag_header);
  if (result.error != webrtc::EncodedImageCallback::Result::OK)
  {
    RTC_LOG(LS_ERROR) << __FUNCTION__ 
                      << " OnEncodedImage failed error:" << result.error;
    return WEBRTC_VIDEO_CODEC_ERROR;
  }
  bitrate_adjuster_.Update(size);
  return WEBRTC_VIDEO_CODEC_OK;
}