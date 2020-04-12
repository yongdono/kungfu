//
// Created by qlu on 2019/1/16.
//

#ifndef WINGCHUN_STRATEGY_H
#define WINGCHUN_STRATEGY_H

#include <kungfu/longfist/longfist.h>
#include <kungfu/yijinjing/practice/apprentice.h>

namespace kungfu::wingchun::strategy {
FORWARD_DECLARE_PTR(Context)

class Strategy {
public:
  virtual ~Strategy() = default;

  //运行前
  virtual void pre_start(Context_ptr &context){};

  virtual void post_start(Context_ptr &context){};

  //退出前
  virtual void pre_stop(Context_ptr &context){};

  virtual void post_stop(Context_ptr &context){};

  //交易日切换回调
  //@param daytime 交易日
  virtual void on_trading_day(Context_ptr &context, int64_t daytime){};

  //行情数据更新回调
  //@param quote             行情数据
  virtual void on_quote(Context_ptr &context, const longfist::types::Quote &quote){};

  // bar 数据更新回调
  //@param bar               bar 数据
  virtual void on_bar(Context_ptr &context, const longfist::types::Bar &bar){};

  //逐笔委托更新回调
  //@param entrust           逐笔委托数据
  virtual void on_entrust(Context_ptr &context, const longfist::types::Entrust &entrust){};

  //逐笔成交更新回调
  //@param transaction       逐笔成交数据
  virtual void on_transaction(Context_ptr &context, const longfist::types::Transaction &transaction){};

  //订单信息更新回调
  //@param order             订单信息数据
  virtual void on_order(Context_ptr &context, const longfist::types::Order &order){};

  //订单操作错误回调
  //@param order             订单信息数据
  virtual void on_order_action_error(Context_ptr &context, const longfist::types::OrderActionError &error){};

  //订单成交回报回调
  //@param trade             订单成交数据
  virtual void on_trade(Context_ptr &context, const longfist::types::Trade &trade){};
};

DECLARE_PTR(Strategy)
} // namespace kungfu::wingchun::strategy
#endif // WINGCHUN_STRATEGY_H
