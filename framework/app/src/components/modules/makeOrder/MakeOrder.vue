<script setup lang="ts">
import { computed, nextTick, onMounted, reactive, ref, watch } from 'vue';
import KfDashboard from '@kungfu-trader/kungfu-app/src/renderer/components/public/KfDashboard.vue';
import KfDashboardItem from '@kungfu-trader/kungfu-app/src/renderer/components/public/KfDashboardItem.vue';
import KfConfigSettingsForm from '@kungfu-trader/kungfu-app/src/renderer/components/public/KfConfigSettingsForm.vue';
import {
  useTriggerMakeOrder,
  useDashboardBodySize,
  confirmModal,
  messagePrompt,
} from '@kungfu-trader/kungfu-app/src/renderer/assets/methods/uiUtils';
import { getConfigSettings, LABEL_COL, WRAPPER_COL } from './config';
import {
  dealOrderPlaceVNode,
  dealStockOffset,
  transformOrderInputToExtConfigForm,
} from './utils';
import {
  makeOrderByOrderInput,
  hashInstrumentUKey,
  getPosClosableVolume,
} from '@kungfu-trader/kungfu-js-api/kungfu';
import {
  InstrumentTypeEnum,
  OffsetEnum,
  OrderInputKeyEnum,
  SideEnum,
} from '@kungfu-trader/kungfu-js-api/typings/enums';
import {
  useCurrentGlobalKfLocation,
  useExtConfigsRelated,
  useInstruments,
  useProcessStatusDetailData,
  useTradeLimit,
} from '@kungfu-trader/kungfu-app/src/renderer/assets/methods/actionsUtils';
import {
  dealVolumeByInstrumentType,
  getExtConfigList,
  getIdByKfLocation,
  getOffsetByOffsetFilter,
  getProcessIdByKfLocation,
  initFormStateByConfig,
  isShotable,
} from '@kungfu-trader/kungfu-js-api/utils/busiUtils';
import OrderConfirmModal from './OrderConfirmModal.vue';
import VueI18n, { useLanguage } from '@kungfu-trader/kungfu-js-api/language';
import { useTradingTask } from '../tradingTask/utils';
import { useGlobalStore } from '@kungfu-trader/kungfu-app/src/renderer/pages/index/store/global';
import { storeToRefs } from 'pinia';
import {
  useMakeOrderInfo,
  useMakeOrderSubscribe,
} from '@kungfu-trader/kungfu-app/src/renderer/assets/methods/actionsUtils';

const { t } = VueI18n.global;
const { error } = messagePrompt();

const { instrumentKeyAccountsMap, uiExtConfigs } = storeToRefs(
  useGlobalStore(),
);
const { isLanguageKeyAvailable } = useLanguage();
const { globalSetting } = storeToRefs(useGlobalStore());

const { handleBodySizeChange } = useDashboardBodySize();
const formState = ref(
  initFormStateByConfig(getConfigSettings('td', InstrumentTypeEnum.future), {}),
);

const formRef = ref();
const { subscribeAllInstrumentByAppStates } = useInstruments();
const { appStates, processStatusData } = useProcessStatusDetailData();
const { mdExtTypeMap, extConfigs } = useExtConfigsRelated();
const { triggerOrderBook } = useTriggerMakeOrder();
const {
  showAmountOrPosition,
  instrumentResolved,
  currentPositionWithLongDirection,
  currentPositionWithShortDirection,
  currentPosition,
  currentResidueMoney,
  currentResiduePosVolume,
  currentPrice,
  currentTradeAmount,
  currentAvailMoney,
  currentAvailPosVolume,
  isAccountOrInstrumentConfirmed,
} = useMakeOrderInfo(formState);
useMakeOrderSubscribe(formState);

const {
  currentGlobalKfLocation,
  currentCategoryData,
  getCurrentGlobalKfLocationId,
} = useCurrentGlobalKfLocation(window.watcher);

const { getValidatorByOrderInputKey } = useTradeLimit();

const makeOrderInstrumentType = ref<InstrumentTypeEnum>(
  InstrumentTypeEnum.unknown,
);

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
  const { side } = formState.value;
  return getConfigSettings(
    category,
    makeOrderInstrumentType.value,
    side,
    +formState.value.price_type,
  );
});

const rules = computed(() => {
  const { instrument } = formState.value;
  return {
    volume: {
      validator: getValidatorByOrderInputKey(
        OrderInputKeyEnum.VOLUME,
        instrument,
      ),
      trigger: 'change',
    },
    limit_price: {
      validator: getValidatorByOrderInputKey(
        OrderInputKeyEnum.PRICE,
        instrument,
      ),
      trigger: 'change',
    },
  };
});

const isShowConfirmModal = ref<boolean>(false);
const curOrderVolume = ref<number>(0);
const curOrderType = ref<InstrumentTypeEnum>(InstrumentTypeEnum.unknown);
const formSteps = reactive<
  Partial<Record<keyof KungfuApi.MakeOrderInput, number>>
>({});
const currentPercent = ref<number>(0);
const percentList = [10, 20, 50, 80, 100];

const makeOrderData = computed(() => {
  if (!instrumentResolved.value) {
    return null;
  }

  const { exchangeId, instrumentId, instrumentType } = instrumentResolved.value;

  const { limit_price, volume, price_type, side, offset, hedge_flag, is_swap } =
    formState.value;

  const makeOrderInput: KungfuApi.MakeOrderInput = {
    instrument_id: instrumentId,
    instrument_type: +instrumentType,
    exchange_id: exchangeId,
    limit_price: +limit_price,
    volume: +volume,
    price_type: +price_type,
    side: +side,
    offset: getResolvedOffset(offset, side, instrumentType),
    hedge_flag: +(hedge_flag || 0),
    is_swap: !!is_swap,
    parent_id: 0n,
  };
  return makeOrderInput;
});

const availTradingTaskExtensionList = computed(() => {
  return getExtConfigList(extConfigs.value, 'strategy').filter(
    (item) => uiExtConfigs.value[item.key]?.position === 'make_order',
  );
});

const getResolvedOffset = (
  offset: OffsetEnum,
  side: SideEnum,
  instrumentType: InstrumentTypeEnum,
) => {
  if (isShotable(instrumentType)) {
    if (offset !== undefined) {
      return offset;
    }
  }
  return side === 0 ? 0 : 1;
};

watch(
  () => currentGlobalKfLocation.value,
  (newVal) => {
    if (newVal?.category === 'td') {
      formState.value.account_id = getIdByKfLocation(newVal);
    } else {
      formState.value.account_id = '';
    }
  },
);

watch(
  () => formState.value.instrument,
  (newVal) => {
    if (
      !formState.value.account_id &&
      currentGlobalKfLocation.value?.category !== 'td' &&
      instrumentKeyAccountsMap.value[newVal] &&
      instrumentKeyAccountsMap.value[newVal].length
    ) {
      formState.value.account_id = instrumentKeyAccountsMap.value[newVal][0];
    }

    if (!instrumentResolved.value) {
      return;
    }

    subscribeAllInstrumentByAppStates(
      processStatusData.value,
      appStates.value,
      mdExtTypeMap.value,
      [instrumentResolved.value],
    );
    triggerOrderBook(instrumentResolved.value);

    const { instrumentId, exchangeId } = instrumentResolved.value;
    const instrumentUKey = hashInstrumentUKey(instrumentId, exchangeId);
    formSteps.limit_price =
      (window.watcher.ledger.Instrument[instrumentUKey] as KungfuApi.Instrument)
        ?.price_tick || 0.001;

    makeOrderInstrumentType.value = instrumentResolved.value.instrumentType;
  },
);

watch(
  () => formState.value.side,
  (newSide) => {
    if (instrumentResolved.value) {
      const { instrumentType } = instrumentResolved.value;

      const resolveOffsetByPosition = (pos: KungfuApi.PositionResolved) => {
        return pos.yesterday_volume
          ? getOffsetByOffsetFilter('CloseYest', OffsetEnum.Close)
          : getOffsetByOffsetFilter('CloseToday', OffsetEnum.Close);
      };

      if (isShotable(instrumentType)) {
        if (newSide === SideEnum.Sell) {
          if (currentPositionWithLongDirection.value) {
            formState.value.offset = currentPositionWithLongDirection.value
              ? resolveOffsetByPosition(currentPositionWithLongDirection.value)
              : OffsetEnum.Open;
          }
        } else if (newSide === SideEnum.Buy) {
          formState.value.offset = currentPositionWithShortDirection.value
            ? resolveOffsetByPosition(currentPositionWithShortDirection.value)
            : OffsetEnum.Open;
        }
      } else {
        formState.value.offset =
          newSide === SideEnum.Buy ? OffsetEnum.Open : OffsetEnum.Close;
      }
    }
  },
);

watch(
  () => formState.value,
  (newVal) => {
    const { account_id, instrument, volume, side, offset } = newVal;
    useGlobalStore().setGlobalFormState({
      account_id,
      instrument,
      volume,
      side,
      offset,
    });
  },
  {
    deep: true,
  },
);

onMounted(() => {
  if (currentGlobalKfLocation.value?.category === 'td') {
    formState.value.account_id = getIdByKfLocation(
      currentGlobalKfLocation.value,
    );
  } else {
    formState.value.account_id = '';
  }
});

// 下单操作
function placeOrder(
  orderInput: KungfuApi.MakeOrderInput,
  globalKfLocation: KungfuApi.KfLocation,
  tdProcessId: string,
): Promise<bigint> {
  return makeOrderByOrderInput(
    window.watcher,
    orderInput,
    globalKfLocation,
    tdProcessId.toAccountId(),
  );
}

function initOrderInputData(): Promise<KungfuApi.MakeOrderInput> {
  if (!instrumentResolved.value) {
    return Promise.reject(new Error(t('instrument_error')));
  }

  const { exchangeId, instrumentId, instrumentType } = instrumentResolved.value;
  const { limit_price, volume, price_type, side, offset, hedge_flag, is_swap } =
    formState.value;

  const makeOrderInput: KungfuApi.MakeOrderInput = {
    instrument_id: instrumentId,
    instrument_type: +instrumentType,
    exchange_id: exchangeId,
    limit_price: +limit_price,
    volume: +volume,
    price_type: +price_type,
    side: +side,
    offset: getResolvedOffset(offset, side, instrumentType),
    hedge_flag: +(hedge_flag || 0),
    is_swap: !!is_swap,
    parent_id: 0n,
  };

  return Promise.resolve(makeOrderInput);
}

function handleResetMakeOrderForm(): void {
  const initFormState = initFormStateByConfig(configSettings.value, {});

  Object.keys(initFormState).forEach((key) => {
    formState.value[key] = initFormState[key];
  });

  nextTick().then(() => {
    formRef.value.clearValidate();
  });
}

// 拆单
async function handleApartOrder(): Promise<void> {
  try {
    await formRef.value.validate();
    const makeOrderInput: KungfuApi.MakeOrderInput = await initOrderInputData();
    const flag = await showCloseModal(makeOrderInput);
    if (!flag) return;
    const isContinue = await confirmContinueOrderModal(
      dealFatFingerMessage(makeOrderInput),
    );
    if (isContinue !== null && !isContinue) return;

    isShowConfirmModal.value = true;
    curOrderVolume.value = Number(makeOrderInput.volume);
    curOrderType.value = makeOrderInput.instrument_type;
  } catch (e) {
    if ((<Error>e).message) {
      error((<Error>e).message);
    }
  }
}

// 拆单弹窗确认回调
async function handleApartedConfirm(volumeList: number[]): Promise<void> {
  try {
    if (!makeOrderData.value || !currentGlobalKfLocation.value) return;

    const tdProcessId = await confirmOrderPlace(
      makeOrderData.value,
      volumeList.length,
    );
    if (!tdProcessId) return;

    const apartOrderInput: KungfuApi.MakeOrderInput = makeOrderData.value;

    Promise.all(
      volumeList.map((volume) => {
        apartOrderInput.volume = volume;
        return placeOrder(
          apartOrderInput as KungfuApi.MakeOrderInput,
          currentGlobalKfLocation.value as KungfuApi.KfLocation,
          tdProcessId,
        );
      }),
    );
  } catch (e) {
    if ((<Error>e).message) {
      error((<Error>e).message);
    }
  }
}

function confirmContinueOrderModal(
  warnningMessage: string,
  okText = t('tradingConfig.Continue'),
  cancelText = t('cancel'),
): Promise<boolean | null> {
  if (warnningMessage !== '') {
    return confirmModal(t('warning'), warnningMessage, okText, cancelText);
  } else {
    return Promise.resolve(null);
  }
}

function dealFatFingerMessage(
  makeOrderInput: KungfuApi.MakeOrderInput,
): string {
  if (!instrumentResolved.value) {
    return '';
  }

  const fatFingerRange = +globalSetting.value?.trade?.fatFinger || 0;

  if (fatFingerRange === 0) return '';

  const { exchangeId, instrumentId } = instrumentResolved.value;
  const ukey = hashInstrumentUKey(instrumentId, exchangeId);

  const { limit_price: price, side } = makeOrderInput;
  const lastPrice = window.watcher.ledger.Quote[ukey]?.last_price;

  const fatFingerBuyRate = (100 + fatFingerRange) / 100;
  const fatFingerSellRate = (100 - fatFingerRange) / 100;

  if (SideEnum.Buy == side && price > lastPrice * fatFingerBuyRate) {
    return t('tradingConfig.fat_finger_buy_modal', {
      price: price,
      warningLine: (lastPrice * fatFingerBuyRate).toFixed(4),
      fatFinger: fatFingerRange,
    });
  }

  if (SideEnum.Sell == side && price < lastPrice * fatFingerSellRate) {
    return t('tradingConfig.fat_finger_sell_modal', {
      price: price,
      warningLine: (lastPrice * fatFingerSellRate).toFixed(4),
      fatFinger: fatFingerRange,
    });
  }

  return '';
}

async function confirmOrderPlace(
  makeOrderInput: KungfuApi.MakeOrderInput,
  orderCount: number = 1,
): Promise<string> {
  if (!currentGlobalKfLocation.value || !window.watcher) {
    return Promise.reject(new Error(t('location_error')));
  }

  const { account_id } = formState.value;
  const tdProcessId =
    currentGlobalKfLocation.value?.category === 'td'
      ? getProcessIdByKfLocation(currentGlobalKfLocation.value)
      : `td_${account_id.toString()}`;

  if (processStatusData.value[tdProcessId] !== 'online') {
    return Promise.reject(
      new Error(t('tradingConfig.start_process', { process: tdProcessId })),
    );
  }

  const flag = await confirmModal(
    t('tradingConfig.place_confirm'),
    dealOrderPlaceVNode(makeOrderInput, orderCount, false),
  );

  if (!flag) return Promise.resolve('');

  return Promise.resolve(tdProcessId);
}

async function confirmApartCloseToOpen(
  makeOrderInput: KungfuApi.MakeOrderInput,
) {
  const { side, offset, volume } = makeOrderInput;

  if (
    isShotable(instrumentResolved.value?.instrumentType) &&
    offset !== OffsetEnum.Open
  ) {
    let direction: string = '',
      oppositeDirection: string = '',
      closableVolume: number | undefined = undefined;

    if (side === SideEnum.Buy) {
      if (currentPositionWithShortDirection.value) {
        closableVolume = Number(
          getPosClosableVolume(currentPositionWithShortDirection.value),
        );

        direction = t('tradingConfig.short');
        oppositeDirection = t('tradingConfig.long');
      }
    } else if (side === SideEnum.Sell) {
      if (currentPositionWithLongDirection.value) {
        closableVolume = Number(
          getPosClosableVolume(currentPositionWithLongDirection.value),
        );

        direction = t('tradingConfig.long');
        oppositeDirection = t('tradingConfig.short');
      }
    }

    if (direction === '' || closableVolume === undefined)
      return [makeOrderInput];

    if (volume > closableVolume) {
      const openVolume = volume - Number(closableVolume);
      const firstOrderInput: KungfuApi.MakeOrderInput = {
        ...makeOrderInput,
        volume: Number(closableVolume),
      };
      const secondOrderInput: KungfuApi.MakeOrderInput = {
        ...makeOrderInput,
        offset: OffsetEnum.Open,
        volume: volume - Number(closableVolume),
      };
      const flag = await confirmContinueOrderModal(
        t('tradingConfig.close_apart_open_modal', {
          direction,
          oppositeDirection,
          volume,
          closableVolume,
          openVolume,
        }),
        t('tradingConfig.original_plan'),
        t('tradingConfig.beyond_to_open'),
      );

      if (flag !== null) {
        if (flag) {
          return [makeOrderInput];
        } else {
          return [firstOrderInput, secondOrderInput];
        }
      }
    }
  }

  return [makeOrderInput];
}

// 下单
async function handleMakeOrder(): Promise<void> {
  try {
    if (!currentGlobalKfLocation.value) return;

    await formRef.value.validate();
    const makeOrderInput: KungfuApi.MakeOrderInput = await initOrderInputData();
    const flag = await showCloseModal(makeOrderInput);
    if (!flag) return;
    const isContinue = await confirmContinueOrderModal(
      dealFatFingerMessage(makeOrderInput),
    );
    if (isContinue !== null && !isContinue) return;
    const makeOrderInputs = await confirmApartCloseToOpen(makeOrderInput);

    for (let orderInput of makeOrderInputs) {
      const tdProcessId = await confirmOrderPlace(orderInput);
      if (!tdProcessId) continue;
      await placeOrder(orderInput, currentGlobalKfLocation.value, tdProcessId);
    }
  } catch (e) {
    if ((<Error>e).message) {
      error((<Error>e).message);
    }
  }
}

// 展示平仓弹窗
function showCloseModal(
  makeOrderInput: KungfuApi.MakeOrderInput,
): Promise<boolean> {
  if (!currentPosition.value || globalSetting.value?.trade?.close === 0)
    return Promise.resolve(true);

  const closeRange = +globalSetting.value?.trade?.close || 100;

  const { result, relationship } = closeModalConditions(
    closeRange,
    makeOrderInput,
    Number(currentPosition.value?.volume || 0),
  );

  if (result) {
    return confirmModal(
      t('prompt'),
      t('tradingConfig.continue_close_rate', {
        rate: closeRange + '',
        relationship,
      }),
    );
  }

  return Promise.resolve(true);
}

// 触发平仓弹窗条件
function closeModalConditions(
  closeRange: number,
  orderInput: KungfuApi.MakeOrderInput,
  positionVolume: number,
): {
  result: boolean;
  relationship?: string;
} {
  const makeOrderInput = dealStockOffset(orderInput);
  const { offset } = makeOrderInput;

  if (offset === OffsetEnum.Open) {
    return { result: false };
  }

  const positionVolumeResolved = positionVolume * (closeRange / 100);

  if (makeOrderInput.volume === positionVolumeResolved) {
    return {
      result: true,
      relationship: t('tradingConfig.reach'),
    };
  } else if (makeOrderInput.volume > positionVolumeResolved) {
    return {
      result: true,
      relationship: t('tradingConfig.above'),
    };
  } else {
    return {
      result: false,
    };
  }
}

const { handleOpenSetTradingTaskModal } = useTradingTask();
async function handleOpenTradingTaskConfigModal(
  kfExtConfig: KungfuApi.KfExtConfig,
) {
  try {
    if (!currentGlobalKfLocation.value) return;
    await formRef.value.validate();
    const taskInitValue = transformOrderInputToExtConfigForm(
      formState.value,
      configSettings.value,
      kfExtConfig.settings,
    );
    handleOpenSetTradingTaskModal('add', kfExtConfig.key, taskInitValue);
  } catch (e) {
    if ((<Error>e).message) {
      error((<Error>e).message);
    }
  }
}

const dealStringToNumber = (tar: string) =>
  Number.isNaN(Number(tar)) ? 0 : Number(tar);

let lastPercentSetVolume = 0;
const handlePercentChange = (target: number) => {
  const { side, offset } = formState.value;

  const curOffset = getResolvedOffset(
    offset,
    side,
    instrumentResolved.value?.instrumentType,
  );

  const targetPercent = target / 100;

  let targetVolume;
  if (curOffset === OffsetEnum.Open) {
    const availMoney = dealStringToNumber(currentAvailMoney.value);
    const allVolume = currentPrice.value ? availMoney / currentPrice.value : 0;
    targetVolume = allVolume * targetPercent;
  } else {
    const availPosVolume = dealStringToNumber(currentAvailPosVolume.value);
    targetVolume = availPosVolume * targetPercent;
  }

  formState.value.volume = dealVolumeByInstrumentType(
    Math.round(targetVolume),
    instrumentResolved.value?.instrumentType,
  );
  if (formState.value.volume) {
    currentPercent.value = target;
    lastPercentSetVolume = formState.value.volume;
  }
};

watch(
  () => formState.value.volume,
  (newVal) => {
    if (newVal !== lastPercentSetVolume) {
      currentPercent.value = 0;
    }
  },
);
</script>

<template>
  <div class="kf-make-order-dashboard__warp">
    <KfDashboard @boardSizeChange="handleBodySizeChange">
      <template v-slot:title>
        <span v-if="currentGlobalKfLocation">
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
            {{ $t('tradingConfig.reset_order') }}
          </a-button>
        </KfDashboardItem>
      </template>
      <div class="make-order__wrap">
        <div class="make-order-content">
          <div class="make-order-form__warp">
            <KfConfigSettingsForm
              ref="formRef"
              v-model:formState="formState"
              :configSettings="configSettings"
              :tdList="tdList"
              changeType="add"
              :label-col="LABEL_COL"
              :wrapper-col="WRAPPER_COL"
              :rules="rules"
              :steps="formSteps"
            ></KfConfigSettingsForm>
            <div class="percent-group__wrap">
              <a-col :span="LABEL_COL + WRAPPER_COL">
                <a-button
                  v-for="percent in percentList"
                  :class="{
                    'percent-button': true,
                    'percent-button-active': currentPercent === percent,
                  }"
                  :key="percent"
                  size="small"
                  ghost
                  @click="
                    currentPercent !== percent && handlePercentChange(percent)
                  "
                >
                  {{ `${percent}%` }}
                </a-button>
              </a-col>
            </div>
            <template v-if="isAccountOrInstrumentConfirmed">
              <div class="make-order-position">
                <a-col :span="LABEL_COL" class="position-label">
                  {{
                    showAmountOrPosition === 'amount'
                      ? $t('可用资金')
                      : $t('可用仓位')
                  }}
                </a-col>
                <a-col :span="WRAPPER_COL" class="position-value">
                  {{
                    showAmountOrPosition === 'amount'
                      ? currentAvailMoney
                      : currentAvailPosVolume
                  }}
                </a-col>
              </div>
              <div class="make-order-position">
                <a-col :span="LABEL_COL" class="position-label">
                  {{
                    isShotable(instrumentResolved?.instrumentType)
                      ? formState.offset === OffsetEnum.Open
                        ? t('保证金占用')
                        : t('保证金返还')
                      : $t('交易金额')
                  }}
                </a-col>
                <a-col :span="WRAPPER_COL" class="position-value">
                  {{ currentTradeAmount }}
                </a-col>
              </div>
              <div class="make-order-position">
                <a-col :span="LABEL_COL" class="position-label">
                  {{
                    showAmountOrPosition === 'amount'
                      ? $t('剩余资金')
                      : $t('剩余仓位')
                  }}
                </a-col>
                <a-col :span="WRAPPER_COL" class="position-value">
                  {{
                    showAmountOrPosition === 'amount'
                      ? currentResidueMoney
                      : currentResiduePosVolume
                  }}
                </a-col>
              </div>
              <a-card
                v-if="availTradingTaskExtensionList.length"
                class="make-order-algorithm__wrap"
                :title="$t('tradingConfig.algorithm')"
                size="small"
                :bodyStyle="{
                  padding: '0 8px 8px 0',
                  height: 'fit-content',
                }"
              >
                <a-button
                  class="make-order-algorithm-btns"
                  v-for="item in availTradingTaskExtensionList"
                  @click="handleOpenTradingTaskConfigModal(item)"
                >
                  {{
                    isLanguageKeyAvailable(item.name)
                      ? $t(item.name)
                      : item.name
                  }}
                </a-button>
              </a-card>
            </template>
          </div>
        </div>
        <div class="make-order-btns">
          <a-button class="make-order" @click="handleMakeOrder">
            {{ $t('tradingConfig.place_order') }}
          </a-button>
          <a-button @click="handleApartOrder">
            {{ $t('tradingConfig.apart_order') }}
          </a-button>
        </div>
      </div>
    </KfDashboard>
    <OrderConfirmModal
      v-if="isShowConfirmModal && curOrderType"
      v-model:visible="isShowConfirmModal"
      :curOrderVolume="curOrderVolume"
      :curOrderType="curOrderType"
      @confirm="handleApartedConfirm"
    ></OrderConfirmModal>
  </div>
</template>
<style lang="less">
.kf-make-order-dashboard__warp {
  width: 100%;
  height: 100%;

  .make-order__wrap {
    height: 100%;
    display: flex;
    justify-content: space-between;
    position: relative;

    .make-order-content {
      flex: 1;
      height: 100%;
      width: calc(100% - 44px);
      margin-right: 44px;
      display: flex;
      flex-direction: column;
      justify-content: flex-start;
    }

    .make-order-form__warp {
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

      .percent-group__wrap {
        margin: auto;
        padding-right: 16px;
        padding-left: 8px;
        box-sizing: border-box;

        .ant-col {
          margin: auto;
        }

        .percent-button {
          margin: 0px 8px 8px 0px;
          color: @border-color-base;
          border-color: @border-color-base;
        }

        .percent-button-active {
          color: @primary-color;
          border-color: @primary-color;
        }
      }
    }

    .make-order-position {
      display: flex;
      line-height: 1;
      font-size: 12px;
      color: @text-color-secondary;
      font-weight: bold;
      margin: 8px 0px;

      .position-label {
        padding-right: 8px;
        text-align: right;
      }

      .position-value {
        font-weight: bold;
      }

      &:first-child {
        margin-top: 8px;
      }
    }

    .make-order-algorithm__wrap {
      width: 90%;
      margin: 40px auto 8px;

      .make-order-algorithm-btns {
        margin: 8px 0 0 8px;
      }
    }

    .make-order-btns {
      width: 40px;
      height: 100%;
      display: flex;
      flex-direction: column;
      position: absolute;
      right: 0;

      .ant-btn {
        height: 26%;
        text-align: center;
        word-break: break-word;
        word-wrap: unset;
        white-space: normal;
        flex: 1;
        margin-bottom: 8px;

        &:last-child {
          margin-bottom: 0px;
        }
      }
      .make-order {
        height: 72%;
        flex: 4;
      }
    }
  }

  .green {
    color: @green-base !important;
  }
  .red {
    color: @red-base !important;
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

.ant-modal-confirm-content {
  white-space: pre-wrap;
}
</style>
