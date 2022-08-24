<script setup lang="ts">
import { ref, computed, toRefs, onMounted, toRaw } from 'vue';

import KfDashboard from '@kungfu-trader/kungfu-app/src/renderer/components/public/KfDashboard.vue';
import KfDashboardItem from '@kungfu-trader/kungfu-app/src/renderer/components/public/KfDashboardItem.vue';
import KfProcessStatus from '@kungfu-trader/kungfu-app/src/renderer/components/public/KfProcessStatus.vue';
import KfSetExtensionModal from '@kungfu-trader/kungfu-app/src/renderer/components/public/KfSetExtensionModal.vue';
import KfSetByConfigModal from '@kungfu-trader/kungfu-app/src/renderer/components/public/KfSetByConfigModal.vue';
import Icon, {
  FileTextOutlined,
  SettingOutlined,
  DeleteOutlined,
} from '@ant-design/icons-vue';

import { categoryRegisterConfig, getColumns } from './config';
import {
  useTableSearchKeyword,
  handleOpenLogview,
  useDashboardBodySize,
  getInstrumentTypeColor,
  isInTdGroup,
  confirmModal,
} from '@kungfu-trader/kungfu-app/src/renderer/assets/methods/uiUtils';
import {
  useAddUpdateRemoveKfConfig,
  handleSwitchProcessStatus,
  useSwitchAllConfig,
  useProcessStatusDetailData,
  useExtConfigsRelated,
  useCurrentGlobalKfLocation,
  useAllKfConfigData,
  useTdGroups,
  useAssets,
  useAssetMargins,
} from '@kungfu-trader/kungfu-app/src/renderer/assets/methods/actionsUtils';
import {
  dealAssetPrice,
  getIdByKfLocation,
  getIfProcessRunning,
  getIfProcessStopping,
  getProcessIdByKfLocation,
} from '@kungfu-trader/kungfu-js-api/utils/busiUtils';
import KfBlinkNum from '@kungfu-trader/kungfu-app/src/renderer/components/public/KfBlinkNum.vue';
import {
  addTdGroup,
  removeTdGroup,
  setTdGroup,
} from '@kungfu-trader/kungfu-js-api/actions';
import SetTdGroupModal from './SetTdGroupModal.vue';
import { useGlobalStore } from '@kungfu-trader/kungfu-app/src/renderer/pages/index/store/global';
import { messagePrompt } from '@kungfu-trader/kungfu-app/src/renderer/assets/methods/uiUtils';
import VueI18n from '@kungfu-trader/kungfu-js-api/language';
import { storeToRefs } from 'pinia';

const { t } = VueI18n.global;
const { success, error } = messagePrompt();
const { dashboardBodyHeight, handleBodySizeChange } = useDashboardBodySize();
const { globalSetting } = storeToRefs(useGlobalStore());
const isShowAssetMargin = computed(() => {
  return !!globalSetting.value?.trade?.assetMargin;
});

globalThis.HookKeeper.getHooks().dealTradingData.register(
  {
    category: categoryRegisterConfig.category,
    group: '*',
    name: '*',
  },
  categoryRegisterConfig,
);

const setSourceModalVisible = ref<boolean>(false);
const setTdModalVisible = ref<boolean>(false);
const setTdConfigPayload = ref<KungfuApi.SetKfConfigPayload>({
  type: 'add',
  title: t('Td'),
  config: {} as KungfuApi.KfExtConfig,
});

const currentSelectedSourceId = ref<string>('');
const { extConfigs, tdExtTypeMap } = useExtConfigsRelated();
const { td } = toRefs(useAllKfConfigData());
const tdIdList = computed(() => {
  return td.value.map(
    (item: KungfuApi.KfLocation): string => `${item.group}_${item.name}`,
  );
});
const { dealRowClassName, customRow } = useCurrentGlobalKfLocation(
  window.watcher,
);

const { processStatusData, getProcessStatusName } =
  useProcessStatusDetailData();
const { allProcessOnline, handleSwitchAllProcessStatus } = useSwitchAllConfig(
  td,
  processStatusData,
);

const tdGroupDataLoaded = ref<boolean>(false);
const addTdGroupModalVisble = ref<boolean>(false);
const setTdGroupModalVisble = ref<boolean>(false);
const tdGroup = useTdGroups();
const tdGroupNames = computed(() => {
  return tdGroup.value.map((item) => item.name);
});
const addTdGroupConfigPayload = ref<KungfuApi.SetKfConfigPayload>({
  type: 'add',
  title: t('tdConfig.account_group'),
  config: {} as KungfuApi.KfExtConfig,
});

const { searchKeyword, tableData } = useTableSearchKeyword<
  KungfuApi.KfConfig | KungfuApi.KfExtraLocation
>(td, ['group', 'name']);

const tableDataResolved = computed(() => {
  const tdGroupResolved: Record<string, KungfuApi.KfExtraLocation> = {};
  const tdResolved: KungfuApi.KfConfig[] = [];
  const markedNameToTdGroup: Record<string, KungfuApi.KfExtraLocation> = {};
  [...tdGroup.value, ...tableData.value].forEach((item) => {
    if ('children' in item) {
      markedNameToTdGroup[item.name] = { ...item };
      tdGroupResolved[item.name] = {
        ...item,
        key: item.name,
        children: [],
      };
      return;
    }

    const accountId = `${item.group}_${item.name}`;
    const targetGroupNames = Object.keys(markedNameToTdGroup).filter((name) => {
      const tdGroup = markedNameToTdGroup[name];
      return (tdGroup.children || []).includes(accountId);
    });
    if (targetGroupNames.length) {
      const targetGroupName = targetGroupNames[0];
      tdGroupResolved[targetGroupName].children?.push(item);
      return;
    }

    tdResolved.push(item as KungfuApi.KfConfig);
  });
  return [...Object.values(tdGroupResolved), ...tdResolved];
});

const { getAssetsByKfConfig, getAssetsByTdGroup } = useAssets();
const { getAssetMarginsByKfConfig, getAssetMarginsByTdGroup } =
  useAssetMargins();
const { handleConfirmAddUpdateKfConfig, handleRemoveKfConfig } =
  useAddUpdateRemoveKfConfig();

const columns = computed(() =>
  getColumns((dataIndex) => {
    return (a: KungfuApi.KfConfig, b: KungfuApi.KfConfig) => {
      return (
        (getAssetsByKfConfig(a)[dataIndex as keyof KungfuApi.Asset] || 0) -
        (getAssetsByKfConfig(b)[dataIndex as keyof KungfuApi.Asset] || 0)
      );
    };
  }, isShowAssetMargin.value),
);

const getPrefixByLocation = (kfLocation: KungfuApi.KfLocation) =>
  globalThis.HookKeeper.getHooks().prefix.trigger(kfLocation);

const { setTdGroups } = useGlobalStore();

onMounted(() => {
  setTdGroups().then(() => {
    tdGroupDataLoaded.value = true;
  });
});

async function handleOpenSetTdModal(
  type = 'add' as KungfuApi.ModalChangeType,
  selectedSource: string,
  tdConfig?: KungfuApi.KfConfig,
) {
  const extConfig: KungfuApi.KfExtConfig = (extConfigs.value['td'] || {})[
    selectedSource
  ];

  if (!extConfig) {
    error(
      t('tdConfig.td_not_found', {
        td: selectedSource,
      }),
    );
    return;
  }

  currentSelectedSourceId.value = selectedSource;
  setTdConfigPayload.value.type = type;
  setTdConfigPayload.value.title = `${selectedSource} ${t('Td')}`;
  setTdConfigPayload.value.config =
    await globalThis.HookKeeper.getHooks().resolveExtConfig.trigger(
      {
        category: 'td',
        group: selectedSource,
        name: '*',
      },
      extConfig,
    );
  setTdConfigPayload.value.initValue = undefined;

  if (type === 'update') {
    if (tdConfig) {
      setTdConfigPayload.value.initValue = JSON.parse(tdConfig.value);
    }
  }

  if (!extConfig?.settings?.length) {
    error(t('tdConfig.sourse_not_found'));
    return;
  }

  setTdModalVisible.value = true;
}

function handleOpenSetSourceDialog() {
  setSourceModalVisible.value = true;
}

function handleOpenAddTdGroupDialog(type: KungfuApi.ModalChangeType) {
  addTdGroupConfigPayload.value.type = type;
  addTdGroupConfigPayload.value.config = {
    type: [],
    name: t('tdConfig.td_group'),
    category: 'tdGroup',
    key: 'TdGroup',
    extPath: '',
    settings: [
      {
        key: 'td_group_name',
        name: t('tdConfig.td_name'),
        type: 'str',
        primary: true,
        required: true,
        tip: t('tdConfig.need_only_group'),
      },
    ],
  };
  addTdGroupConfigPayload.value.initValue = undefined;
  addTdGroupModalVisble.value = true;
}

function handleConfirmAddUpdateTdGroup(
  formState: Record<string, KungfuApi.KfConfigValue>,
) {
  const { td_group_name } = formState;
  const newTdGroup: KungfuApi.KfExtraLocation = {
    category: 'tdGroup',
    group: 'group',
    name: td_group_name.toString(),
    mode: 'live',
    children: [],
  };

  return addTdGroup(newTdGroup)
    .then(() => {
      return setTdGroups();
    })
    .then(() => {
      success();
    })
    .catch((err) => {
      error(err.message || t('operation_failed'));
    });
}

function handleRemoveTdGroup(item: KungfuApi.KfExtraLocation) {
  confirmModal(
    t('tdConfig.delete_amount_group', {
      group: item.name,
    }),
    `${t('tdConfig.delete_amount_group', {
      group: item.name,
    })}, ${t('tdConfig.confirm_delete_group')}`,
  ).then((flag) => {
    if (!flag) return;
    removeTdGroup(item.name)
      .then(() => {
        return setTdGroups();
      })
      .then(() => {
        success();
      })
      .catch((err) => {
        error(err.message || t('operation_failed'));
      });
  });
}

function handleRemoveTd(item: KungfuApi.KfConfig) {
  handleRemoveKfConfig(window.watcher, item, processStatusData.value)
    .then(() => {
      const accountId = getIdByKfLocation(item);
      const oldGroup = isInTdGroup(tdGroup.value, accountId);
      if (oldGroup) {
        const index = oldGroup.children?.indexOf(accountId);
        oldGroup.children.splice(index, 1);
        setTdGroup(toRaw(tdGroup.value)).then(() => {
          return setTdGroups();
        });
      }
    })
    .then(() => {
      success();
    })
    .catch((err) => {
      error(err.message || t('operation_failed'));
    });
}
</script>

<template>
  <div class="kf-td__warp kf-translateZ">
    <KfDashboard @boardSizeChange="handleBodySizeChange">
      <template v-slot:header>
        <KfDashboardItem>
          <a-input-search
            v-model:value="searchKeyword"
            :placeholder="$t('keyword_input')"
            style="width: 120px"
          />
        </KfDashboardItem>
        <KfDashboardItem>
          <a-switch
            :checked="allProcessOnline"
            @click="handleSwitchAllProcessStatus"
          ></a-switch>
        </KfDashboardItem>
        <KfDashboardItem>
          <a-button size="small" @click="handleOpenAddTdGroupDialog('add')">
            {{ $t('tdConfig.add_group_placeholder') }}
          </a-button>
        </KfDashboardItem>
        <KfDashboardItem>
          <a-button
            size="small"
            type="primary"
            @click="handleOpenSetSourceDialog"
          >
            {{ $t('tdConfig.add_td') }}
          </a-button>
        </KfDashboardItem>
      </template>
      <a-table
        v-if="tdGroupDataLoaded"
        class="kf-ant-table"
        :columns="columns"
        :data-source="tableDataResolved"
        size="small"
        :pagination="false"
        :scroll="{ y: dashboardBodyHeight - 4 }"
        :rowClassName="dealRowClassName"
        :customRow="customRow"
        :defaultExpandAllRows="true"
        :emptyText="$t('empty_text')"
      >
        <template
          #bodyCell="{
            column,
            record,
          }: {
            column: AntTableColumn,
            record: KungfuApi.KfConfig | KungfuApi.KfExtraLocation,
          }"
        >
          <template
            v-if="column.dataIndex === 'name' && record.category === 'td'"
          >
            <div class="td-name__warp">
              <a-tag
                :color="getInstrumentTypeColor(tdExtTypeMap[record.group])"
              >
                {{ record.group }}
              </a-tag>
              <span>
                {{ record.name }}
              </span>
              <Icon
                v-if="getPrefixByLocation(record).prefixType === 'icon'"
                :component="getPrefixByLocation(record).prefix"
                style="font-size: 12px; margin-left: 7px"
              />
            </div>
          </template>
          <template
            v-else-if="
              column.dataIndex === 'name' && record.category === 'tdGroup'
            "
          >
            <div class="td-name__warp">
              <a-tag color="#FAAD14">账户组</a-tag>
              <span>
                {{ record.name }}
              </span>
            </div>
          </template>
          <template
            v-else-if="
              column.dataIndex === 'accountName' && record.category === 'td'
            "
          >
            {{
              JSON.parse((record as KungfuApi.KfConfig).value).account_name ||
              '--'
            }}
          </template>

          <template v-else-if="column.dataIndex === 'stateStatus'">
            <KfProcessStatus
              v-if="record.category === 'td'"
              :statusName="getProcessStatusName(record)"
            ></KfProcessStatus>
          </template>
          <template v-else-if="column.dataIndex === 'processStatus'">
            <a-switch
              v-if="record.category === 'td'"
              size="small"
              :checked="
                getIfProcessRunning(
                  processStatusData,
                  getProcessIdByKfLocation(record),
                )
              "
              :loading="
                getIfProcessStopping(
                  processStatusData,
                  getProcessIdByKfLocation(record),
                )
              "
              @click="
                                (checked: boolean, Event: MouseEvent) => handleSwitchProcessStatus(checked, Event, record)
                            "
            ></a-switch>
          </template>
          <template v-else-if="column.dataIndex === 'unrealizedPnl'">
            <KfBlinkNum
              v-if="record.category === 'td'"
              mode="compare-zero"
              :num="dealAssetPrice(getAssetsByKfConfig(record).unrealized_pnl)"
            ></KfBlinkNum>
            <KfBlinkNum
              v-else-if="record.category === 'tdGroup'"
              mode="compare-zero"
              :num="dealAssetPrice(getAssetsByTdGroup(record).unrealized_pnl)"
            ></KfBlinkNum>
          </template>
          <template v-else-if="column.dataIndex === 'marketValue'">
            <KfBlinkNum
              v-if="record.category === 'td'"
              :num="dealAssetPrice(getAssetsByKfConfig(record).market_value)"
            ></KfBlinkNum>
            <KfBlinkNum
              v-else-if="record.category === 'tdGroup'"
              :num="dealAssetPrice(getAssetsByTdGroup(record).market_value)"
            ></KfBlinkNum>
          </template>
          <template v-else-if="column.dataIndex === 'margin'">
            <KfBlinkNum
              v-if="record.category === 'td'"
              :num="
                dealAssetPrice(
                  getAssetsByKfConfig(record).margin ||
                    getAssetMarginsByKfConfig(record).margin,
                )
              "
            ></KfBlinkNum>
            <KfBlinkNum
              v-else-if="record.category === 'tdGroup'"
              :num="
                dealAssetPrice(
                  getAssetsByTdGroup(record).margin ||
                    getAssetMarginsByTdGroup(record).margin,
                )
              "
            ></KfBlinkNum>
          </template>
          <template v-else-if="column.dataIndex === 'avail'">
            <KfBlinkNum
              v-if="record.category === 'td'"
              mode="compare-zero"
              :num="dealAssetPrice(getAssetsByKfConfig(record).avail)"
            ></KfBlinkNum>
            <KfBlinkNum
              v-else-if="record.category === 'tdGroup'"
              :num="dealAssetPrice(getAssetsByTdGroup(record).avail)"
            ></KfBlinkNum>
          </template>
          <template
            v-else-if="isShowAssetMargin && column.dataIndex === 'avail_margin'"
          >
            <KfBlinkNum
              v-if="record.category === 'td'"
              mode="compare-zero"
              :num="
                dealAssetPrice(getAssetMarginsByKfConfig(record).avail_margin)
              "
            ></KfBlinkNum>
            <KfBlinkNum
              v-else-if="record.category === 'tdGroup'"
              :num="
                dealAssetPrice(getAssetMarginsByTdGroup(record).avail_margin)
              "
            ></KfBlinkNum>
          </template>
          <template
            v-else-if="isShowAssetMargin && column.dataIndex === 'cash_debt'"
          >
            <KfBlinkNum
              v-if="record.category === 'td'"
              mode="compare-zero"
              :num="dealAssetPrice(getAssetMarginsByKfConfig(record).cash_debt)"
            ></KfBlinkNum>
            <KfBlinkNum
              v-else-if="record.category === 'tdGroup'"
              :num="dealAssetPrice(getAssetMarginsByTdGroup(record).cash_debt)"
            ></KfBlinkNum>
          </template>
          <template
            v-else-if="isShowAssetMargin && column.dataIndex === 'total_asset'"
          >
            <KfBlinkNum
              v-if="record.category === 'td'"
              mode="compare-zero"
              :num="
                dealAssetPrice(getAssetMarginsByKfConfig(record).total_asset)
              "
            ></KfBlinkNum>
            <KfBlinkNum
              v-else-if="isShowAssetMargin && record.category === 'tdGroup'"
              :num="
                dealAssetPrice(getAssetMarginsByTdGroup(record).total_asset)
              "
            ></KfBlinkNum>
          </template>
          <template v-else-if="column.dataIndex === 'actions'">
            <div class="kf-actions__warp" v-if="record.category === 'td'">
              <FileTextOutlined
                style="font-size: 12px"
                @click.stop="handleOpenLogview(record)"
              />
              <SettingOutlined
                style="font-size: 12px"
                @click.stop="
                  handleOpenSetTdModal(
                    'update',
                    record.group,
                    record as KungfuApi.KfConfig,
                  )
                "
              />
              <DeleteOutlined
                style="font-size: 12px"
                @click.stop="handleRemoveTd(record as KungfuApi.KfConfig)"
              />
            </div>
            <div class="kf-actions__warp" v-if="record.category === 'tdGroup'">
              <SettingOutlined
                style="font-size: 12px"
                @click.stop="setTdGroupModalVisble = true"
              />
              <DeleteOutlined
                style="font-size: 12px"
                @click.stop="handleRemoveTdGroup(record)"
              />
            </div>
          </template>
        </template>
      </a-table>
    </KfDashboard>
    <KfSetExtensionModal
      v-if="setSourceModalVisible"
      v-model:visible="setSourceModalVisible"
      extensionType="td"
      @confirm="handleOpenSetTdModal('add', $event)"
    ></KfSetExtensionModal>
    <KfSetByConfigModal
      v-if="setTdModalVisible"
      v-model:visible="setTdModalVisible"
      :payload="setTdConfigPayload"
      :primaryKeyAvoidRepeatCompareTarget="tdIdList"
      :primaryKeyAvoidRepeatCompareExtra="currentSelectedSourceId"
      @confirm="
        handleConfirmAddUpdateKfConfig($event, 'td', currentSelectedSourceId)
      "
    ></KfSetByConfigModal>
    <KfSetByConfigModal
      v-if="addTdGroupModalVisble"
      v-model:visible="addTdGroupModalVisble"
      :payload="addTdGroupConfigPayload"
      :primaryKeyAvoidRepeatCompareTarget="tdGroupNames"
      @confirm="({ formState }) => handleConfirmAddUpdateTdGroup(formState)"
    ></KfSetByConfigModal>
    <SetTdGroupModal
      v-if="setTdGroupModalVisble"
      v-model:visible="setTdGroupModalVisble"
    ></SetTdGroupModal>
  </div>
</template>
<style lang="less">
.kf-td__warp {
  height: 100%;

  .td-name__warp {
    word-break: break-all;
  }
}
</style>