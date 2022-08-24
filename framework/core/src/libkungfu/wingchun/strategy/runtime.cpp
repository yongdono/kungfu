//
// Created by Keren Dong on 2020-07-20.
//

#include <fmt/format.h>

#include <kungfu/wingchun/strategy/runtime.h>
#include <kungfu/yijinjing/log.h>
#include <kungfu/yijinjing/time.h>

using namespace kungfu::yijinjing::practice;
using namespace kungfu::rx;
using namespace kungfu::longfist;
using namespace kungfu::longfist::types;
using namespace kungfu::yijinjing;
using namespace kungfu::yijinjing::data;
using namespace kungfu::yijinjing::util;

namespace kungfu::wingchun::strategy {

RuntimeContext::RuntimeContext(apprentice &app, const rx::connectable_observable<event_ptr> &events)
    : app_(app), events_(events), broker_client_(app_), bookkeeper_(app_, broker_client_) {
  log::copy_log_settings(app_.get_home(), app_.get_home()->name);
}

void RuntimeContext::on_start() {
  broker_client_.on_start(events_);
  bookkeeper_.on_start(events_);
}

int64_t RuntimeContext::now() const { return app_.now(); }

void RuntimeContext::add_timer(int64_t nanotime, const std::function<void(event_ptr)> &callback) {
  app_.add_timer(nanotime, callback);
}

void RuntimeContext::add_time_interval(int64_t duration, const std::function<void(event_ptr)> &callback) {
  app_.add_time_interval(duration, callback);
}

void RuntimeContext::add_account(const std::string &source, const std::string &account) {
  uint32_t hashed_account = hash_account(source, account);

  if (td_locations_.find(hashed_account) != td_locations_.end()) {
    throw wingchun_error(fmt::format("duplicated account {}_{}", source, account));
  }

  auto home = app_.get_io_device()->get_home();
  auto account_location = location::make_shared(mode::LIVE, category::TD, source, account, home->locator);
  if (home->mode == mode::LIVE and not app_.has_location(account_location->uid)) {
    throw wingchun_error(fmt::format("invalid account {}_{}", source, account));
  }

  td_locations_.emplace(hashed_account, account_location);
  td_locations_.emplace(account_location->uid, account_location);
  account_location_ids_.emplace(hashed_account, account_location->uid);

  broker_client_.enroll_account(account_location);
}

void RuntimeContext::subscribe(const std::string &source, const std::vector<std::string> &instrument_ids,
                               const std::string &exchange_ids) {
  auto md_location = find_md_location(source);
  for (const auto &instrument_id : instrument_ids) {
    broker_client_.subscribe(md_location, exchange_ids, instrument_id);
  }
  md_locations_.emplace(md_location->uid, md_location);
}

void RuntimeContext::subscribe_all(const std::string &source, uint8_t market_type, uint64_t instrument_type,
                                   uint64_t data_type) {
  broker_client_.subscribe_all(find_md_location(source), market_type, instrument_type, data_type);
}

uint64_t RuntimeContext::insert_block_message(const std::string &source, const std::string &account,
                                              uint32_t opponent_seat, uint64_t match_number, bool is_specific) {
  auto account_location_uid = get_td_location_uid(source, account);
  if (not broker_client_.is_ready(account_location_uid)) {
    SPDLOG_ERROR("account {} not ready", td_locations_.at(account_location_uid)->uname);
    return 0;
  }
  auto writer = app_.get_writer(account_location_uid);
  BlockMessage &msg = writer->open_data<BlockMessage>(app_.now());
  msg.opponent_seat = opponent_seat;
  msg.match_number = match_number;
  msg.is_specific = is_specific;
  msg.block_id = writer->current_frame_uid();
  writer->close_data();
  return msg.block_id;
}

uint64_t RuntimeContext::insert_order(const std::string &instrument_id, const std::string &exchange_id,
                                      const std::string &source, const std::string &account, double limit_price,
                                      int64_t volume, PriceType type, Side side, Offset offset, HedgeFlag hedge_flag,
                                      bool is_swap, uint64_t block_id) {
  auto account_location_uid = get_td_location_uid(source, account);
  auto insert_time = time::now_in_nano();
  if (not broker_client_.is_ready(account_location_uid)) {
    SPDLOG_ERROR("account {} not ready", td_locations_.at(account_location_uid)->uname);
    return 0;
  }
  auto instrument_type = get_instrument_type(exchange_id, instrument_id);
  if (instrument_type == InstrumentType::Unknown) {
    SPDLOG_ERROR("unsupported instrument type {} of {}.{}", str_from_instrument_type(instrument_type), instrument_id,
                 exchange_id);
    return 0;
  }
  auto writer = app_.get_writer(account_location_uid);
  OrderInput &input = writer->open_data<OrderInput>(app_.now());
  input.order_id = writer->current_frame_uid();
  strcpy(input.instrument_id, instrument_id.c_str());
  strcpy(input.exchange_id, exchange_id.c_str());
  input.instrument_type = instrument_type;
  input.limit_price = limit_price;
  input.frozen_price = limit_price;
  input.volume = volume;
  input.price_type = type;
  input.side = side;
  input.offset = offset;
  input.hedge_flag = hedge_flag;
  input.block_id = block_id;
  input.is_swap = is_swap;
  input.insert_time = insert_time;
  writer->close_data();
  bookkeeper_.on_order_input(app_.now(), app_.get_home_uid(), account_location_uid, input);
  return input.order_id;
}

uint64_t RuntimeContext::cancel_order(uint64_t order_id) {
  uint32_t account_location_uid = (order_id >> 32u) xor (app_.get_home_uid());
  if (not broker_client_.is_ready(account_location_uid)) {
    SPDLOG_ERROR("invalid order_id {:16x}", order_id);
    return 0;
  }
  auto account_location = app_.get_location(account_location_uid);
  auto writer = app_.get_writer(account_location_uid);
  OrderAction &action = writer->open_data<OrderAction>(0);

  action.order_action_id = writer->current_frame_uid();
  action.order_id = order_id;
  action.action_flag = OrderActionFlag::Cancel;

  writer->close_data();
  return action.order_action_id;
}

const location_map &RuntimeContext::list_md() const { return md_locations_; }

const location_map &RuntimeContext::list_accounts() const { return td_locations_; }

int64_t RuntimeContext::get_trading_day() const { return app_.get_trading_day(); }

broker::Client &RuntimeContext::get_broker_client() { return broker_client_; }

book::Bookkeeper &RuntimeContext::get_bookkeeper() { return bookkeeper_; }

uint32_t RuntimeContext::lookup_account_location_id(const std::string &account) const {
  return account_location_ids_.at(hash_str_32(account));
}

uint32_t RuntimeContext::get_td_location_uid(const std::string &source, const std::string &account) const {
  uint32_t hashed_account = hash_account(source, account);
  if (td_locations_.find(hashed_account) == td_locations_.end()) {
    throw wingchun_error(fmt::format("invalid account {}_{}", source, account));
  }

  return td_locations_.at(hashed_account)->uid;
}

const location_ptr &RuntimeContext::find_md_location(const std::string &source) {
  if (market_data_.find(source) == market_data_.end()) {
    auto home = app_.get_home();
    auto md_location = location::make_shared(mode::LIVE, category::MD, source, source, home->locator);
    if (not app_.has_location(md_location->uid)) {
      throw wingchun_error(fmt::format("invalid md {}", source));
    }
    market_data_.emplace(source, md_location);
  }
  return market_data_.at(source);
}

void RuntimeContext::req_history_order(const std::string &source, const std::string &account, uint32_t query_num) {
  auto account_location_uid = get_td_location_uid(source, account);
  if (not broker_client_.is_ready(account_location_uid)) {
    SPDLOG_ERROR("account {}_{} not ready", source, account);
    return;
  }
  auto writer = app_.get_writer(account_location_uid);
  RequestHistoryOrder &request = writer->open_data<RequestHistoryOrder>();
  request.trigger_time = now();
  request.query_num = query_num;
  writer->close_data();
}

void RuntimeContext::req_history_trade(const std::string &source, const std::string &account, uint32_t query_num) {
  auto account_location_uid = get_td_location_uid(source, account);
  if (not broker_client_.is_ready(account_location_uid)) {
    SPDLOG_ERROR("account {}_{} not ready", source, account);
    return;
  }
  auto writer = app_.get_writer(account_location_uid);
  RequestHistoryTrade &request = writer->open_data<RequestHistoryTrade>();
  request.trigger_time = now();
  request.query_num = query_num;
  writer->close_data();
}

void RuntimeContext::req_deregister() { app_.request_deregister(); }

void RuntimeContext::update_strategy_state(StrategyStateUpdate &state_update) {
  auto writer = app_.get_writer(location::PUBLIC);
  state_update.update_time = now();
  writer->write(state_update.update_time, state_update);
}

} // namespace kungfu::wingchun::strategy