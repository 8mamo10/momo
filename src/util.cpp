#include "rtc_base/helpers.h"
#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>
#include <boost/preprocessor/stringize.hpp>

#include "util.h"

// バージョン情報
// 通常は外から渡すが、渡されていなかった場合の対応
#ifndef MOMO_VERSION
#define MOMO_VERSION "internal-build"
#endif

// HWA を効かせる場合は 1 になる
#if USE_IL_ENCODER
  #define MOMO_USE_IL_ENCODER 1
#else
  #define MOMO_USE_IL_ENCODER 0
#endif

using json = nlohmann::json;

// 列挙した文字列のみを許可するバリデータ
struct Enum : public CLI::Validator
{
  Enum(std::vector<std::string> xs)
  {
    std::stringstream out;

    bool first = true;
    for (auto x : xs)
    {
      if (!first)
      {
        out << ",";
      }
      first = false;

      out << x;
    }
    std::string name = out.str();

    tname = "STR in [" + name + "]";
    func = [xs, name](std::string input) {
      auto it = std::find(std::begin(xs), std::end(xs), input);
      if (it == std::end(xs))
      {
        return "Value " + input + " not in range [" + name + "]";
      }
      return std::string();
    };
  }
};

// JSON Value のみを許可するバリデータ
struct JsonValue : public CLI::Validator
{
  JsonValue()
  {
    tname = "JSON Value";
    func = [](std::string input) {
      try
      {
        json::parse(input);
        return std::string();
      }
      catch (json::parse_error &e)
      {
        return "Value " + input + " is not JSON Value";
      }
    };
  }
};

void RTCUtil::parseArgs(int argc, char *argv[], bool &is_daemon,
                        bool &use_p2p, bool &use_sora, 
                        int &log_level, ConnectionSettings &cs)
{
  CLI::App app("Momo - WebRTC ネイティブクライアント");

  bool version = false;

  app.add_flag("--no-video", cs.no_video, "ビデオを表示しない");
  app.add_flag("--no-audio", cs.no_audio, "オーディオを出さない");
  app.add_option("--video-codec", cs.video_codec, "ビデオコーデック")->check(Enum({"VP8", "VP9", "H264"}));
  app.add_option("--audio-codec", cs.audio_codec, "オーディオコーデック")->check(Enum({"OPUS", "PCMU"}));
  app.add_option("--video-bitrate", cs.video_bitrate, "ビデオのビットレート")->check(CLI::Range(1, 30000));
  app.add_option("--audio-bitrate", cs.video_bitrate, "オーディオのビットレート")->check(CLI::Range(6, 510));
  app.add_option("--resolution", cs.resolution, "解像度")->check(Enum({"QVGA", "VGA", "HD", "FHD"}));
  app.add_option("--framerate", cs.framerate, "フレームレート")->check(CLI::Range(1, 60));
  app.add_option("--priority", cs.priority, "優先設定 (Experimental)")->check(Enum({"BALANCE", "FRAMERATE", "RESOLUTION"}));
  app.add_flag("--daemon", is_daemon, "デーモン化する");
  app.add_flag("--version", version, "バージョン情報の表示");
  app.add_option("--log-level", log_level, "ログレベル")->check(CLI::Range(0, 5));
  // 隠しオプション
  std::string metadata;
  app.add_option("--metadata", metadata, "メタデータ")->group("")->check(JsonValue());

  auto p2p_app = app.add_subcommand("p2p", "P2P");
  auto sora_app = app.add_subcommand("sora", "WebRTC SFU Sora");

  p2p_app->add_option("--port", cs.p2p_port, "ポート番号")->check(CLI::Range(0, 65535));

  sora_app->add_option("SIGNALING-URL", cs.sora_signaling_host, "シグナリングホスト")->required();
  sora_app->add_option("CHANNEL-ID", cs.sora_channel_id, "チャンネルID")->required();
  sora_app->add_flag("--auto", cs.sora_auto_connect, "自動接続する");

  try
  {
    app.parse(argc, argv);
  }
  catch (const CLI::ParseError &e)
  {
    exit(app.exit(e));
  }

  // メタデータのパース
  if (!metadata.empty())
  {
    cs.metadata = json::parse(metadata);
  }

  if (version)
  {
    std::cout << "WebRTC Native Client Momo version " MOMO_VERSION " USE_IL_ENCODER=" BOOST_PP_STRINGIZE(MOMO_USE_IL_ENCODER) << std::endl;
    exit(0);
  }

  if (!p2p_app->parsed() && !sora_app->parsed())
  {
    std::cout << app.help() << std::endl;
    exit(1);
  }

  if (sora_app->parsed()) {
    use_sora = true;
  }

  if (p2p_app->parsed()) {
    use_p2p = true;
  }
}

std::string RTCUtil::generateRundomChars()
{
  return generateRundomChars(32);
}

std::string RTCUtil::generateRundomChars(size_t length)
{
  std::string result;
  rtc::CreateRandomString(length, &result);
  return result;
}

std::string RTCUtil::iceConnectionStateToString(
        webrtc::PeerConnectionInterface::IceConnectionState state) {
  switch (state) {
    case webrtc::PeerConnectionInterface::kIceConnectionNew:
      return "new";
    case webrtc::PeerConnectionInterface::kIceConnectionChecking:
      return "checking";
    case webrtc::PeerConnectionInterface::kIceConnectionConnected:
      return "connected";
    case webrtc::PeerConnectionInterface::kIceConnectionCompleted:
      return "completed";
    case webrtc::PeerConnectionInterface::kIceConnectionFailed:
      return "failed";
    case webrtc::PeerConnectionInterface::kIceConnectionDisconnected:
      return "disconnected";
    case webrtc::PeerConnectionInterface::kIceConnectionClosed:
      return "closed";
    case webrtc::PeerConnectionInterface::kIceConnectionMax:
      return "max";
  }
  return "unknown";
}