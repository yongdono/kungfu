<script setup lang="ts">
import { computed, nextTick, ref } from 'vue';
import KfDashboard from '@kungfu-trader/kungfu-app/src/renderer/components/public/KfDashboard.vue';
import KfDashboardItem from '@kungfu-trader/kungfu-app/src/renderer/components/public/KfDashboardItem.vue';
import KfConfigSettingsForm from '@kungfu-trader/kungfu-app/src/renderer/components/public/KfConfigSettingsForm.vue';
import { getConfigSettings } from './config';
import { RuleObject } from 'ant-design-vue/lib/form';
import { categoryRegisterConfig } from '../posGlobal/config';
import { FutureArbitrageCodeEnum } from '@kungfu-trader/kungfu-js-api/typings/enums';
import { makeOrderByOrderInput } from '@kungfu-trader/kungfu-js-api/kungfu';
import {
  getProcessIdByKfLocation,
  initFormStateByConfig,
  transformSearchInstrumentResultToInstrument,
} from '@kungfu-trader/kungfu-js-api/utils/busiUtils';
import {
  useCurrentGlobalKfLocation,
  useProcessStatusDetailData,
} from '@kungfu-trader/kungfu-app/src/renderer/assets/methods/actionsUtils';
import {
  confirmModal,
  messagePrompt,
  useDashboardBodySize,
} from '@kungfu-trader/kungfu-app/src/renderer/assets/methods/uiUtils';
import VueI18n from '@kungfu-trader/kungfu-js-api/language';
import { dealOrderPlaceVNode } from '../makeOrder/utils';

const { t } = VueI18n.global;
const { error } = messagePrompt();

const { handleBodySizeChange } = useDashboardBodySize();

const formState = ref(initFormStateByConfig(getConfigSettings(), {}));
const formRef = ref();
const { processStatusData } = useProcessStatusDetailData();

const {
  currentGlobalKfLocation,
  currentCategoryData,
  getCurrentGlobalKfLocationId,
} = useCurrentGlobalKfLocation(window.watcher);

const isShowCurrentGlobalKfLocationTitle = computed(() => {
  return (
    currentGlobalKfLocation.value?.category !== categoryRegisterConfig.category
  );
});

const tdList = computed<KungfuApi.KfLocation[] | null | undefined>(() => {
  return currentGlobalKfLocation.value &&
    'children' in currentGlobalKfLocation.value
    ? currentGlobalKfLocation.value.children
    : null;
});

const configSettings = computed(() => {
  if (!currentGlobalKfLocation.value) {
    return getConfigSettings();
  }

  const { category } = currentGlobalKfLocation.value;
  return getConfigSettings(category, +formState.value.side);
});

const rules = {
  instrument_A: {
    validator: arbitrageExchangeValidator,
    trigger: 'change',
  },
  instrument_B: {
    validator: arbitrageExchangeValidator,
    trigger: 'change',
  },
};

function arbitrageExchangeValidator(
  _rule: RuleObject,
  value: number | string,
): Promise<void> {
  const instrumentResolved = transformSearchInstrumentResultToInstrument(
    value.toString(),
  );

  if (!instrumentResolved) {
    return Promise.resolve();
  }

  const { exchangeId } = instrumentResolved;

  if (
    formState.value.future_arbitrage_code === FutureArbitrageCodeEnum.SP ||
    formState.value.future_arbitrage_code === FutureArbitrageCodeEnum.SPC
  ) {
    if (exchangeId !== 'CZCE') {
      return Promise.reject(
        new Error(
          `${t('futureArbitrageConfig.only_corresponding')}
            ${t('tradingConfig.CZCE')}
            ${t('tradingConfig.instrument')},
          `,
        ),
      );
    }
  }

  if (
    formState.value.future_arbitrage_code === FutureArbitrageCodeEnum.SPD ||
    formState.value.future_arbitrage_code === FutureArbitrageCodeEnum.IPS
  ) {
    if (exchangeId !== 'DCE') {
      return Promise.reject(
        new Error(
          `${t('futureArbitrageConfig.only_corresponding')} 
          ${t('tradingConfig.DCE')} 
          ${t('tradingConfig.instrument')}`,
        ),
      );
    }
  }

  return Promise.resolve();
}

function handleResetMakeOrderForm() {
  const initFormState = initFormStateByConfig(configSettings.value, {});
  Object.keys(initFormState).forEach((key) => {
    formState.value[key] = initFormState[key];
  });
  nextTick().then(() => {
    formRef.value.clearValidate();
  });
}

function handleMakeOrder() {
  formRef.value
    .validate()
    .then(async () => {
      const instrumentA = formState.value.instrument_A.toString();
      const instrumentB = formState.value.instrument_B.toString();
      const instrumnetResolved_A =
        transformSearchInstrumentResultToInstrument(instrumentA);
      const instrumnetResolved_B =
        transformSearchInstrumentResultToInstrument(instrumentB);

      if (!instrumnetResolved_A || !instrumnetResolved_B) {
        error(t('instrument_error'));
        return;
      }

      const { exchangeId, instrumentType } = instrumnetResolved_A;

      const {
        account_id,
        future_arbitrage_code,
        limit_price,
        volume,
        price_type,
        side,
        offset,
        hedge_flag,
        is_swap,
      } = formState.value;

      const instrumentId = `${future_arbitrage_code} ${instrumnetResolved_A.instrumentId}&${instrumnetResolved_B.instrumentId}`;
      const makeOrderInput: KungfuApi.MakeOrderInput = {
        instrument_id: instrumentId,
        instrument_type: +instrumentType,
        exchange_id: exchangeId,
        limit_price: +limit_price,
        volume: +volume,
        price_type: +price_type,
        side: +side,
        offset: +(offset !== undefined ? offset : +side === 0 ? 0 : 1),
        hedge_flag: +(hedge_flag || 0),
        is_swap: !!is_swap,
        parent_id: 0n,
      };

      if (!currentGlobalKfLocation.value) {
        error(t('location_error'));
        return;
      }

      const tdProcessId =
        currentGlobalKfLocation.value?.category === 'td'
          ? getProcessIdByKfLocation(currentGlobalKfLocation.value)
          : `td_${account_id.toString()}`;

      if (processStatusData.value[tdProcessId] !== 'online') {
        error(
          `${t('orderConfig.start')} ${tdProcessId} ${t('orderConfig.td')}`,
        );
        return;
      }

      const flag = await confirmModal(
        t('tradingConfig.place_confirm'),
        dealOrderPlaceVNode(makeOrderInput, 1, true),
      );
      if (!flag) return;

      makeOrderByOrderInput(
        window.watcher,
        makeOrderInput,
        currentGlobalKfLocation.value,
        tdProcessId.toAccountId(),
      ).catch((err) => {
        error(err.message);
      });
    })
    .catch((err: Error) => {
      console.error(err);
    });
}
</script>
<template>
  <div class="kf-make-order-dashboard__warp">
    <KfDashboard @boardSizeChange="handleBodySizeChange">
      <template v-slot:title>
        <span
          v-if="currentGlobalKfLocation && isShowCurrentGlobalKfLocationTitle"
        >
          <a-tag
            v-if="currentCategoryData"
            :color="currentCategoryData?.color || 'default'"
          >
            {{ currentCategoryData?.name }}
          </a-tag>
          <span class="name" v-if="currentGlobalKfLocation">
            {{ getCurrentGlobalKfLocationId(currentGlobalKfLocation) }}
          </span>
        </span>
      </template>
      <template v-slot:header>
        <KfDashboardItem>
          <a-button size="small" @click="handleResetMakeOrderForm">
            {{ $t('futureArbitrageConfig.reset_order') }}
          </a-button>
        </KfDashboardItem>
      </template>
      <div class="make-order__warp">
        <div class="make-order-form__warp">
          <KfConfigSettingsForm
            ref="formRef"
            v-model:formState="formState"
            :configSettings="configSettings"
            :tdList="tdList"
            changeType="add"
            :label-col="6"
            :wrapper-col="14"
            :rules="rules"
          ></KfConfigSettingsForm>
        </div>
        <div class="make-order-btns">
          <a-button size="small" @click="handleMakeOrder">
            {{ $t('futureArbitrageConfig.place_order') }}
          </a-button>
        </div>
      </div>
    </KfDashboard>
  </div>
</template>
<style lang="less">
.kf-make-order-dashboard__warp {
  width: 100%;
  height: 100%;

  .make-order__warp {
    display: flex;
    justify-content: space-between;
    height: 100%;

    .make-order-form__warp {
      flex: 1;
      height: 100%;
      padding-top: 16px;
      overflow-y: overlay;

      .ant-form-item {
        margin-bottom: 16px;

        .ant-form-item-explain,
        .ant-form-item-extra {
          min-height: unset;
        }
      }
    }

    .make-order-btns {
      width: 40px;
      height: 100%;

      .ant-btn {
        height: 100%;
        text-align: center;
        word-break: break-word;
        word-wrap: unset;
        white-space: normal;
      }
    }
  }
}
.modal-node {
  .root-node {
    display: flex;
    flex-wrap: nowrap;
    .green {
      color: @green-base !important;
    }
    .red {
      color: @red-base !important;
    }
    .order-number {
      flex: 1;
      margin-top: 10%;
      text-align: center;
    }
  }
}
</style>
