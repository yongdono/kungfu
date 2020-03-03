//
// Created by Keren Dong on 2020/1/14.
//

#include "io.h"
#include "watcher.h"

using namespace kungfu::rx;
using namespace kungfu::longfist;
using namespace kungfu::longfist::enums;
using namespace kungfu::longfist::types;
using namespace kungfu::longfist::sqlite;
using namespace kungfu::yijinjing;
using namespace kungfu::yijinjing::data;

namespace kungfu::node
{
    Napi::FunctionReference Watcher::constructor;

    inline location_ptr GetWatcherLocation(const Napi::CallbackInfo &info)
    {
        return std::make_shared<location>(
                mode::LIVE, category::SYSTEM, "node", info[1].As<Napi::String>().Utf8Value(), IODevice::GetLocator(info)
        );
    }

    inline void InitStateMap(const Napi::CallbackInfo &info, Napi::ObjectReference &ref)
    {
        boost::hana::for_each(StateDataTypes, [&](auto it)
        {
            using DataType = typename decltype(+boost::hana::second(it))::type;
            auto name = std::string(boost::hana::first(it).c_str());
            ref.Set(name, Napi::Object::New(info.Env()));
        });
    }

    Watcher::Watcher(const Napi::CallbackInfo &info) :
            ObjectWrap(info),
            apprentice(GetWatcherLocation(info), true),
            ledger_location_(mode::LIVE, category::SYSTEM, "service", "ledger", get_io_device()->get_home()->locator),
            config_ref_(Napi::ObjectReference::New(ConfigStore::NewInstance({info[0]}).ToObject(), 1)),
            state_ref_(Napi::ObjectReference::New(Napi::Object::New(info.Env()), 1)),
            ledger_ref_(Napi::ObjectReference::New(Napi::Object::New(info.Env()), 1)),
            update_state(state_ref_),
            update_ledger(ledger_ref_),
            publish(*this, state_ref_)
    {
        InitStateMap(info, state_ref_);
        InitStateMap(info, ledger_ref_);
        SPDLOG_INFO("watcher created at {}", get_io_device()->get_home()->uname);

        auto locator = get_io_device()->get_home()->locator;
        for (const auto &pair : ConfigStore::Unwrap(config_ref_.Value())->cs_.get_all(Config{}))
        {
            RestoreState(location::make_shared(pair.second, locator));
        }
        RestoreState(location::make_shared(mode::LIVE, category::SYSTEM, "service", "ledger", locator));
        SPDLOG_INFO("watcher ledger restored");
    }

    Watcher::~Watcher()
    {
        ledger_ref_.Unref();
        state_ref_.Unref();
        config_ref_.Unref();
    }

    void Watcher::NoSet(const Napi::CallbackInfo &info, const Napi::Value &value)
    {}

    Napi::Value Watcher::GetLocation(const Napi::CallbackInfo &info)
    {
        auto uid = info[0].ToNumber().Uint32Value();
        if (not has_location(uid))
        {
            return Napi::Value();
        }
        auto location = get_location(uid);
        auto locationObj = Napi::Object::New(info.Env());
        locationObj.Set("category", Napi::String::New(info.Env(), get_category_name(location->category)));
        locationObj.Set("group", Napi::String::New(info.Env(), location->group));
        locationObj.Set("name", Napi::String::New(info.Env(), location->name));
        locationObj.Set("mode", Napi::String::New(info.Env(), get_mode_name(location->mode)));
        return locationObj;
    }

    Napi::Value Watcher::GetConfig(const Napi::CallbackInfo &info)
    {
        return config_ref_.Value();
    }

    Napi::Value Watcher::IsUsable(const Napi::CallbackInfo &info)
    {
        return Napi::Boolean::New(info.Env(), is_usable());
    }

    Napi::Value Watcher::IsLive(const Napi::CallbackInfo &info)
    {
        return Napi::Boolean::New(info.Env(), is_live());
    }

    Napi::Value Watcher::IsStarted(const Napi::CallbackInfo &info)
    {
        return Napi::Boolean::New(info.Env(), is_started());
    }

    Napi::Value Watcher::Setup(const Napi::CallbackInfo &info)
    {
        setup();
        return Napi::Value();
    }

    Napi::Value Watcher::Step(const Napi::CallbackInfo &info)
    {
        step();
        return Napi::Value();
    }

    Napi::Value Watcher::GetState(const Napi::CallbackInfo &info)
    {
        return state_ref_.Value();
    }

    void Watcher::SetState(const Napi::CallbackInfo &info, const Napi::Value &value)
    {}

    Napi::Value Watcher::GetLedger(const Napi::CallbackInfo &info)
    {
        return ledger_ref_.Value();
    }

    void Watcher::SetLedger(const Napi::CallbackInfo &info, const Napi::Value &value)
    {}

    Napi::Value Watcher::PublishState(const Napi::CallbackInfo &info)
    {
        Napi::Object obj = info[0].ToObject();
        longfist::cast_type_invoke(obj.Get("type").ToString().Utf8Value(), obj, publish);
        return Napi::Value();
    }

    void Watcher::Init(Napi::Env env, Napi::Object exports)
    {
        Napi::HandleScope scope(env);

        Napi::Function func = DefineClass(env, "Watcher", {
                InstanceMethod("isUsable", &Watcher::IsUsable),
                InstanceMethod("isLive", &Watcher::IsLive),
                InstanceMethod("isStarted", &Watcher::IsStarted),
                InstanceMethod("setup", &Watcher::Setup),
                InstanceMethod("step", &Watcher::Step),
                InstanceMethod("getLocation", &Watcher::GetLocation),
                InstanceMethod("publishState", &Watcher::PublishState),
                InstanceAccessor("config", &Watcher::GetConfig, &Watcher::NoSet),
                InstanceAccessor("state", &Watcher::GetState, &Watcher::NoSet),
                InstanceAccessor("ledger", &Watcher::GetLedger, &Watcher::NoSet),
        });

        constructor = Napi::Persistent(func);
        constructor.SuppressDestruct();

        exports.Set("Watcher", func);
    }

    void Watcher::register_location(int64_t trigger_time, const yijinjing::data::location_ptr &location)
    {
        apprentice::register_location(trigger_time, location);

        if (location->uid == ledger_location_.uid and has_writer(get_master_commands_uid()))
        {
            request_read_from_public(now(), ledger_location_.uid, get_master_start_time());
        }
    }

    void Watcher::react()
    {
        events_ |
        $([&](const event_ptr &event)
          {
              longfist::cast_event_invoke(event, event->source() == get_master_commands_uid() ? update_state : update_ledger);
          });

        events_ | is(Channel::tag) |
        $([&](const event_ptr &event)
          {
              const Channel &channel = event->data<Channel>();
              reader_->join(get_location(channel.source_id), channel.dest_id, event->gen_time());
          });

        apprentice::react();
    }

    void Watcher::RestoreState(location_ptr state_location)
    {
        register_location(0, state_location);
        auto storage = make_storage(state_location->locator->layout_file(state_location, layout::SQLITE, "state"), StateDataTypes);
        storage.sync_schema();
        serialize::JsRestoreState(ledger_ref_, state_location, storage)();
    }
}