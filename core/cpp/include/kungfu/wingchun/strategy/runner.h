//
// Created by Keren Dong on 2019-06-20.
//

#ifndef WINGCHUN_RUNNER_H
#define WINGCHUN_RUNNER_H

#include <kungfu/yijinjing/practice/apprentice.h>
#include <kungfu/wingchun/strategy/context.h>
#include <kungfu/wingchun/strategy/strategy.h>

namespace kungfu
{
    namespace wingchun
    {
        namespace strategy
        {
            class Runner : public yijinjing::practice::apprentice
            {
            public:
                Runner(yijinjing::data::locator_ptr locator, const std::string &group, const std::string &name, yijinjing::data::mode m, bool low_latency);

                virtual ~Runner() = default;

                void add_strategy(const Strategy_ptr& strategy);

                void on_trading_day(const event_ptr &event, int64_t daytime) override;

            protected:
                std::vector<Strategy_ptr> strategies_;

                void on_start() override ;

                void on_exit() override ;

                virtual Context_ptr make_context();

            private:
                Context_ptr context_;
            };
        }
    }
}

#endif //WINGCHUN_RUNNER_H