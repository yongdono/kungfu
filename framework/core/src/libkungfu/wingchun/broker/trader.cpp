// SPDX-License-Identifier: Apache-2.0

//
// Created by Keren Dong on 2019-06-20.
//

#include <kungfu/common.h>
#include <kungfu/wingchun/broker/trader.h>
#include <kungfu/yijinjing/journal/assemble.h>
#include <kungfu/yijinjing/time.h>

using namespace kungfu::rx;
using namespace kungfu::longfist::types;
using namespace kungfu::longfist::enums;
using namespace kungfu::yijinjing::practice;
using namespace kungfu::yijinjing;
using namespace kungfu::yijinjing::data;
using namespace kungfu::yijinjing::journal;

namespace kungfu::wingchun::broker {
TraderVendor::TraderVendor(locator_ptr locator, const std::string &group, const std::string &name, bool low_latency)
    : BrokerVendor(location::make_shared(mode::LIVE, category::TD, group, name, std::move(locator)), low_latency) {}

void TraderVendor::set_service(Trader_ptr service) { service_ = std::move(service); }

void TraderVendor::react() {
  events_ | skip_until(events_ | is(RequestStart::tag)) | is(OrderInput::tag) | $$(service_->handle_order_input(event));
  events_ | skip_until(events_ | is(RequestStart::tag)) | instanceof <frame>() | $$(service_->on_custom_event(event));
  apprentice::react();
}

void TraderVendor::on_react() {
  events_ | is(ResetBookRequest::tag) |
      $([&](const event_ptr &event) { get_writer(location::PUBLIC)->mark(now(), ResetBookRequest::tag); });
}

void TraderVendor::on_start() {
  BrokerVendor::on_start();

  events_ | is(BlockMessage::tag) | $$(service_->insert_block_message(event));
  events_ | is(OrderAction::tag) | $$(service_->cancel_order(event));
  events_ | is(AssetRequest::tag) | $$(service_->req_account());
  events_ | is(Deregister::tag) | $$(service_->on_strategy_exit(event));
  events_ | is(PositionRequest::tag) | $$(service_->req_position());
  events_ | is(RequestHistoryOrder::tag) | $$(service_->req_history_order(event));
  events_ | is(RequestHistoryTrade::tag) | $$(service_->req_history_trade(event));
  events_ | is(AssetSync::tag) | $$(service_->handle_asset_sync());
  events_ | is(PositionSync::tag) | $$(service_->handle_position_sync());

  events_ | filter([&](const event_ptr &event) {
    return event->msg_type() == BatchOrderBegin::tag or event->msg_type() == BatchOrderEnd::tag;
  }) | $$(service_->handle_batch_order_tag(event));

  service_->restore();
  service_->on_restore();
  service_->on_start();
}

BrokerService_ptr TraderVendor::get_service() { return service_; }

void TraderVendor::on_trading_day(const event_ptr &event, int64_t daytime) { service_->on_trading_day(event, daytime); }

const std::string &Trader::get_account_id() const { return get_home()->name; }

yijinjing::journal::writer_ptr Trader::get_asset_writer() const {
  return get_writer(sync_asset_ ? location::SYNC : location::PUBLIC);
}

yijinjing::journal::writer_ptr Trader::get_asset_margin_writer() const {
  return get_writer(sync_asset_margin_ ? location::SYNC : location::PUBLIC);
}

yijinjing::journal::writer_ptr Trader::get_position_writer() const {
  return get_writer(sync_position_ ? location::SYNC : location::PUBLIC);
}

void Trader::enable_asset_sync() { sync_asset_ = true; }

void Trader::enable_asset_margin_sync() { sync_asset_margin_ = true; }

void Trader::enable_positions_sync() { sync_position_ = true; }

bool Trader::write_default_asset_margin() {
  SPDLOG_INFO("Write an empty AssetMargin by default");
  sync_asset_margin_ = true;
  auto writer = get_asset_margin_writer();
  AssetMargin &asset_margin = writer->open_data<AssetMargin>();
  asset_margin.holder_uid = get_home_uid();
  asset_margin.update_time = yijinjing::time::now_in_nano();
  writer->close_data();
  return false;
}

void Trader::handle_asset_sync() {
  if (state_ == BrokerState::Ready) {
    req_account();
    write_default_asset_margin();
  }
}

void Trader::handle_position_sync() {
  if (state_ == BrokerState::Ready) {
    req_position();
  }
}

bool Trader::has_self_deal_risk(const event_ptr &event) {
  if (not self_deal_detect_) {
    return false;
  }
  const OrderInput &input = event->data<OrderInput>();

  auto fun_has_self_deal_risk = [&](const Order &order) -> bool {
    if (strcmp(order.instrument_id, input.instrument_id) or strcmp(order.exchange_id, input.exchange_id)) {
      return false;
    }
    if (order.side == input.side or is_final_status(order.status)) {
      return false;
    } else {
      if (input.price_type != PriceType::Limit) {
        return true;
      } else {
        if (input.side == Side::Buy and input.limit_price < order.limit_price) {
          return false;
        } else if (input.side == Side::Sell and input.limit_price > order.limit_price) {
          return false;
        } else {
          return true;
        }
      }
    }
  };

  for (const auto pair : orders_) {
    if (fun_has_self_deal_risk(pair.second.data)) {
      return true;
    }
  }
  return false;
}

void Trader::handle_order_input(const event_ptr &event) {
  if (has_self_deal_risk(event)) {
    Order &order = get_writer(event->source())->open_data<Order>();
    order_from_input(event->data<OrderInput>(), order);
    order.status = OrderStatus::Error;
    strncpy(order.error_msg, "该委托存在自成交风险,已拒绝下单", ERROR_MSG_LEN);
    order.insert_time = event->gen_time();
    order.update_time = event->gen_time();
    get_writer(event->source())->close_data();
    return;
  }

  /// try_emplace default insert false to map, means not batch mode
  if (batch_status_.try_emplace(event->source()).first->second) {
    const OrderInput &input = event->data<OrderInput>();
    order_inputs_.try_emplace(event->source()).first->second.push_back(input);
  } else {
    insert_order(event);
  }
}

void Trader::handle_batch_order_tag(const event_ptr &event) {
  if (event->msg_type() == BatchOrderBegin::tag) {
    batch_status_.insert_or_assign(event->source(), true);
  } else if (event->msg_type() == BatchOrderEnd::tag) {
    batch_status_.insert_or_assign(event->source(), false);
    insert_batch_orders(event);
    clear_order_inputs(event->source());
  }
}

bool Trader::insert_block_message(const event_ptr &event) {
  const BlockMessage &msg = event->data<BlockMessage>();
  return block_messages_.try_emplace(msg.block_id, msg).second;
}

void Trader::enable_self_detect() { self_deal_detect_ = true; }

void Trader::restore() {
  auto fn_deal_frame = [&](const frame_ptr &frame) {
    if (frame->msg_type() == Order::tag) {
      const Order &order = frame->data<Order>();
      external_order_id_to_order_id_.emplace(order.external_order_id, order.order_id);
      orders_.insert_or_assign(order.order_id, state<Order>(frame->source(), frame->dest(), frame->gen_time(), order));
    } else if (frame->msg_type() == Trade::tag) {
      const Trade &trade = frame->data<Trade>();
      trades_.insert_or_assign(trade.trade_id, state<Trade>(frame->source(), frame->dest(), frame->gen_time(), trade));
    }
  };

  assemble asb(get_home(), location::PUBLIC, AssembleMode::Write);
  asb.seek_to_time(time::today_start()); // restore from today
  while (asb.data_available()) {
    auto frame = asb.current_frame();
    fn_deal_frame(frame);
    asb.next();
  }
}

} // namespace kungfu::wingchun::broker
