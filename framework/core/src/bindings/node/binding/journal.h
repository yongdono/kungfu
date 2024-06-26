// SPDX-License-Identifier: Apache-2.0

//
// Created by Keren Dong on 2020/1/1.
//

#ifndef KUNGFU_NODE_JOURNAL_H
#define KUNGFU_NODE_JOURNAL_H

#include "common.h"

#include <kungfu/yijinjing/io.h>
#include <kungfu/yijinjing/journal/assemble.h>
#include <kungfu/yijinjing/journal/journal.h>

namespace kungfu::node {
class Reader;

class Frame : public Napi::ObjectWrap<Frame> {
public:
  explicit Frame(const Napi::CallbackInfo &info);

  Napi::Value DataLength(const Napi::CallbackInfo &info);

  Napi::Value GenTime(const Napi::CallbackInfo &info);

  Napi::Value TriggerTime(const Napi::CallbackInfo &info);

  Napi::Value MsgType(const Napi::CallbackInfo &info);

  Napi::Value Source(const Napi::CallbackInfo &info);

  Napi::Value Dest(const Napi::CallbackInfo &info);

  Napi::Value Data(const Napi::CallbackInfo &info);

  static void Init(Napi::Env env, Napi::Object exports);

  static Napi::Value NewInstance(Napi::Value arg);

private:
  yijinjing::journal::frame_ptr frame_;
  static Napi::FunctionReference constructor;

  void SetFrame(yijinjing::journal::frame_ptr frame);

  friend class Reader;
  friend class Assemble;
};

class Reader : public Napi::ObjectWrap<Reader>, public yijinjing::journal::reader {
public:
  explicit Reader(const Napi::CallbackInfo &info);

  Napi::Value ToString(const Napi::CallbackInfo &info);

  Napi::Value CurrentFrame(const Napi::CallbackInfo &info);

  Napi::Value SeekToTime(const Napi::CallbackInfo &info);

  Napi::Value DataAvailable(const Napi::CallbackInfo &info);

  Napi::Value Next(const Napi::CallbackInfo &info);

  Napi::Value Join(const Napi::CallbackInfo &info);

  Napi::Value Disjoin(const Napi::CallbackInfo &info);

  static void Init(Napi::Env env, Napi::Object exports);

  static Napi::Value NewInstance(Napi::Value arg);

private:
  yijinjing::io_device_ptr io_device_;
  static Napi::FunctionReference constructor;
};

class Assemble : public Napi::ObjectWrap<Assemble>, public yijinjing::journal::assemble {
public:
  explicit Assemble(const Napi::CallbackInfo &info);

  Napi::Value CurrentFrame(const Napi::CallbackInfo &info);

  Napi::Value SeekToTime(const Napi::CallbackInfo &info);

  Napi::Value DataAvailable(const Napi::CallbackInfo &info);

  Napi::Value Next(const Napi::CallbackInfo &info);

  static void Init(Napi::Env env, Napi::Object exports);

private:
  static Napi::FunctionReference constructor;

  static std::vector<yijinjing::data::locator_ptr> ExtractLocator(const Napi::CallbackInfo &info);
};
} // namespace kungfu::node
#endif // KUNGFU_NODE_JOURNAL_H
