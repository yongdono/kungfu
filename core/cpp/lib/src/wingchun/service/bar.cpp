
#include <kungfu/wingchun/common.h>
#include <kungfu/wingchun/service/bar.h>
#include <kungfu/yijinjing/log.h>
#include <regex>

using namespace kungfu::longfist::types;
using namespace kungfu::rx;
using namespace kungfu::yijinjing;
using namespace kungfu::yijinjing::data;

namespace kungfu::wingchun::service {
static int64_t parse_time_interval(const std::string &s) {
  std::regex r("[0-9]+");
  std::smatch m;
  std::regex_search(s, m, r);
  if (m.empty()) {
    throw std::runtime_error("invalid time interval: " + s);
  }
  int n = std::stoi(m[0]);
  if (endswith(s, "s")) {
    return n * time_unit::NANOSECONDS_PER_SECOND;
  } else if (endswith(s, "m")) {
    return n * time_unit::NANOSECONDS_PER_MINUTE;
  } else if (endswith(s, "h")) {
    return n * time_unit::NANOSECONDS_PER_HOUR;
  } else if (endswith(s, "d")) {
    return n * time_unit::NANOSECONDS_PER_DAY;
  } else {
    throw std::runtime_error("invalid time_interval: " + s);
  }
}

BarGenerator::BarGenerator(locator_ptr locator, mode m, bool low_latency, const std::string &json_config)
    : yijinjing::practice::apprentice(location::make_shared(m, category::SYSTEM, "service", "bar", std::move(locator)),
                                      low_latency),
      time_interval_(kungfu::yijinjing::time_unit::NANOSECONDS_PER_MINUTE) {
  log::copy_log_settings(get_io_device()->get_home(), "bar");
  auto config = nlohmann::json::parse(json_config);
  std::string source = config["source"];
  auto home = get_io_device()->get_home();
  source_location_ = location::make_shared(mode::LIVE, category::MD, source, source, home->locator);
  if (config.find("time_interval") != config.end()) {
    time_interval_ = parse_time_interval(config["time_interval"]);
  }
}

bool BarGenerator::subscribe(const std::vector<InstrumentKey> &instruments) {
  if (not has_writer(source_location_->uid)) {
    events_ | is(RequestWriteTo::tag) | filter([=](const event_ptr &event) {
      const RequestWriteTo &data = event->data<RequestWriteTo>();
      return data.dest_id == source_location_->uid;
    }) | first() |
        $([=](const event_ptr &event) {
          for (const auto &inst : instruments) {
            auto instrument_key = hash_instrument(inst.exchange_id, inst.instrument_id);
            if (bars_.find(instrument_key) == bars_.end()) {
              get_writer(source_location_->uid)->write(0, inst);
              auto now_in_nano = now();
              auto start_time = now_in_nano - now_in_nano % time_interval_ + time_interval_;
              SPDLOG_INFO("subscribe {}.{}", inst.instrument_id, inst.exchange_id);
              Bar bar = {};
              strncpy(bar.instrument_id, inst.instrument_id, INSTRUMENT_ID_LEN);
              strncpy(bar.exchange_id, inst.exchange_id, EXCHANGE_ID_LEN);
              bar.start_time = start_time;
              bar.end_time = start_time + time_interval_;
              bars_[instrument_key] = bar;
            }
          }
        });
  } else {
    for (const auto &inst : instruments) {
      auto instrument_key = hash_instrument(inst.exchange_id, inst.instrument_id);
      if (bars_.find(instrument_key) == bars_.end()) {
        get_writer(source_location_->uid)->write(0, inst);
        auto now_in_nano = now();
        auto start_time = now_in_nano - now_in_nano % time_interval_ + time_interval_;
        SPDLOG_INFO("subscribe {}.{}", inst.instrument_id, inst.exchange_id);
        Bar bar = {};
        strncpy(bar.instrument_id, inst.instrument_id, INSTRUMENT_ID_LEN);
        strncpy(bar.exchange_id, inst.exchange_id, EXCHANGE_ID_LEN);
        bar.start_time = start_time;
        bar.end_time = start_time + time_interval_;
        bars_[instrument_key] = bar;
      }
    }
  }
  return true;
}

void BarGenerator::on_start() {
  apprentice::on_start();

  events_ | is(Register::tag) | $([&](const event_ptr &event) {
    auto register_data = event->data<Register>();
    if (register_data.location_uid == source_location_->uid) {
      request_read_from_public(now(), source_location_->uid, now());
      request_write_to(now(), source_location_->uid);
      SPDLOG_INFO("added md {} [{:08x}]", source_location_->uname, source_location_->uid);
    }
  });

  events_ | is(Quote::tag) | $([&](const event_ptr &event) {
    const auto &quote = event->data<Quote>();
    auto instrument_key = hash_instrument(quote.exchange_id, quote.instrument_id);
    if (bars_.find(instrument_key) != bars_.end()) {
      SPDLOG_TRACE("{}.{} at {} vol {} price {}", quote.instrument_id, quote.exchange_id,
                   time::strftime(quote.data_time), quote.volume, quote.last_price);
      auto &bar = bars_[instrument_key];
      if (quote.data_time >= bar.start_time && quote.data_time <= bar.end_time) {
        if (bar.tick_count == 0) {
          bar.high = quote.last_price;
          bar.low = quote.last_price;
          bar.open = quote.last_price;
          bar.close = quote.last_price;
          bar.start_volume = quote.volume;
        }
        bar.tick_count++;
        bar.volume = quote.volume - bar.start_volume;
        bar.high = std::max(bar.high, quote.last_price);
        bar.low = std::min(bar.low, quote.last_price);
        bar.close = quote.last_price;
      }
      if (quote.data_time >= bar.end_time) {
        this->get_writer(0)->write(0, Bar::tag, bar);
        SPDLOG_INFO("{}.{} [o:{},c:{},h:{},l:{}] from {} to {}", bar.instrument_id, bar.exchange_id, bar.open,
                    bar.close, bar.high, bar.low, time::strftime(bar.start_time), time::strftime(bar.end_time));
        bar.start_time = bar.end_time;
        while (bar.start_time + time_interval_ < quote.data_time) {
          bar.start_time += time_interval_;
        }
        bar.end_time = bar.start_time + time_interval_;
        if (bar.start_time <= quote.data_time) {
          bar.tick_count = 1;
          bar.start_volume = quote.volume;
          bar.volume = 0;
          bar.high = quote.last_price;
          bar.low = quote.last_price;
          bar.open = quote.last_price;
          bar.close = quote.last_price;
        } else {
          bar.tick_count = 0;
          bar.start_volume = 0;
          bar.volume = 0;
          bar.high = 0;
          bar.low = 0;
          bar.open = 0;
          bar.close = 0;
        }
      }
    }
  });

  events_ | is(InstrumentKey::tag) | $$(subscribe({event->data<InstrumentKey>()}));
}

} // namespace kungfu::wingchun::service
