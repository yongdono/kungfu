//
// Created by Keren Dong on 2019-06-15.
//

#include <kungfu/common.h>
#include <kungfu/longfist/longfist.h>
#include <kungfu/yijinjing/practice/master.h>
#include <kungfu/yijinjing/time.h>

using namespace kungfu::rx;
using namespace kungfu::longfist;
using namespace kungfu::longfist::types;
using namespace kungfu::yijinjing::cache;
using namespace kungfu::yijinjing::data;
using namespace kungfu::yijinjing::journal;

namespace kungfu::yijinjing::practice {

master::master(location_ptr home, bool low_latency)
    : hero(std::make_shared<io_device_master>(home, low_latency)), start_time_(time::now_in_nano()), last_check_(0),
      session_builder_(get_io_device()), profile_(get_locator()) {
  for (const auto &app_location : profile_.get_all(Location{})) {
    add_location(start_time_, location::make_shared(app_location, get_locator()));
  }
  for (const auto &config : profile_.get_all(Config{})) {
    try_add_location(start_time_, location::make_shared(config, get_locator()));
  }
  auto io_device = std::dynamic_pointer_cast<io_device_master>(get_io_device());
  writers_.emplace(location::PUBLIC, io_device->open_writer(0));
  get_writer(location::PUBLIC)->mark(start_time_, SessionStart::tag);
}

index::session_builder &master::get_session_builder() { return session_builder_; }

void master::on_exit() {
  auto io_device = std::dynamic_pointer_cast<io_device_master>(get_io_device());
  auto now = time::now_in_nano();
  get_writer(location::PUBLIC)->mark(now, SessionEnd::tag);
}

void master::on_notify() { get_io_device()->get_publisher()->notify(); }

void master::register_app(const event_ptr &event) {
  auto io_device = std::dynamic_pointer_cast<io_device_master>(get_io_device());
  auto home = io_device->get_home();

  auto request_json = event->data<nlohmann::json>();
  auto request_data = event->data_as_string();

  Register register_data(request_data.c_str(), request_data.length());

  auto app_location = location::make_shared(register_data, home->locator);

  if (is_location_live(app_location->uid)) {
    SPDLOG_ERROR("location {} has already been registered live", app_location->uname);
    return;
  }

  auto now = time::now_in_nano();
  auto uid_str = fmt::format("{:08x}", app_location->uid);
  auto master_cmd_location = location::make_shared(mode::LIVE, category::SYSTEM, "master", uid_str, home->locator);
  auto public_writer = get_writer(location::PUBLIC);
  auto app_cmd_writer = get_io_device()->open_writer_at(master_cmd_location, app_location->uid);

  try_add_location(event->gen_time(), app_location);
  try_add_location(event->gen_time(), master_cmd_location);
  app_cmd_locations_.emplace(app_location->uid, master_cmd_location->uid);

  register_data.last_active_time = session_builder_.find_last_active_time(app_location);
  register_location(event->gen_time(), register_data);

  writers_.emplace(app_location->uid, app_cmd_writer);
  reader_->join(app_location, location::PUBLIC, now);
  reader_->join(app_location, master_cmd_location->uid, now);

  session_builder_.open_session(app_location, event->gen_time());

  public_writer->write(event->gen_time(), *std::dynamic_pointer_cast<Location>(app_location));
  public_writer->write(event->gen_time(), register_data);

  require_write_to(event->gen_time(), app_location->uid, location::PUBLIC);
  require_write_to(event->gen_time(), app_location->uid, master_cmd_location->uid);

  app_cmd_writer->mark(event->gen_time(), SessionStart::tag);

  write_time_reset(event->gen_time(), app_cmd_writer);
  write_trading_day(event->gen_time(), app_cmd_writer);
  write_profile_data(event->gen_time(), app_cmd_writer);

  app_cache_shift_.emplace(app_location->uid, app_location);
  app_cache_shift_[app_location->uid] >> app_cmd_writer;

  app_cmd_writer->mark(start_time_, RequestStart::tag);

  write_registries(event->gen_time(), app_cmd_writer);
  write_channels(event->gen_time(), app_cmd_writer);

  on_register(event, register_data);
}

void master::deregister_app(int64_t trigger_time, uint32_t app_location_uid) {
  auto location = get_location(app_location_uid);
  SPDLOG_INFO("app {} gone", location->uname);
  session_builder_.close_session(location, trigger_time);
  get_writer(app_location_uid)->mark(trigger_time, SessionEnd::tag);
  deregister_channel(app_location_uid);
  deregister_location(trigger_time, app_location_uid);
  registry_.erase(app_location_uid);
  reader_->disjoin(app_location_uid);
  writers_.erase(app_location_uid);
  timer_tasks_.erase(app_location_uid);
  app_cache_shift_.erase(app_location_uid);
  get_writer(location::PUBLIC)->write(trigger_time, location->to<Deregister>());
}

void master::publish_trading_day() { write_trading_day(0, get_writer(location::PUBLIC)); }

void master::react() {
  events_ |
      instanceof <journal::frame>() | $([&](const event_ptr &event) {
                   if (registry_.find(event->source()) != registry_.end()) {
                     session_builder_.update_session(std::dynamic_pointer_cast<journal::frame>(event));
                     if (has_location(event->source()) and get_location(event->source())->category != category::MD) {
                       feed_state_data(event, app_cache_shift_[event->source()]);
                       feed_profile_data(event, profile_);
                     }
                   }
                 });

  events_ | is(Location::tag) | $([&](const event_ptr &event) {
    Location data = event->data<Location>();
    try_add_location(event->gen_time(), location::make_shared(data, get_locator()));
    get_writer(location::PUBLIC)->write(event->gen_time(), data);
  });

  events_ | is(Register::tag) | $$(register_app(event));

  events_ | is(RequestWriteTo::tag) | $([&](const event_ptr &event) {
    const RequestWriteTo &request = event->data<RequestWriteTo>();
    if (check_location_live(event->source(), request.dest_id)) {
      reader_->join(get_location(event->source()), request.dest_id, event->gen_time());
      require_write_to(event->gen_time(), event->source(), request.dest_id);
      require_read_from(0, request.dest_id, event->source(), event->gen_time());
      Channel channel = {};
      channel.source_id = event->source();
      channel.dest_id = request.dest_id;
      register_channel(event->gen_time(), channel);
      get_writer(location::PUBLIC)->write(event->gen_time(), channel);
    }
  });

  events_ | is(RequestReadFrom::tag) | $([&](const event_ptr &event) {
    const RequestReadFrom &request = event->data<RequestReadFrom>();
    if (check_location_live(request.source_id, event->source())) {
      reader_->join(get_location(request.source_id), event->source(), event->gen_time());
      require_write_to(event->gen_time(), request.source_id, event->source());
      require_read_from(event->gen_time(), event->source(), request.source_id, request.from_time);
      Channel channel = {};
      channel.source_id = request.source_id;
      channel.dest_id = event->source();
      register_channel(event->gen_time(), channel);
      get_writer(location::PUBLIC)->write(event->gen_time(), channel);
    }
  });

  events_ | is(RequestReadFromPublic::tag) | $([&](const event_ptr &event) {
    const RequestReadFromPublic &request = event->data<RequestReadFromPublic>();
    require_read_from_public(event->gen_time(), event->source(), request.source_id, request.from_time);
  });

  events_ | is(Channel::tag) | $([&](const event_ptr &event) {
    const Channel &request = event->data<Channel>();
    if (is_location_live(request.source_id) and not has_channel(request.source_id, request.dest_id)) {
      reader_->join(get_location(request.source_id), request.dest_id, event->gen_time());
      require_write_to(event->gen_time(), request.source_id, request.dest_id);
      register_channel(event->gen_time(), request);
      get_writer(location::PUBLIC)->write(event->gen_time(), request);
    }
  });

  events_ | is(TimeRequest::tag) | $([&](const event_ptr &event) {
    const TimeRequest &request = event->data<TimeRequest>();
    if (timer_tasks_.find(event->source()) == timer_tasks_.end()) {
      timer_tasks_[event->source()] = std::unordered_map<int32_t, timer_task>();
    }
    std::unordered_map<int32_t, timer_task> &app_tasks = timer_tasks_[event->source()];
    if (app_tasks.find(request.id) == app_tasks.end()) {
      app_tasks[request.id] = timer_task();
    }
    timer_task &task = app_tasks[request.id];
    task.checkpoint = time::now_in_nano() + request.duration;
    task.duration = request.duration;
    task.repeat_count = 0;
    task.repeat_limit = request.repeat;
  });

  events_ | is(Ping::tag) | $([&](const event_ptr &event) { get_io_device()->get_publisher()->publish("{}"); });

  events_ | is(CacheReset::tag) | $([&](const event_ptr &event) {
    auto msg_type = event->data<CacheReset>().msg_type;
    boost::hana::for_each(StateDataTypes, [&](auto it) {
      using DataType = typename decltype(+boost::hana::second(it))::type;
      if (DataType::tag == msg_type) {
        app_cache_shift_[event->source()] -= typed_event_ptr<DataType>(event);
        app_cache_shift_[event->dest()] /= typed_event_ptr<DataType>(event);
      }
    });
  });
}

void master::on_active() {
  auto now = time::now_in_nano();

  for (auto &app : timer_tasks_) {
    uint32_t app_id = app.first;
    auto &app_tasks = app.second;
    for (auto it = app_tasks.begin(); it != app_tasks.end();) {
      auto &task = it->second;
      if (task.checkpoint <= now) {
        get_writer(app_id)->mark(0, Time::tag);
        task.checkpoint += task.duration;
        task.repeat_count++;
        if (task.repeat_count >= task.repeat_limit) {
          it = app_tasks.erase(it);
          continue;
        }
      }
      it++;
    }
  }

  if (last_check_ + time_unit::NANOSECONDS_PER_SECOND < now) {
    on_interval_check(now);
    last_check_ = now;
  }
}

void master::try_add_location(int64_t trigger_time, const location_ptr &app_location) {
  if (not has_location(app_location->uid)) {
    profile_.set(dynamic_cast<Location &>(*app_location));
    add_location(trigger_time, app_location);
  }
}

void master::write_time_reset(int64_t trigger_time, const writer_ptr &writer) {
  auto time_base = time::get_base();
  TimeReset &time_reset = writer->open_data<TimeReset>();
  time_reset.system_clock_count = time_base.system_clock_count;
  time_reset.steady_clock_count = time_base.steady_clock_count;
  writer->close_data();
}

void master::write_trading_day(int64_t trigger_time, const writer_ptr &writer) {
  TradingDay &trading_day = writer->open_data<TradingDay>();
  trading_day.timestamp = acquire_trading_day();
  writer->close_data();
}

void master::write_profile_data(int64_t trigger_time, const writer_ptr &writer) {
  boost::hana::for_each(ProfileDataTypes, [&](auto it) {
    using DataType = typename decltype(+boost::hana::second(it))::type;
    for (const auto &data : profile_.get_all(DataType{})) {
      writer->write(trigger_time, data);
    }
  });
}

void master::write_registries(int64_t trigger_time, const writer_ptr &writer) {
  for (const auto &item : registry_) {
    writer->write(trigger_time, item.second);
  }
}

void master::write_channels(int64_t trigger_time, const writer_ptr &writer) {
  for (const auto &item : channels_) {
    writer->write(trigger_time, item.second);
  }
}
} // namespace kungfu::yijinjing::practice
