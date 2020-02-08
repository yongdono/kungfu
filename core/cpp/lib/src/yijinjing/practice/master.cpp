//
// Created by Keren Dong on 2019-06-15.
//

#include <typeinfo>
#include <nlohmann/json.hpp>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <kungfu/longfist/longfist.h>
#include <kungfu/longfist/serialize/sql.h>
#include <kungfu/yijinjing/time.h>
#include <kungfu/yijinjing/practice/master.h>

using namespace kungfu::rx;
using namespace kungfu::longfist::types;
using namespace kungfu::longfist::sqlite;
using namespace kungfu::yijinjing;
using namespace kungfu::yijinjing::data;

namespace kungfu::yijinjing::practice
{

    master::master(location_ptr home, bool low_latency) : hero(std::make_shared<io_device_master>(home, low_latency)), last_check_(0)
    {
        auto io_device = std::dynamic_pointer_cast<io_device_master>(get_io_device());
        auto now = time::now_in_nano();
        io_device->open_session(io_device->get_home(), now);
        writers_[0] = io_device->open_writer(0);
        writers_[0]->mark(now, SessionStart::tag);
    }

    void master::on_exit()
    {
        auto io_device = std::dynamic_pointer_cast<io_device_master>(get_io_device());
        auto now = time::now_in_nano();
        io_device->close_session(io_device->get_home(), now);
        writers_[0]->mark(now, SessionEnd::tag);
    }

    void master::on_notify()
    {
        get_io_device()->get_publisher()->notify();
    }

    void master::register_app(const event_ptr &e)
    {
        auto io_device = std::dynamic_pointer_cast<io_device_master>(get_io_device());
        auto home = io_device->get_home();

        auto request_loc = e->data<nlohmann::json>();
        auto app_location = std::make_shared<location>(
                static_cast<mode>(request_loc["mode"]),
                static_cast<category>(request_loc["category"]),
                request_loc["group"], request_loc["name"],
                home->locator
        );

        if (has_location(app_location->uid))
        {
            SPDLOG_ERROR("location {} has already been registered", app_location->uname);
            return;
        }

        auto now = time::now_in_nano();
        auto uid_str = fmt::format("{:08x}", app_location->uid);

        if (app_locations_.find(app_location->uid) == app_locations_.end())
        {
            auto master_location = std::make_shared<location>(mode::LIVE, category::SYSTEM, "master", uid_str, home->locator);
            register_location(e->gen_time(), master_location);
            app_locations_[app_location->uid] = master_location->uid;
        }

        register_location(e->gen_time(), app_location);

        auto master_location = get_location(app_locations_[app_location->uid]);
        writers_[app_location->uid] = get_io_device()->open_writer_at(master_location, app_location->uid);

        reader_->join(app_location, 0, now);
        reader_->join(app_location, master_location->uid, now);

        auto &writer = writers_[app_location->uid];

        {
            auto msg = request_loc.dump();
            auto frame = writers_[0]->open_frame(e->gen_time(), Register::tag, msg.length());
            memcpy(reinterpret_cast<void *>(frame->address() + frame->header_length()), msg.c_str(), msg.length());
            writers_[0]->close_frame(msg.length());
        }

        io_device->open_session(app_location, e->gen_time());
        writer->mark(e->gen_time(), SessionStart::tag);

        require_write_to(app_location->uid, e->gen_time(), 0);
        require_write_to(app_location->uid, e->gen_time(), master_location->uid);

        for (const auto &item : locations_)
        {
            nlohmann::json location;
            location["mode"] = item.second->mode;
            location["category"] = item.second->category;
            location["group"] = item.second->group;
            location["name"] = item.second->name;
            auto msg = location.dump();
            SPDLOG_DEBUG("adding location {}", msg);
            auto &&frame = writer->open_frame(e->gen_time(), Location::tag, msg.length());
            memcpy(reinterpret_cast<void *>(frame->address() + frame->header_length()), msg.c_str(), msg.length());
            writer->close_frame(msg.length());
        }

        for (const auto &item: channels_)
        {
            writer->write(e->gen_time(), item.second);
        }

        on_register(e, app_location);

        auto state_db_file = home->locator->layout_file(app_location, layout::SQLITE, "state");
        app_sqlizers_[app_location->uid] = std::make_shared<sqlizer>(state_db_file);
        app_sqlizers_[app_location->uid]->storage.sync_schema();
        app_sqlizers_[app_location->uid]->restore(writer);

        writer->mark(e->gen_time(), RequestStart::tag);
    }

    void master::deregister_app(int64_t trigger_time, uint32_t app_location_uid)
    {
        auto io_device = std::dynamic_pointer_cast<io_device_master>(get_io_device());
        auto location = get_location(app_location_uid);
        nlohmann::json location_desc{};
        location_desc["mode"] = location->mode;
        location_desc["category"] = location->category;
        location_desc["group"] = location->group;
        location_desc["name"] = location->name;
        location_desc["uname"] = location->uname;
        location_desc["uid"] = app_location_uid;

        writers_[app_location_uid]->mark(trigger_time, SessionEnd::tag);
        io_device->close_session(location, trigger_time);

        deregister_channel_by_source(app_location_uid);

        deregister_location(trigger_time, app_location_uid);
        reader_->disjoin(app_location_uid);
        writers_.erase(app_location_uid);
        timer_tasks_.erase(app_location_uid);

        auto msg = location_desc.dump();
        auto &&frame = writers_[0]->open_frame(trigger_time, Deregister::tag, msg.length());
        memcpy(reinterpret_cast<void *>(frame->address() + frame->header_length()), msg.c_str(), msg.length());
        writers_[0]->close_frame(msg.length());

        app_sqlizers_.erase(app_location_uid);
    }

    void master::publish_time(int32_t msg_type, int64_t nanotime)
    {
        writers_[0]->write(0, msg_type, nanotime);
    }

    void master::send_time(uint32_t dest, int32_t msg_type, int64_t nanotime)
    {
        if (has_location(dest))
        {
            writers_[dest]->write(0, msg_type, nanotime);
        } else
        {
            SPDLOG_ERROR("Can not send time to {:08x}", dest);
        }
    }

    void master::register_channel(int64_t trigger_time, const Channel &channel)
    {
        hero::register_channel(trigger_time, channel);
        writers_[0]->write(trigger_time, channel);
    }

    bool master::produce_one(const rx::subscriber<event_ptr> &sb)
    {
        auto now = time::now_in_nano();

        for (auto &app : timer_tasks_)
        {
            uint32_t app_id = app.first;
            auto &app_tasks = app.second;
            for (auto it = app_tasks.begin(); it != app_tasks.end();)
            {
                auto &task = it->second;
                if (task.checkpoint <= now)
                {
                    writers_[app_id]->mark(0, Time::tag);
                    SPDLOG_DEBUG("sent time event to {}", get_location(app_id)->uname);
                    task.checkpoint += task.duration;
                    task.repeat_count++;
                    if (task.repeat_count >= task.repeat_limit)
                    {
                        it = app_tasks.erase(it);
                        continue;
                    }
                }
                it++;
            }
        }

        if (last_check_ + time_unit::NANOSECONDS_PER_SECOND < now)
        {
            on_interval_check(now);
            last_check_ = now;
        }

        return hero::produce_one(sb);
    }

    void master::react()
    {
        events_ |
        filter([&](const event_ptr &event)
               {
                   return dynamic_cast<journal::frame *>(event.get()) != nullptr;
               }) |
        $([&](const event_ptr &e)
          {
              auto io_device = std::dynamic_pointer_cast<io_device_master>(get_io_device());
              io_device->update_session(std::dynamic_pointer_cast<journal::frame>(e));
          });

        events_ | is(Register::tag) |
        $([&](const event_ptr &e)
          {
              register_app(e);
          });

        events_ | is(RequestWriteTo::tag) |
        $([&](const event_ptr &e)
          {
              const RequestWriteTo &request = e->data<RequestWriteTo>();
              if (has_location(request.dest_id))
              {
                  reader_->join(get_location(e->source()), request.dest_id, e->gen_time());
                  require_write_to(e->source(), e->gen_time(), request.dest_id);
                  require_read_from(request.dest_id, e->gen_time(), e->source(), false);
                  Channel channel = {};
                  channel.source_id = e->source();
                  channel.dest_id = request.dest_id;
                  register_channel(e->gen_time(), channel);
              } else
              {
                  SPDLOG_ERROR("Request write to unknown location {:08x}", request.dest_id);
              }
          });

        events_ | is(RequestReadFrom::tag) |
        $([&](const event_ptr &e)
          {
              const RequestReadFrom &request = e->data<RequestReadFrom>();
              if (has_location(request.source_id))
              {
                  reader_->join(get_location(request.source_id), e->source(), e->gen_time());
                  require_write_to(request.source_id, e->gen_time(), e->source());
                  require_read_from(e->source(), e->gen_time(), request.source_id, false);
                  Channel channel = {};
                  channel.source_id = request.source_id;
                  channel.dest_id = e->source();
                  register_channel(e->gen_time(), channel);
              } else
              {
                  SPDLOG_ERROR("Request read from unknown location {:08x}", request.source_id);
              }
          });

        events_ | is(RequestReadFromPublic::tag) |
        $([&](const event_ptr &e)
          {
              const RequestReadFrom &request = e->data<RequestReadFrom>();
              if (has_location(request.source_id))
              {
                  require_read_from(e->source(), e->gen_time(), request.source_id, true);
              } else
              {
                  SPDLOG_ERROR("Request read public from unknown location {:08x}", request.source_id);
              }
          });

        events_ | is(TimeRequest::tag) |
        $([&](const event_ptr &e)
          {
              const TimeRequest &request = e->data<TimeRequest>();
              if (timer_tasks_.find(e->source()) == timer_tasks_.end())
              {
                  timer_tasks_[e->source()] = std::unordered_map<int32_t, timer_task>();
              }
              std::unordered_map<int32_t, timer_task> &app_tasks = timer_tasks_[e->source()];
              if (app_tasks.find(request.id) == app_tasks.end())
              {
                  app_tasks[request.id] = timer_task();
              }
              timer_task &task = app_tasks[request.id];
              task.checkpoint = time::now_in_nano() + request.duration;
              task.duration = request.duration;
              task.repeat_count = 0;
              task.repeat_limit = request.repeat;
              SPDLOG_DEBUG("time request from {} duration {} repeat {}", get_location(e->source())->uname, request.duration, request.repeat);
          });

        events_ | to(0) |
        $([&](const event_ptr &e)
          {
              longfist::cast_invoke(e, *app_sqlizers_[e->source()]);
          });
    }
}